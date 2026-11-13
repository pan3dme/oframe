# -*- coding: utf-8 -*-
"""道路/地名管理页（对标小程序 pages/road + pages/places）
- 道路：列表 / 显示到场景 / 新增（手动输入坐标串，桌面端替代小程序的步行录制）/ 删除
- 地名：列表 / 显示到场景 / 新增 / 删除
"""

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                             QListWidget, QListWidgetItem, QInputDialog, QMessageBox,
                             QTabWidget)
from PyQt6.QtCore import Qt

from crowui import theme, wx_codec
from crowui.wx_api import WxApi


class RoadsPlacesPage(QWidget):
    def __init__(self, api: WxApi, scene, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.scene = scene
        self.notify = notify or (lambda msg: print('[道路地名]', msg))
        self._roads = []
        self._places = []
        self._setup_ui()
        self.refresh()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(8)

        top = QHBoxLayout()
        refresh_btn = QPushButton("刷新")
        refresh_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        refresh_btn.clicked.connect(self.refresh)
        top.addWidget(refresh_btn)
        top.addStretch()
        layout.addLayout(top)

        tabs = QTabWidget()
        tabs.setFont(theme.font(11))
        tabs.addTab(self._build_road_tab(), "道路")
        tabs.addTab(self._build_place_tab(), "地名")
        layout.addWidget(tabs, stretch=1)

    def _build_road_tab(self):
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(4, 4, 4, 4)
        v.setSpacing(6)

        btn_row = QHBoxLayout()
        show_btn = QPushButton("全部显示到场景")
        show_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        show_btn.clicked.connect(self._show_all_roads)
        clear_btn = QPushButton("清除")
        clear_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        clear_btn.clicked.connect(self._clear_roads)
        add_btn = QPushButton("新增道路")
        add_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        add_btn.clicked.connect(self._add_road)
        for b in (show_btn, clear_btn, add_btn):
            btn_row.addWidget(b)
        v.addLayout(btn_row)

        self.road_list = QListWidget()
        self.road_list.setStyleSheet(
            f"QListWidget {{ border: 1px solid {theme.CARD_BORDER}; border-radius: 8px;"
            f" background: {theme.CARD_BG}; font-size: 12px; }}")
        self.road_list.itemDoubleClicked.connect(self._show_one_road)
        v.addWidget(self.road_list, stretch=1)

        tip = QLabel("双击道路显示单条；新增为手动输入坐标点（lat,lng 用分号隔开）")
        tip.setFont(theme.font(9))
        tip.setWordWrap(True)
        tip.setStyleSheet(f"color: {theme.TEXT_MUTED};")
        v.addWidget(tip)
        return w

    def _build_place_tab(self):
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(4, 4, 4, 4)
        v.setSpacing(6)

        btn_row = QHBoxLayout()
        show_btn = QPushButton("全部显示到场景")
        show_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        show_btn.clicked.connect(self._show_all_places)
        clear_btn = QPushButton("清除")
        clear_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        clear_btn.clicked.connect(self._clear_places)
        add_btn = QPushButton("新增地名")
        add_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        add_btn.clicked.connect(self._add_place)
        for b in (show_btn, clear_btn, add_btn):
            btn_row.addWidget(b)
        v.addLayout(btn_row)

        self.place_list = QListWidget()
        self.place_list.setStyleSheet(
            f"QListWidget {{ border: 1px solid {theme.CARD_BORDER}; border-radius: 8px;"
            f" background: {theme.CARD_BG}; font-size: 12px; }}")
        self.place_list.itemDoubleClicked.connect(self._show_one_place)
        v.addWidget(self.place_list, stretch=1)

        tip = QLabel("双击地名定位到场景；删除按钮见下方")
        tip.setFont(theme.font(9))
        tip.setStyleSheet(f"color: {theme.TEXT_MUTED};")
        v.addWidget(tip)

        del_row = QHBoxLayout()
        del_road_btn = QPushButton("删除选中道路")
        del_road_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        del_road_btn.clicked.connect(self._delete_road)
        del_place_btn = QPushButton("删除选中地名")
        del_place_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        del_place_btn.clicked.connect(self._delete_place)
        # 删除按钮放道路tab里需要道路列表引用，这里放地名tab只删地名；道路删除在道路tab
        del_row.addWidget(del_place_btn)
        del_row.addStretch()
        v.addLayout(del_row)
        return w

    # ==================== 数据 ====================

    def refresh(self):
        if not self.api.logged_in:
            self.notify("请先登录")
            return
        def load():
            return self.api.getRoadListFromCache(), self.api.getPlaceListFromCache()
        self.api.run_async(load, self._on_loaded, lambda m: self.notify(f"加载失败: {m}"))

    def _on_loaded(self, result):
        self._roads, self._places = result
        self.road_list.clear()
        for row in self._roads:
            points = wx_codec.parse_road_points(row.get('roadinfo', ''))
            item = QListWidgetItem(
                f"{row.get('roadname', '')} ({row.get('route_id', '')}) - {len(points)}点")
            item.setData(Qt.ItemDataRole.UserRole, row)
            self.road_list.addItem(item)
        self.place_list.clear()
        for row in self._places:
            item = QListWidgetItem(
                f"{row.get('name', '')} - {row.get('gps', '')} ({row.get('placeid', '')})")
            item.setData(Qt.ItemDataRole.UserRole, row)
            self.place_list.addItem(item)

    # ==================== 道路 ====================

    def _show_one_road(self, item):
        row = item.data(Qt.ItemDataRole.UserRole)
        points = wx_codec.parse_road_points(row.get('roadinfo', ''))
        if self.scene and points:
            self.scene.receive_load_to_scene(points)
        self.notify(f"已显示道路: {row.get('roadname', '')} ({len(points)}点)")

    def _show_all_roads(self):
        if not self.scene:
            return
        self.scene.clear_all_load_line()
        count = 0
        for row in self._roads:
            points = wx_codec.parse_road_points(row.get('roadinfo', ''))
            if points:
                self.scene.receive_load_to_scene(points)
                count += 1
        self.notify(f"已显示 {count} 条道路")

    def _clear_roads(self):
        if self.scene:
            self.scene.clear_all_load_line()

    def _add_road(self):
        if not self.api.logged_in:
            self.notify("请先登录")
            return
        name, ok = QInputDialog.getText(self, "新增道路", "道路名称:")
        if not ok or not name.strip():
            return
        text, ok2 = QInputDialog.getMultiLineText(
            self, "新增道路", "坐标点（每行一个 lat,lng，至少2个点）:",
            text="26.525115,109.394106\n26.530000,109.400000")
        if not ok2:
            return
        coords = []
        for line in text.strip().splitlines():
            parts = line.strip().split(',')
            if len(parts) < 2:
                continue
            try:
                coords.append((float(parts[0]), float(parts[1])))
            except ValueError:
                continue
        if len(coords) < 2:
            QMessageBox.warning(self, "格式错误", "至少需要 2 个有效坐标点")
            return
        roadinfo = wx_codec.encode_road_points(coords)
        self.api.run_async(
            lambda: self.api.addRoad(name.strip(), roadinfo),
            on_ok=lambda _: (self.notify("道路已保存"), self.refresh()),
            on_err=lambda m: self.notify(f"保存失败: {m}"),
        )

    def _delete_road(self):
        item = self.road_list.currentItem()
        if not item:
            self.notify("请先选中一条道路")
            return
        row = item.data(Qt.ItemDataRole.UserRole)
        if QMessageBox.question(self, "删除道路",
                                f"确认删除 {row.get('roadname', '')}?") != QMessageBox.StandardButton.Yes:
            return
        self.api.run_async(
            lambda: self.api.deleteRoad(row.get('route_id', '')),
            on_ok=lambda _: (self.notify("已删除"), self.refresh()),
            on_err=lambda m: self.notify(f"删除失败: {m}"),
        )

    # ==================== 地名 ====================

    def _show_one_place(self, item):
        row = item.data(Qt.ItemDataRole.UserRole)
        gps = str(row.get('gps', '')).split(',')
        if len(gps) < 2:
            return
        try:
            lat, lng = float(gps[0]), float(gps[1])
        except ValueError:
            return
        if self.scene:
            self.scene.receive_place_to_scene3d((lat, lng), str(row.get('name', '')))
            self.scene.receive_gps_coordinates(lat, lng)

    def _show_all_places(self):
        if not self.scene:
            return
        self.scene.clear_all_place()
        for row in self._places:
            gps = str(row.get('gps', '')).split(',')
            if len(gps) < 2:
                continue
            try:
                self.scene.receive_place_to_scene3d((float(gps[0]), float(gps[1])),
                                                    str(row.get('name', '')))
            except ValueError:
                continue
        self.notify(f"已显示 {len(self._places)} 个地名")

    def _clear_places(self):
        if self.scene:
            self.scene.clear_all_place()

    def _add_place(self):
        if not self.api.logged_in:
            self.notify("请先登录")
            return
        name, ok = QInputDialog.getText(self, "新增地名", "地名:")
        if not ok or not name.strip():
            return
        gps, ok2 = QInputDialog.getText(self, "新增地名", "坐标 lat,lng:")
        if not ok2 or ',' not in gps:
            self.notify("坐标格式: lat,lng")
            return
        self.api.run_async(
            lambda: self.api.addPlace(name.strip(), gps.strip()),
            on_ok=lambda _: (self.notify("地名已保存"), self.refresh()),
            on_err=lambda m: self.notify(f"保存失败: {m}"),
        )

    def _delete_place(self):
        item = self.place_list.currentItem()
        if not item:
            self.notify("请先选中一个地名")
            return
        row = item.data(Qt.ItemDataRole.UserRole)
        if QMessageBox.question(self, "删除地名",
                                f"确认删除 {row.get('name', '')}?") != QMessageBox.StandardButton.Yes:
            return
        self.api.run_async(
            lambda: self.api.deletePlace(row.get('placeid', '')),
            on_ok=lambda _: (self.notify("已删除"), self.refresh()),
            on_err=lambda m: self.notify(f"删除失败: {m}"),
        )
