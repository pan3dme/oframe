# -*- coding: utf-8 -*-
"""卫星地图组件：XYZ 瓦片在线加载（默认 ArcGIS World Imagery，免 key；
支持天地图 AK 切换），拖拽平移 / 滚轮缩放 / GPS 轨迹折线绘制。
瓦片磁盘缓存 res/sat_cache，先出底图、轨迹随瓦片落地即画。"""

import math
import os
import queue
import threading

import requests
from PyQt6.QtCore import Qt, pyqtSignal, QRectF, QPointF
from PyQt6.QtGui import (QPainter, QPixmap, QPen, QBrush, QColor, QFont,
                         QPainterPath)
from PyQt6.QtWidgets import QWidget

# 瓦片源：arcgis=免key全球卫星影像；tianditu=天地图(需 config 里 TIANDITU_AK)
TILE_ARCGIS = ('https://server.arcgisonline.com/ArcGIS/rest/services/'
               'World_Imagery/MapServer/tile/{z}/{y}/{x}')
TILE_TIANDITU = ('https://t{s}.tianditu.gov.cn/DataServer?'
                 'T=img_w&x={x}&y={y}&l={z}&tk={ak}')
_CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      '..', 'res', 'sat_cache')

UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) TrackMap/1.0'


def _tianditu_ak():
    try:
        from config import settings
        return getattr(settings, 'TIANDITU_AK', '') or ''
    except Exception:
        return ''


def _tile_url(z, x, y):
    ak = _tianditu_ak()
    if ak:
        return TILE_TIANDITU.format(z=z, x=x, y=y, s=x % 8, ak=ak)
    return TILE_ARCGIS.format(z=z, x=x, y=y)


def _cache_path(z, x, y):
    return os.path.join(_CACHE, str(z), f'{z}_{x}_{y}.jpg')


# ---------- Web 墨卡托 ----------

def lng_to_xf(lng):
    return (lng + 180.0) / 360.0


