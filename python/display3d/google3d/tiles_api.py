# -*- coding: utf-8 -*-
"""Google Map Tiles API —— 摄影测量 3D 瓦片拉取（对标官方 create-renderer 文档）
流程：GET root.json?key= → 响应里的相对 URI 自带 session → 逐级下载子 tileset(.json)
与瓦片内容(.glb)，所有请求都要补 &key=。
boundingVolume.region = [west, south, east, north(弧度), minH, maxH]（EPSG:4979）。
注意：瓦片数据版权归 Google，按 ToS 只做运行时显示与短期缓存，不得持久分发。
"""

import json
import os

import numpy as np
import requests

TILES_HOST = 'https://tile.googleapis.com'
_CACHE_DIR = os.path.join('res', 'google3d_cache')


class GoogleTilesError(Exception):
    pass


def _ensure_key(url, api_key):
    """相对 URI 补全域名 + key 参数（官方规则：URI 不含 key，渲染器自行附加）"""
    if url.startswith('/'):
        url = TILES_HOST + url
    if 'key=' not in url:
        sep = '&' if '?' in url else '?'
        url = f'{url}{sep}key={api_key}'
    return url


class GoogleTilesSession:
    """一次浏览会话：root.json 打开后 session 挂在所有子 URI 上，失效需重新 open"""

    def __init__(self, api_key, cache_dir=None, timeout=30):
        self.api_key = api_key
        self.timeout = timeout
        self.cache_dir = cache_dir or _CACHE_DIR
        self.root = None
        self._http = requests.Session()

    # ---------- 基础请求（带磁盘缓存） ----------

    def _cache_path(self, url):
        import hashlib
        name = hashlib.sha1(url.split('&key=')[0].encode('utf-8')).hexdigest()
        return os.path.join(self.cache_dir, name)

    def _get(self, url):
        """下载 URL：json 落盘缓存（session 只在文件名前剥离，glb 也缓存）"""
        os.makedirs(self.cache_dir, exist_ok=True)
        path = self._cache_path(url)
        if os.path.exists(path):
            with open(path, 'rb') as f:
                return f.read()
        r = self._http.get(url, timeout=self.timeout)
        if r.status_code != 200:
            raise GoogleTilesError(f'HTTP {r.status_code}: {url[:120]}')
        with open(path, 'wb') as f:
            f.write(r.content)
        return r.content

    # ---------- 入口 ----------

    def open(self):
        """拉根 tileset；失败时（session 过期等）由调用方重试"""
        url = _ensure_key(f'{TILES_HOST}/v1/3dtiles/root.json', self.api_key)
        self.root = json.loads(self._get(url))
        return self.root

    # ---------- 瓦片树遍历 / ROI 圈选 ----------

    @staticmethod
    def _region_of(tile):
        region = (tile.get('boundingVolume') or {}).get('region')
        if region and len(region) >= 4:
            return region[:4]     # west, south, east, north（弧度）
        return None

    @staticmethod
    def _intersects(region, roi):
        """弧度矩形相交测试（不跨经线 180° 的场景够用）"""
        rw, rs, re_, rn = region
        ow, os_, oe, on = roi
        return not (re_ <= ow or rw >= oe or rn <= os_ or rs >= on)

    def select_tiles(self, lat, lng, radius_deg, max_tiles=150,
                     max_geometric_error=12.0):
        """圈定目标区域瓦片：
        - region 不相交 ROI 的分支整枝剪掉
        - 几何误差 > 阈值且还有子级 → 继续向下（REPLACE 语义：子级替换父级）
        - 到叶子或误差达标 → 记 (glb URI, 累乘的 tile transform 列主序16元组)
        返回 [(uri, transform)] 列表
        """
        if self.root is None:
            self.open()
        import math
        roi = (math.radians(lng - radius_deg), math.radians(lat - radius_deg),
               math.radians(lng + radius_deg), math.radians(lat + radius_deg))
        selected, visited = [], 0
        IDENTITY = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)

        def mat_mul(a, b):
            """两个列主序 4x4 相乘（3D Tiles transform 累乘），返回列主序"""
            A = np.array(a, dtype=np.float64).reshape(4, 4).T
            B = np.array(b, dtype=np.float64).reshape(4, 4).T
            return (A @ B).T.reshape(-1).tolist()

        def visit(tile, parent_transform):
            nonlocal visited
            if len(selected) >= max_tiles or visited > max_tiles * 40:
                return
            visited += 1
            region = self._region_of(tile)
            if region and not self._intersects(region, roi):
                return
            t = tile.get('transform') or IDENTITY
            transform = mat_mul(parent_transform, t)
            content = tile.get('content') or {}
            uri = content.get('uri', '')
            # URI 带 ?session= 查询串，先剥掉再判类型
            path = uri.split('?')[0]
            ge = tile.get('geometricError', 0.0)
            children = tile.get('children') or []

            if path.endswith('.json'):
                # 子 tileset：下载展开，其 root 当作当前 tile 的内容继续走
                sub_url = _ensure_key(uri, self.api_key)
                try:
                    sub_root = json.loads(self._get(sub_url))['root']
                except GoogleTilesError as e:
                    raise GoogleTilesError(f'子瓦片集下载失败: {e}')
                region = self._region_of(sub_root) or region
                if region and not self._intersects(region, roi):
                    return
                visit(sub_root, transform)
                return

            if path.endswith('.glb'):
                if children and ge > max_geometric_error:
                    # 还有更精细的子级 → 下钻（REPLACE：父级不画）
                    for child in children:
                        visit(child, transform)
                    return
                selected.append((uri, transform))
                return

            for child in children:
                visit(child, transform)

        visit(self.root.get('root') or {}, IDENTITY)
        return selected

    def download_glb(self, uri):
        """下载 .glb，返回原始字节"""
        return self._get(_ensure_key(uri, self.api_key))
