# -*- coding: utf-8 -*-
"""GLB(glTF 2.0 binary) 解析 → 世界坐标网格 + RGBA 贴图
- 处理 KHR_mesh_quantization 量化顶点（int8/16 归一化）
- 不支持 KHR_draco_mesh_compression / EXT_meshopt_compression（Google 服务端已解 Draco，
  出现时抛可读错误）
- 坐标链：顶点(y-up 局部) × node 矩阵 × tile transform → ECEF(m)
  → Bowring 迭代转经纬高 → 与 settings.gps_to_world_pos 同一公式 → 场景世界坐标
"""

import io
import json
import struct

import numpy as np

# WGS84
_A = 6378137.0
_E2 = 6.6943799901413165e-3

_COMP_TYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16,
               5123: np.uint16, 5125: np.uint32, 5126: np.float32}
_NORM_DEN = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}
_VEC_N = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


class GlbError(Exception):
    pass


def _read_accessor(gltf_json, bin_chunk, idx):
    """读 accessor → (N, comp) float32 数组（归一化量化已还原）"""
    acc = gltf_json['accessors'][idx]
    ncomp = _VEC_N[acc['type']]
    count = acc['count']
    comp_type = acc['componentType']
    dtype = _COMP_TYPES[comp_type]
    item_size = np.dtype(dtype).itemsize

    bv = gltf_json['bufferViews'][acc['bufferView']]
    base = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride') or item_size * ncomp

    raw = bin_chunk
    if stride == item_size * ncomp:                       # 紧密排列
        flat = np.frombuffer(raw, dtype=dtype, count=count * ncomp, offset=base)
        arr = flat.reshape(count, ncomp).astype(np.float32)
    else:                                                 # 跨步排列
        arr = np.empty((count, ncomp), dtype=np.float32)
        for c in range(ncomp):
            col = np.frombuffer(raw, dtype=dtype, count=count,
                                offset=base + c * item_size)
            # 跨步：手动步进（数量小，向量切片按 stride 不可靠，用规则间隔拷贝）
            arr[:, c] = col[::stride // item_size] if stride % item_size == 0 else 0
            if stride % item_size != 0:
                raise GlbError('异常的 bufferView byteStride')
    if acc.get('normalized'):
        arr = arr / _NORM_DEN[comp_type]
    return arr


def _read_indices(gltf_json, bin_chunk, idx):
    acc = gltf_json['accessors'][idx]
    dtype = _COMP_TYPES[acc['componentType']]
    bv = gltf_json['bufferViews'][acc['bufferView']]
    base = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    flat = np.frombuffer(bin_chunk, dtype=dtype, count=acc['count'], offset=base)
    return flat.astype(np.uint32)


def _node_matrix(node):
    m = node.get('matrix')
    if m:
        return np.array(m, dtype=np.float64).reshape(4, 4).T   # glTF 列主序
    t = node.get('translation', [0, 0, 0])
    r = node.get('rotation', [0, 0, 0, 1])
    s = node.get('scale', [1, 1, 1])
    x, y, z, w = r
    rot = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ], dtype=np.float64)
    mat = np.eye(4)
    mat[:3, :3] = rot * s
    mat[:3, 3] = t
    return mat


def _ecef_to_llh(x, y, z):
    """ECEF(m) → (lat_deg, lng_deg, h_m)，Bowring 迭代 4 次，向量化"""
    p = np.hypot(x, y)
    lng = np.arctan2(y, x)
    lat = np.arctan2(z, p * (1.0 - _E2))
    for _ in range(4):
        sin_lat = np.sin(lat)
        n = _A / np.sqrt(1.0 - _E2 * sin_lat * sin_lat)
        lat = np.arctan2(z + _E2 * n * sin_lat, p)
    sin_lat = np.sin(lat)
    n = _A / np.sqrt(1.0 - _E2 * sin_lat * sin_lat)
    h = p / np.cos(lat) - n
    return np.degrees(lat), np.degrees(lng), h


class TileMesh:
    """一个瓦片解析结果：若干图元（顶点/UV/索引/贴图 RGBA）"""

    def __init__(self):
        self.primitives = []   # {'positions','uvs','indices','tex':(rgba,w,h) 或 None}


