# -*- coding: utf-8 -*-
"""牛羊管理页（对标小程序 pages/livestock + pages/detail）
列表 / 新增 / 编辑 / 绑定设备（绑定入口在设备页，这里显示绑定关系）
"""

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                             QTableWidget, QTableWidgetItem, QHeaderView, QInputDialog,
                             QMessageBox)
from PyQt6.QtCore import Qt

from crowui import theme
from crowui.wx_api import WxApi


class LivestockPage(QWidget):
    def __init__(self, api: WxApi, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[牛羊]', msg))
        self._rows = []
        self._device_rows = []
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
        add_btn = QPushButton("新增牛羊")
        add_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        add_btn.clicked.connect(self._add)
        bind_btn = QPushButton("绑定设备")
        bind_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        bind_btn.clicked.connect(self._bind)
        for b in (refresh_btn, add_btn, bind_btn):
            top.addWidget(b)
        top.addStretch()
        layout.addLayout(top)

        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(["牛羊ID", "名称", "性别", "生日", "绑定设备"])
        self.table.setStyleSheet(theme.TABLE_QSS)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.verticalHeader().setVisible(False)
        header = self.table.horizontalHeader()
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        for i in (2, 3, 4):
            header.setSectionResizeMode(i, QHeaderView.ResizeMode.ResizeToContents)
        self.table.doubleClicked.connect(self._edit)
        layout.addWidget(self.table, stretch=1)

        tip = QLabel("双击行编辑牛羊信息（名称/生日/性别/头像URL）")
        tip.setFont(theme.font(9))
        tip.setStyleSheet(f"color: {theme.TEXT_MUTED};")
        layout.addWidget(tip)

    def refresh(self):
        if not self.api.logged_in:
            self.notify("请先登录")
            return
        def load():
            livestock = self.api.getLivestockList()
            devices = self.api.getDeviceList()
            return livestock, devices
        self.api.run_async(load, self._on_loaded, lambda m: self.notify(f"加载失败: {m}"))

    def _on_loaded(self, result):
        self._rows, self._device_rows = result
        device_bind = {}
        for d in self._device_rows:
            cow = str(d.get('link_cowsheep_id', '')).strip()
            if cow:
                device_bind.setdefault(cow, []).append(str(d.get('deviceId', '')))
        gender_names = {'1': '公', '2': '母'}
        self.table.setRowCount(len(self._rows))
        for i, row in enumerate(self._rows):
            cow_id = str(row.get('cowsheep_id', ''))
            gender = str(row.get('gender', ''))
            bound = ', '.join(device_bind.get(cow_id, [])) or '未绑定'
            values = [cow_id, str(row.get('rename', '')),
                      gender_names.get(gender, gender), str(row.get('birthday', '')), bound]
            for col, text in enumerate(values):
                item = QTableWidgetItem(text)
                item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                self.table.setItem(i, col, item)

    def _add(self):
        if not self.api.logged_in:
            self.notify("请先登录")
            return
        cow_id, ok = QInputDialog.getText(self, "新增牛羊", "牛羊ID:")
        if not ok or not cow_id.strip():
            return
        birthday, ok2 = QInputDialog.getText(self, "新增牛羊", "生日 (YYYY-MM-DD，可空):")
        gender = '1' if QMessageBox.question(self, "性别", "是公的吗？") == QMessageBox.StandardButton.Yes else '2'
        self.api.run_async(
            lambda: self.api.addLivestock(cow_id.strip(), birthday.strip(), gender),
            on_ok=lambda _: (self.notify("新增成功"), self.refresh()),
            on_err=lambda m: self.notify(f"新增失败: {m}"),
        )

    def _edit(self, index):
        row_idx = index.row()
        if row_idx < 0 or row_idx >= len(self._rows):
            return
        row = self._rows[row_idx]
        cow_id = str(row.get('cowsheep_id', ''))
        rename, ok = QInputDialog.getText(self, "编辑牛羊", "名称:", text=str(row.get('rename', '')))
        if not ok:
            return
        birthday, ok2 = QInputDialog.getText(self, "编辑牛羊", "生日:", text=str(row.get('birthday', '')))
        if not ok2:
            return
        avatar, ok3 = QInputDialog.getText(self, "编辑牛羊", "头像URL (可空):", text=str(row.get('avatar', '')))
        if not ok3:
            return
        self.api.run_async(
            lambda: self.api.updateLivestock(cow_id, rename.strip(), birthday.strip(),
                                              str(row.get('gender', '0')), avatar.strip()),
            on_ok=lambda _: (self.notify("保存成功"), self.refresh()),
            on_err=lambda m: self.notify(f"保存失败: {m}"),
        )

    def _bind(self):
        """绑定设备：选牛羊 → 选设备"""
        if not self._rows or not self._device_rows:
            self.notify("数据未加载")
            return
        cows = [str(r.get('cowsheep_id', '')) for r in self._rows]
        cow_id, ok = QInputDialog.getItem(self, "绑定设备", "选择牛羊:", cows, 0, False)
        if not ok:
            return
        devices = [f"{d.get('deviceId', '')}" for d in self._device_rows]
        device_id, ok2 = QInputDialog.getItem(self, "绑定设备", "选择设备:", devices, 0, False)
        if not ok2:
            return
        self.api.run_async(
            lambda: self.api.bindDeviceCow(device_id, cow_id),
            on_ok=lambda _: (self.notify(f"已绑定 {device_id} → {cow_id}"), self.refresh()),
            on_err=lambda m: self.notify(f"绑定失败: {m}"),
        )
