# -*- coding: utf-8 -*-
"""KTX2 贴图解码 → RGBA
Google 摄影测量瓦片的贴图是 KTX2 + Basis Universal(ETC1S/UASTC) 压缩，
Python 生态没有现成转码器，方案：
1. 未压缩 vkFormat（RGBA8 等）直接解析像素（zstd 超压缩先解）
2. BasisU 压缩 → 调外部官方工具转码（按优先级探测）：
   - ktx2ktx2（KTX-Software 自带，转码目标 R8G8B8A8_UNORM）
   - basisu（Binomial CLI，输出 PNG）
   工具放置位置：res/tools/ 或系统 PATH，结果按内容哈希缓存
"""

import os
import shutil
import struct
import subprocess
import tempfile
import hashlib

import numpy as np

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_TOOLS_DIRS = [os.path.join(_PROJECT_ROOT, 'res', 'tools')]
_VK_UNCOMPRESSED = {37: ('RGBA', 4), 43: ('RGBA', 4),   # R8G8B8A8 / B8G8R8A8(重排)
                    56: ('RGB', 3), 50: ('RGB', 3)}      # R8G8B8 / B8G8R8
_MAGIC = b'\xabKTX 20\xbb\r\n\x1a\n'


class Ktx2Error(Exception):
    pass


# ---------- KTX2 容器解析 ----------

def _parse_ktx2(data):
    """→ (vk_format, w, h, level_count, supercompression_scheme, levels[（off,len,uncomp_len)])"""
    if data[:12] != _MAGIC:
        raise Ktx2Error('不是 KTX2 文件')
    (vk_format, type_size, w, h, depth, layer_count, face_count,
     level_count, scheme) = struct.unpack_from('<9I', data, 12)
    off = 12 + 9 * 4
    levels = []
    for _ in range(level_count):
        bo, bl, ul = struct.unpack_from('<QQQ', data, off)
        levels.append((bo, bl, ul))
        off += 24
    return vk_format, w, h, level_count, scheme, levels


def _data_start(data, level_count):
    """levelIndex 后紧跟 DFD/KVD，纹理数据起点按 4 字节对齐（scheme=1 时另有约定）"""
    off = 12 + 9 * 4 + level_count * 24
    dfd_off, dfd_size = struct.unpack_from('<II', data, off)
    off += 8
    kvd_off, kvd_size = struct.unpack_from('<QQ', data, off)
    off += 16
    sgd_off, sgd_size = struct.unpack_from('<QQ', data, off)
    off += 16
    # 各 section 指针均为文件绝对偏移；数据区起点 = 所有节末尾的最大值（≥当前读指针）
    end = max(dfd_off + dfd_size, kvd_off + kvd_size, sgd_off + sgd_size, off)
    return end


def _decode_uncompressed(data, vk_format, w, h, scheme, levels):
    bo, bl, _ul = levels[0]
    if scheme == 1:
        # scheme=1：数据区整段 zstd 压缩，level 偏移相对解压后流起点
        import zstandard
        start = _data_start(data, len(levels))
        blob = zstandard.ZstdDecompressor().decompressobj().decompress(data[start:])
        pixels = blob[bo: bo + bl]
    else:
        # scheme=0：level 偏移是相对文件起始的绝对偏移
        pixels = data[bo: bo + bl]
    if vk_format not in _VK_UNCOMPRESSED:
        raise Ktx2Error(f'未压缩格式 {vk_format} 暂不支持')
    order, ch = _VK_UNCOMPRESSED[vk_format]
    img = np.frombuffer(pixels[:w * h * ch], dtype=np.uint8).reshape(h, w, ch)
    if order == 'RGB':
        img = np.concatenate([img, np.full((h, w, 1), 255, np.uint8)], axis=2)
    elif vk_format == 43:      # B8G8R8A8 → RGBA
        img = img[:, :, [2, 1, 0, 3]]
    return np.ascontiguousarray(img), w, h


# ---------- BasisU：外部工具转码 ----------