def parse_glb(data, tile_transform, lat0, lng0, scale_x, scale_z,
              height_scale=1.0, height_offset=0.0):
    """解析 glb → TileMesh（顶点已是场景世界坐标）
    tile_transform: 3D Tiles 逐级累乘的 transform（列主序16元组），tile 局部 → ECEF(m)
    """
    magic, version, length = struct.unpack_from('<III', data, 0)
    if magic != 0x46546C67:
        raise GlbError('不是 GLB 文件')
    offset, gltf_json, bin_chunk = 12, None, b''
    while offset < length:
        csize, ctype = struct.unpack_from('<II', data, offset)
        chunk = data[offset + 8: offset + 8 + csize]
        if ctype == 0x4E4F534A:
            gltf_json = json.loads(chunk.decode('utf-8'))
        elif ctype == 0x004E4942:
            bin_chunk = chunk
        offset += 8 + csize + ((4 - csize % 4) % 4)
    if gltf_json is None:
        raise GlbError('GLB 缺少 JSON chunk')

    unsupported = set(gltf_json.get('extensionsRequired', [])) - {'KHR_mesh_quantization'}
    if unsupported:
        raise GlbError(f'暂不支持的压缩扩展: {", ".join(unsupported)}')

    import math
    lat0_r, lng0_r = math.radians(lat0), math.radians(lng0)
    # ECEF → 世界：等价于 (lat-lat0)*scaleX / -(lng-lng0)*scaleZ（与 gps_to_world_pos 一致）
    n0 = _A / np.sqrt(1.0 - _E2 * math.sin(lat0_r) ** 2)

    mesh = TileMesh()
    scene = gltf_json['scenes'][gltf_json.get('scene', 0)]
    # tile transform（列主序）→ 行主序 4x4；glb 内容是 y-up，Google 根矩阵内含 z-up 修正
    tmat = np.array(tile_transform or [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                                       0, 0, 0, 1], dtype=np.float64).reshape(4, 4).T

    def visit_node(node_idx, parent_m):
        node = gltf_json['nodes'][node_idx]
        m = tmat @ parent_m @ _node_matrix(node)
        if 'mesh' in node:
            _add_mesh(gltf_json['meshes'][node['mesh']], m)
        for child in node.get('children', []):
            visit_node(child, m)

    def _add_mesh(gmesh, m):
        for prim in gmesh.get('primitives', []):
            attrs = prim.get('attributes', {})
            if 'POSITION' not in attrs or 'TEXCOORD_0' not in attrs:
                continue
            pos = _read_accessor(gltf_json, bin_chunk, attrs['POSITION'])
            uv = _read_accessor(gltf_json, bin_chunk, attrs['TEXCOORD_0'])[:, :2]
            # y-up 局部 × node 矩阵 → ECEF(m)
            v = np.empty((len(pos), 4))
            v[:, :3] = pos
            v[:, 3] = 1.0
            ecef = v @ m.T
            lat, lng, h = _ecef_to_llh(ecef[:, 0], ecef[:, 1], ecef[:, 2])
            # 对齐 settings.gps_to_world_pos：x=(lat-lat0)*scaleX, z=-(lng-lng0)*scaleZ
            wx = (lat - math.degrees(lat0_r)) * scale_x
            wz = -(lng - math.degrees(lng0_r)) * scale_z
            wy = (h - height_offset) * height_scale
            positions = np.stack([wx, wy, wz], axis=1).astype(np.float32)

            indices = _read_indices(gltf_json, bin_chunk, prim['indices']) \
                if 'indices' in prim else \
                np.arange(len(positions), dtype=np.uint32)

            tex = None
            mat_idx = prim.get('material')
            if mat_idx is not None:
                tex = _load_texture(gltf_json, bin_chunk, gltf_json['materials'][mat_idx])
            mesh.primitives.append({
                'positions': positions.astype(np.float32),
                'uvs': uv.astype(np.float32),
                'indices': indices,
                'tex': tex,
            })

    for root_idx in scene['nodes']:
        visit_node(root_idx, np.eye(4))
    return mesh


def _load_texture(gltf_json, bin_chunk, material):
    """材质 baseColorTexture → (rgba_bytes, w, h)；ktx2 走外部转码"""
    tex_ref = material.get('pbrMetallicRoughness', {}).get('baseColorTexture')
    if tex_ref is None:
        return None
    tex_idx = tex_ref.get('index') if isinstance(tex_ref, dict) else tex_ref
    img_idx = gltf_json['textures'][tex_idx].get('source')
    if img_idx is None:
        ext = gltf_json['textures'][tex_idx].get('extensions', {}).get('KHR_texture_basisu')
        if ext:
            img_idx = ext['source']
    if img_idx is None:
        return None
    image = gltf_json['images'][img_idx]
    mime = image.get('mimeType', '')
    if 'bufferView' in image:
        bv = gltf_json['bufferViews'][image['bufferView']]
        raw = bin_chunk[bv.get('byteOffset', 0): bv.get('byteOffset', 0) + bv['byteLength']]
    elif 'uri' in image:
        raise GlbError(f'外部贴图 uri 暂不支持: {image["uri"][:60]}')
    else:
        return None

    from display3d.google3d.ktx2_decode import decode_image
    return decode_image(mime, raw)
