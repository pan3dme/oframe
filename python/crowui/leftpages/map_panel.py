# -*- coding: utf-8 -*-
"""地图中心面板：把小程序 pages/map/map.js 的控制功能接到 3D 场景上
- 刷新所有设备点（visible 且无 ProductKey 的普通设备）
- 道路/地名图层开关
- 下一台（循环聚焦设备）
- 全体中继 GPS 指令（gpstm 广播）
"""

from datetime import datetime

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                             QComboBox, QCheckBox)
from PyQt6.QtCore import Qt

from crowui import theme
from crowui.wx_api import WxApi

# 1小时内GPS=新鲜；超1小时但1小时内有对时=次新鲜；其余离线
FRESH_SEC = 3600


class MapPanel(QWidget):
    def __init__(self, api: WxApi, scene, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.scene = scene
        self.notify = notify or (lambda msg: print('[地图]', msg))
        self._gps_list = []       # device_gps 表数据
        self._device_rows = []
        self._sync_map = {}
        self._next_index = -1
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(10)

        title = QLabel("地图中心")
        title.setFont(theme.font(13, bold=True))
        title.setStyleSheet(f"color: {theme.TEXT_MAIN};")
        layout.addWidget(title)

        # 设备点控制
        row1 = QHBoxLayout()
        refresh_btn = QPushButton("刷新设备点")
        refresh_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        refresh_btn.clicked.connect(self.refresh_markers)
        clear_btn = QPushButton("清除设备点")
        clear_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        clear_btn.clicked.connect(self._clear_devices)
        next_btn = QPushButton("下一台")
        next_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        next_btn.clicked.connect(self._next_device)
        for b in (refresh_btn, clear_btn, next_btn):
            row1.addWidget(b)
        layout.addLayout(row1)

        # 图层
        layer_row = QHBoxLayout()
        self.road_check = QCheckBox("道路图层")
        self.road_check.setFont(theme.font(10))
        self.road_check.stateChanged.connect(self._toggle_roads)
        self.place_check = QCheckBox("地名图层")
        self.place_check.setFont(theme.font(10))
        self.place_check.stateChanged.connect(self._toggle_places)
        self.google3d_check = QCheckBox("Google 3D 地图")
        self.google3d_check.setFont(theme.font(10))
        self.google3d_check.stateChanged.connect(self._toggle_google3d)
        layer_row.addWidget(self.road_check)
        layer_row.addWidget(self.place_check)
        layer_row.addWidget(self.google3d_check)
        layer_row.addStretch()
        layout.addLayout(layer_row)

        # 定位到设备
        focus_row = QHBoxLayout()
        self.focus_combo = QComboBox()
        self.focus_combo.setStyleSheet(
            f"QComboBox {{ border: 1px solid {theme.CARD_BORDER}; border-radius: 6px; padding: 4px 8px; }}")
        focus_btn = QPushButton("定位到设备")
        focus_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        focus_btn.clicked.connect(self._focus_selected)
        focus_row.addWidget(self.focus_combo, stretch=1)
        focus_row.addWidget(focus_btn)
        layout.addLayout(focus_row)

        # 中继广播
        relay_title = QLabel("中继广播")
        relay_title.setFont(theme.font(12, bold=True))
        relay_title.setStyleSheet(f"color: {theme.TEXT_MAIN};")
        layout.addWidget(relay_title)

        gpstm_btn = QPushButton("全体中继 GPS 上报 (gpstm)")
        gpstm_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        gpstm_btn.clicked.connect(self._broadcast_gpstm)
        layout.addWidget(gpstm_btn)

        self.status_label = QLabel("设备点: 未加载")
        self.status_label.setFont(theme.font(10))
        self.status_label.setStyleSheet(f"color: {theme.TEXT_SECONDARY};")
        self.status_label.setWordWrap(True)
        layout.addWidget(self.status_label)
        layout.addStretch()

    # ==================== 数据 ====================

    def refresh_markers(self):
        """拉取设备+GPS+对时数据并渲染设备点（普通设备 = 无 ProductKey）"""
        if not self.api.logged_in:
            self.notify("请先登录")
            return
        def load():
            devices = [d for d in self.api.getDeviceList()
                       if not d.get('ProductKey') and str(d.get('visible', '1')) == '1']
            gps_list = self.api.getDeviceGpsAll()
            sync = self.api.getDeviceSyncAll()
            return devices, gps_list, sync
        self.api.run_async(load, self._on_loaded, self._on_error)

    def _on_loaded(self, result):
        self._device_rows, self._gps_list, self._sync_map = result
        self._render_devices()
        self._fill_focus_combo()

    def _on_error(self, msg):
        self.notify(f"加载失败: {msg}")

    def _render_devices(self):
        if not self.scene:
            return
        self.scene.clear_all_device()
        count = 0
        now_ts = datetime.now().timestamp()
        for device in self._device_rows:
            device_id = str(device.get('deviceId', ''))
            gps_row = next((g for g in self._gps_list if g['deviceId'] == device_id), None)
            if not gps_row:
                continue
            sync = self._sync_map.get(device_id) or {}
            sync_ts = sync.get('ts', 0)
            fresh = (now_ts - gps_row.get('ts', 0)) < FRESH_SEC
            semi_fresh = (now_ts - sync_ts) < FRESH_SEC
            # 状态标记进名称，3D 场景的球颜色由 sprite 决定
            tag = '' if fresh else ('(旧)' if semi_fresh else '(离线)')
            rename = device.get('rename', '')
            label = f"{device_id}{tag}" + (f" {rename}" if rename else '')
            self.scene.receive_device_to_scene3d(device_id, f"{gps_row['lat']},{gps_row['lng']}", label)
            count += 1
        self.status_label.setText(f"设备点: 共 {count} 台（绿=在线 旧/离线 见标签）")

    def _fill_focus_combo(self):
        self.focus_combo.clear()
        for device in self._device_rows:
            device_id = str(device.get('deviceId', ''))
            rename = device.get('rename', '')
            self.focus_combo.addItem(f"{device_id} ({rename})" if rename else device_id, device_id)

    def _clear_devices(self):
        if self.scene:
            self.scene.clear_all_device()
        self.status_label.setText("设备点: 已清除")

    def _next_device(self):
        """下一台：循环聚焦有坐标的设备"""
        ids = [self.focus_combo.itemData(i) for i in range(self.focus_combo.count())]
        if not ids:
            self.refresh_markers()
            return
        self._next_index = (self._next_index + 1) % len(ids)
        self.focus_combo.setCurrentIndex(self._next_index)
        self._focus_device(ids[self._next_index])

    def _focus_selected(self):
        device_id = self.focus_combo.currentData()
        if device_id:
            self._focus_device(device_id)

    def _focus_device(self, device_id):
        gps_row = next((g for g in self._gps_list if g['deviceId'] == device_id), None)
        if not gps_row:
            self.notify(f"{device_id} 没有坐标数据")
            return
        if self.scene:
            self.scene.receive_gps_coordinates(gps_row['lat'], gps_row['lng'])
        self.notify(f"已定位到 {device_id}: {gps_row['lat']},{gps_row['lng']}")

    # ==================== 图层 ====================

    def _toggle_roads(self, state):
        if not self.scene:
            return
        if state == Qt.CheckState.Checked.value:
            self.api.run_async(
                lambda: self.api.getRoadListFromCache(),
                on_ok=self._on_roads,
                on_err=lambda m: self.notify(f"道路加载失败: {m}"),
            )
        else:
            self.scene.clear_all_load_line()

    def _on_roads(self, rows):
        from crowui import wx_codec
        self.scene.clear_all_load_line()
        count = 0
        for row in rows:
            points = wx_codec.parse_road_points(row.get('roadinfo', ''))
            if points:
                self.scene.receive_load_to_scene(points)
                count += 1
        self.notify(f"道路图层: 已显示 {count} 条")

    def _toggle_places(self, state):
        if not self.scene:
            return
        if state == Qt.CheckState.Checked.value:
            self.api.run_async(
                lambda: self.api.getPlaceListFromCache(),
                on_ok=self._on_places,
                on_err=lambda m: self.notify(f"地名加载失败: {m}"),
            )
        else:
            self.scene.clear_all_place()

    def _on_places(self, rows):
        self.scene.clear_all_place()
        count = 0
        for row in rows:
            gps = str(row.get('gps', ''))
            parts = gps.split(',')
            if len(parts) < 2:
                continue
            try:
                lat, lng = float(parts[0]), float(parts[1])
            except ValueError:
                continue
            self.scene.receive_place_to_scene3d((lat, lng), str(row.get('name', '')))
            count += 1
        self.notify(f"地名图层: 已显示 {count} 个")

    # ==================== Google 3D 瓦片 ====================

    def _toggle_google3d(self, state):
        if not self.scene:
            return
        if state == Qt.CheckState.Checked.value:
            self._load_google3d()
        else:
            self.scene.clear_google_tiles()

    def _load_google3d(self):
        """以当前选中设备（或农场中心）为圆心拉取摄影测量 3D 瓦片"""
        from config import settings
        api_key = getattr(settings, 'GOOGLE_MAPS_API_KEY', '')
        if not api_key:
            self.notify('未配置 GOOGLE_MAPS_API_KEY（config.py），无法加载 Google 3D 瓦片')
            self.google3d_check.setChecked(False)
            return
        device_id = self.focus_combo.currentData()
        if device_id:
            gps_row = next((g for g in self._gps_list
                            if g['deviceId'] == device_id), None)
            lat, lng = (gps_row['lat'], gps_row['lng']) if gps_row else settings.centenGps
        else:
            lat, lng = settings.centenGps
        self.notify(f'Google 3D 瓦片下载中...（中心 {lat:.5f},{lng:.5f}）')

        def load():
            from display3d.google3d.tiles_api import GoogleTilesSession
            session = GoogleTilesSession(api_key)
            tiles = session.select_tiles(
                lat, lng, settings.GOOGLE_3D_RADIUS_DEG,
                max_tiles=settings.GOOGLE_3D_MAX_TILES,
                max_geometric_error=settings.GOOGLE_3D_GE_ERROR)
            items = []
            for uri, transform in tiles:
                items.append((session.download_glb(uri), transform))
            return items

        def on_ok(items):
            count = self.scene.receive_google_tiles(items, lat, lng)
            self.notify(f'Google 3D 瓦片: 已加载 {count} 个图元（{len(items)} 瓦片）')

        self.api.run_async(load, on_ok, self._on_error)

    # ==================== 中继广播 ====================

    def _broadcast_gpstm(self):
        """对处于开机时间窗内的中继并发 gpstm 指令（对标小程序 onGpstimAllTap）"""
        if not self.api.logged_in:
            self.notify("请先登录")
            return
        def load():
            devices = self.api.getDeviceList()
            configs = self.api.getDeviceConfigAll()
            return devices, configs
        def on_ok(result):
            devices, configs = result
            now = datetime.now()
            sent = 0
            for device in devices:
                device_id = str(device.get('deviceId', ''))
                if not (device.get('ProductKey') and device.get('DeviceName')):
                    continue  # 只发中继（带三元组）
                cfg = configs.get(device_id)
                if not cfg or not cfg.get('powerWin'):
                    continue
                win = cfg['powerWin']
                current = now.hour * 60 + now.minute
                start = win['start'] * 60
                end = 23 * 60 + 59 if win['end'] == 23 else win['end'] * 60
                if not (start <= current < end):
                    continue  # 不在工作时间窗内的中继跳过
                cmd = {'cmd': 'gpstm', 'value': 30, 'deviceId': device_id}
                self.api.run_async(
                    lambda d=device, c=cmd: self.api.sendtodtucmd(
                        d['DeviceName'], d['ProductKey'], c),
                    on_ok=lambda _, d=d: self.notify(f"gpstm 已发 {d.get('deviceId', '')}"),
                    on_err=lambda m: self.notify(f"gpstm 失败: {m}"),
                )
                sent += 1
            self.notify(f"gpstm 广播: 已发送 {sent} 台中继")
        self.api.run_async(load, on_ok, self._on_error)