def lat_to_yf(lat):
    lat = max(-85.0, min(85.0, lat))
    return (1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2


class _TileWorker:
    """单线程瓦片下载（磁盘缓存），完成后信号回主线程"""

    def __init__(self, widget):
        self.widget = widget
        self.queue = queue.Queue()
        self.queued = set()
        threading.Thread(target=self._loop, daemon=True).start()

    def request(self, z, x, y):
        key = (z, x, y)
        if key in self.queued:
            return
        self.queued.add(key)
        self.queue.put(key)

    def _loop(self):
        while True:
            try:
                key = self.queue.get(timeout=2)
            except queue.Empty:
                continue
            z, x, y = key
            self.queued.discard(key)   # 取出后允许失败重试
            path = _cache_path(z, x, y)
            if not os.path.exists(path):
                try:
                    r = requests.get(_tile_url(z, x, y), timeout=10,
                                     headers={'User-Agent': UA})
                    if r.status_code == 200 and len(r.content) > 100:
                        os.makedirs(os.path.dirname(path), exist_ok=True)
                        with open(path, 'wb') as f:
                            f.write(r.content)
                except Exception:
                    pass    # 失败静默，重绘时会重试
            self.widget.tile_ready.emit(key)   # 无论成败都回一次


class SatMapWidget(QWidget):
    """卫星瓦片地图（在线）+ GPS 轨迹绘制"""

    tile_ready = pyqtSignal(tuple)

    MIN_Z, MAX_Z = 3, 18
    TILE = 256

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMouseTracking(True)
        self.setMinimumSize(320, 240)
        self.setStyleSheet('background:#1a1a2e;')

        self.zoom = 15
        self.cx = self.cy = 0.0        # 世界像素坐标（当前 zoom）
        self._points = []              # [(lat, lng, time_str)]
        self._mode = 'dot'             # dot=散点 / line=折线
        self._pixmaps = {}             # (z,x,y) -> QPixmap
        self._drag_last = None
        self._worker = _TileWorker(self)
        self.tile_ready.connect(self._on_tile_ready)
        self._font = QFont('Microsoft YaHei', 9)

    # ---------- 公开接口 ----------

    def set_track(self, points, mode='dot'):
        """points: [(lat, lng, time_str)]；mode='dot'散点 / 'line'折线
        自动缩放到范围"""
        self._points = list(points or [])
        self._mode = mode if mode in ('dot', 'line') else 'dot'
        if self._points:
            self._fit_track()
        self.update()

    def clear_track(self):
        self._points = []
        self.update()

    # ---------- 坐标换算 ----------

    def _world(self):
        return self.TILE * (2 ** self.zoom)

    def _to_screen(self, lat, lng):
        left, top = self.cx - self.width() / 2, self.cy - self.height() / 2
        return (lng_to_xf(lng) * self._world() - left,
                lat_to_yf(lat) * self._world() - top)

    def _fit_track(self):
        """缩放并居中到全部轨迹点"""
        xs = [lng_to_xf(p[1]) for p in self._points]
        ys = [lat_to_yf(p[0]) for p in self._points]
        x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
        bw, bh = x1 - x0, y1 - y0
        w = max(self.width() - 120, 120)
        h = max(self.height() - 120, 120)
        z = self.MIN_Z
        for cand in range(self.MAX_Z, self.MIN_Z - 1, -1):
            span = 256 * 2 ** cand
            if bw * span <= w and bh * span <= h:
                z = cand
                break
        self.zoom = min(z, 16)
        self.cx = (x0 + x1) / 2 * self._world()
        self.cy = (y0 + y1) / 2 * self._world()

    # ---------- 瓦片 ----------

    def _on_tile_ready(self, key):
        z, x, y = key
        path = _cache_path(z, x, y)
        if os.path.exists(path):
            pm = QPixmap(path)
            if not pm.isNull():
                self._pixmaps[key] = pm
        self.update()

    def _request_visible(self):
        left, top = self.cx - self.width() / 2, self.cy - self.height() / 2
        tx0, ty0 = max(0, int(left // self.TILE)), max(0, int(top // self.TILE))
        tx1 = int((left + self.width()) // self.TILE)
        ty1 = int((top + self.height()) // self.TILE)
        n = 2 ** self.zoom
        for tx in range(tx0, tx1 + 1):
            for ty in range(ty0, min(ty1 + 1, n)):
                key = (self.zoom, tx, ty)
                if key in self._pixmaps:
                    continue
                # 磁盘缓存命中：主线程直接读，立即显示（不走下载线程）
                path = _cache_path(*key)
                if os.path.exists(path):
                    pm = QPixmap(path)
                    if not pm.isNull():
                        self._pixmaps[key] = pm
                        continue
                self._worker.request(*key)

    # ---------- 绘制 ----------

    def paintEvent(self, ev):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), QColor('#1a1a2e'))
        self._request_visible()

        left, top = self.cx - self.width() / 2, self.cy - self.height() / 2
        n = 2 ** self.zoom

        # ---- 底图瓦片 ----
        tx0, ty0 = int(left // self.TILE), int(top // self.TILE)
        tx1 = int((left + self.width()) // self.TILE)
        ty1 = int((top + self.height()) // self.TILE)
        for tx in range(tx0, tx1 + 1):
            for ty in range(ty0, ty1 + 1):
                if tx < 0 or tx >= n or ty < 0 or ty >= n:
                    continue
                sx, sy = tx * self.TILE - left, ty * self.TILE - top
                pm = self._pixmaps.get((self.zoom, tx, ty))
                if pm:
                    p.drawPixmap(QRectF(sx, sy, self.TILE, self.TILE), pm,
                                 QRectF(0, 0, self.TILE, self.TILE))
                else:
                    p.fillRect(QRectF(sx, sy, self.TILE, self.TILE),
                               QColor('#232338'))

        # ---- 轨迹/散点 ----
        if self._mode == 'line' and len(self._points) >= 2:
            path = QPainterPath()
            started = False
            for lat, lng, _t in self._points:
                px, py = self._to_screen(lat, lng)
                if not (-self.TILE <= px <= self.width() + self.TILE and
                        -self.TILE <= py <= self.height() + self.TILE):
                    started = False     # 离屏断线，防止长直线穿屏
                    continue
                if not started:
                    path.moveTo(px, py)
                    started = True
                else:
                    path.lineTo(px, py)
            p.setPen(QPen(QColor(30, 200, 100), 3))
            p.drawPath(path)

        # ---- 所有点统一图标（散点模式）：实心+白色描边（原终点样式）----
        if self._mode == 'dot':
            p.setPen(QPen(Qt.GlobalColor.white, 2))
            p.setBrush(QBrush(QColor(30, 200, 100)))
            for lat, lng, _t in self._points:
                px, py = self._to_screen(lat, lng)
                if -10 <= px <= self.width() + 10 and \
                        -10 <= py <= self.height() + 10:
                    p.drawEllipse(QPointF(px, py), 5, 5)

        # ---- 角标 ----
        p.setFont(self._font)
        p.setPen(QColor(255, 255, 255, 160))
        p.drawText(8, self.height() - 8,
                   f'Esri World Imagery · zoom {self.zoom} · '
                   f'{len(self._points)} 点')

    # ---------- 交互 ----------

    def mousePressEvent(self, ev):
        if ev.button() == Qt.MouseButton.LeftButton:
            self._drag_last = ev.position()

    def mouseMoveEvent(self, ev):
        if self._drag_last is not None:
            d = ev.position() - self._drag_last
            self._drag_last = ev.position()
            self.cx = max(0, min(self._world(), self.cx - d.x()))
            self.cy = max(0, min(self._world(), self.cy - d.y()))
            self.update()

    def mouseReleaseEvent(self, ev):
        self._drag_last = None

    def wheelEvent(self, ev):
        delta = ev.angleDelta().y()
        if not delta:
            return
        # 以光标为锚缩放：保持光标下的地理点不动
        pos = ev.position()
        left, top = self.cx - self.width() / 2, self.cy - self.height() / 2
        wx, wy = left + pos.x(), top + pos.y()
        old_world = self._world()
        new_z = max(self.MIN_Z, min(self.MAX_Z,
                                    self.zoom + (1 if delta > 0 else -1)))
        if new_z == self.zoom:
            return
        self.zoom = new_z
        scale = self._world() / old_world
        # 锚定推导：sx = wx*scale - (cx' - W/2) ⇒ cx' = wx*scale - sx + W/2
        self.cx = wx * scale - pos.x() + self.width() / 2
        self.cy = wy * scale - pos.y() + self.height() / 2
        self.update()

    def resizeEvent(self, ev):
        super().resizeEvent(ev)
        if self._points:
            self._fit_track()   # 尺寸变化重新适配轨迹
        self.update()