def _find_tool(names):
    for d in _TOOLS_DIRS:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return p
    for n in names:
        p = shutil.which(n)
        if p:
            return p
    return None


def _cache_path(data, ext):
    os.makedirs('res/google3d_cache/tex', exist_ok=True)
    return os.path.join('res', 'google3d_cache', 'tex',
                        hashlib.sha1(data).hexdigest() + ext)


def _transcode_by_tool(data):
    """ktx2ktx2 优先（输出未压缩 ktx2），其次 basisu（输出 png）"""
    cache = _cache_path(data, '.rgba.npy')
    if os.path.exists(cache):
        with open(cache, 'rb') as f:
            payload = np.load(f, allow_pickle=True)
        return payload.item() if payload.ndim == 0 else tuple(payload)

    ktx2ktx2 = _find_tool(['ktx2ktx2.exe', 'ktx2ktx2'])
    if ktx2ktx2:
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, 'in.ktx2')
            dst = os.path.join(td, 'out.ktx2')
            with open(src, 'wb') as f:
                f.write(data)
            # 不同版本参数不同，逐个尝试直到输出可解析的未压缩 KTX2
            for extra in (['--format', 'R8G8B8A8_UNORM'], ['-t', 'R8G8B8A8_UNORM'], []):
                r = subprocess.run([ktx2ktx2, src, '-o', dst, *extra],
                                   capture_output=True, timeout=120)
                if r.returncode != 0 or not os.path.exists(dst):
                    continue
                with open(dst, 'rb') as f:
                    out = f.read()
                try:
                    vk, w, h, lc, scheme, levels = _parse_ktx2(out)
                    if vk in _VK_UNCOMPRESSED:
                        img, w, h = _decode_uncompressed(out, vk, w, h, scheme, levels)
                        result = (np.ascontiguousarray(img), w, h)
                        np.save(cache, np.array(result, dtype=object))
                        return result
                except (Ktx2Error, struct.error):
                    continue

    basisu = _find_tool(['basisu.exe', 'basisu'])
    if basisu:
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, 'in.ktx2')
            with open(src, 'wb') as f:
                f.write(data)
            r = subprocess.run([basisu, '-unpack', '-no_ktx', '-format_only', '6', src],
                               capture_output=True, timeout=120, cwd=td)
            # BC7 RGBA output preserves color and alpha; *_a_* PNGs are alpha only.
            pngs = sorted(p for p in os.listdir(td)
                          if '_unpacked_rgba_' in p and p.endswith('.png'))
            if r.returncode == 0 and pngs:
                from PIL import Image
                img = Image.open(os.path.join(td, pngs[0])).convert('RGBA')
                arr = np.asarray(img, dtype=np.uint8)
                result = (arr, arr.shape[1], arr.shape[0])
                np.save(cache, np.array(result, dtype=object))
                return result

    raise Ktx2Error(
        'KTX2(BasisU) 贴图需要官方转码工具：请下载 KTX-Software 或 basisu 的 ktx2ktx2.exe /'
        ' basisu.exe 放到 res/tools/ 目录（或加入 PATH）后重试。'
        '下载地址：https://github.com/KhronosGroup/KTX-Software/releases')


def decode_image(mime, raw):
    """glTF image(mimeType, 字节) → (h,w,4) uint8 RGBA"""
    if mime == 'image/png' or mime == 'image/jpeg':
        from PIL import Image
        import io
        img = Image.open(io.BytesIO(raw)).convert('RGBA')
        return np.asarray(img, dtype=np.uint8), img.width, img.height
    if mime == 'image/ktx2' or raw[:4] == _MAGIC[:4]:
        vk, w, h, lc, scheme, levels = _parse_ktx2(raw)
        if vk == 0:      # VK_FORMAT_UNDEFINED = BasisU 超压缩
            return _transcode_by_tool(raw)
        return _decode_uncompressed(raw, vk, w, h, scheme, levels)
    raise Ktx2Error(f'不支持的贴图类型: {mime or "未知"}')
