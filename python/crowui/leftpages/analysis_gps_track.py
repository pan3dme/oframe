# -*- coding: utf-8 -*-
"""分析技能：设备定位分布——选设备 + 时间范围，从本机缓存取全部定位坐标，
在 3D 地图上画出轨迹折线并飞行定位（数据全部来自本机 SQLite，不请求云端）"""

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel,
                             QPushButton, QComboBox, QFrame)
from PyQt6.QtCore import Qt, pyqtSignal

from crowui import theme
from crowui.ots_sync import DeviceLogSyncer

_RANGE_OPTIONS = [('全部', None), ('近30天', 30), ('近7天', 7), ('近24小时', 1)]


class GpsTrackDialog(QDialog):
    """设备定位分布配置窗（独立窗口）"""

    # 在地图上画轨迹（坐标列表） / 清除轨迹
    show_track_requested = pyqtSignal(list)
    clear_track_requested = pyqtSignal()

    def __init__(self, api, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[定位分布]', msg))
        self._syncer = DeviceLogSyncer(api)
        self._devices = []
        self.setWindowTitle('分析技能 · 设备定位分布')
        self.setMinimumWidth(430)
        self._setup_ui()
        self._load_devices()

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(18, 16, 18, 14)
        lay.setSpacing(10)

        title = QLabel('📍 设备定位分布')
        title.setStyleSheet('font-size:15px; font-weight:600; color:#333;')
        lay.addWidget(title)
        tip = QLabel('把该设备在本机缓存中的定位/跟踪记录显示到卫星地图'
                     '（绿色散点），纯本地查询。')
        tip.setStyleSheet('color:#888; font-size:11px;')
        lay.addWidget(tip)

        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        line.setStyleSheet('color:#eee;')
        lay.addWidget(line)

        row = QHBoxLayout()
        row.addWidget(QLabel('设备:'))
        self.device_combo = QComboBox()
        self.device_combo.currentIndexChanged.connect(self._update_stats)
        row.addWidget(self.device_combo, stretch=1)
        lay.addLayout(row)

        row2 = QHBoxLayout()
        row2.addWidget(QLabel('范围:'))
        self.range_combo = QComboBox()
        for label, _d in _RANGE_OPTIONS:
            self.range_combo.addItem(label)
        self.range_combo.currentIndexChanged.connect(self._update_stats)
        row2.addWidget(self.range_combo)
        row2.addStretch()
        lay.addLayout(row2)

        self.stats_label = QLabel('-')
        self.stats_label.setStyleSheet('color:#555; font-size:12px;')
        lay.addWidget(self.stats_label)

        lay.addStretch()
        btn_row = QHBoxLayout()
        clear_btn = QPushButton('清除地图轨迹')
        clear_btn.setFixedSize(110, 34)
        clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        clear_btn.setStyleSheet(theme.GRAY_BUTTON_QSS)
        clear_btn.clicked.connect(self._on_clear)
        btn_row.addWidget(clear_btn)
        btn_row.addStretch()
        self.show_btn = QPushButton('在地图中显示')
        self.show_btn.setFixedSize(120, 34)
        self.show_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.show_btn.setStyleSheet(theme.GREEN_BUTTON_QSS)
        self.show_btn.clicked.connect(self._on_show)
        btn_row.addWidget(self.show_btn)
        close_btn = QPushButton('关闭')
        close_btn.setFixedSize(70, 34)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet(theme.GRAY_BUTTON_QSS)
        close_btn.clicked.connect(self.close)
        btn_row.addWidget(close_btn)
        lay.addLayout(btn_row)

    # ---------- 数据 ----------

    def _load_devices(self):
        self._devices = self._syncer.gps_devices()
        self.device_combo.blockSignals(True)
        self.device_combo.clear()
        if not self._devices:
            self.device_combo.addItem('（本机缓存无定位数据，请先同步）', None)
            self.show_btn.setEnabled(False)
        else:
            for did, cnt in self._devices:
                self.device_combo.addItem(f'{did}（{cnt:,} 条上报）', did)
            self.show_btn.setEnabled(True)
        self.device_combo.blockSignals(False)
        self._update_stats()

    def _cur_device(self):
        return self.device_combo.currentData()

    def _cur_days(self):
        return _RANGE_OPTIONS[self.range_combo.currentIndex()][1]

    def _update_stats(self, *_):
        did = self._cur_device()
        if not did:
            self.stats_label.setText('请先在"同步数据"里拉取云端日志')
            return
        raw = dict(self._devices).get(did, 0)
        pts = self._syncer.device_gps_points(did, days=self._cur_days())
        if self._cur_days() is None:
            raw_txt = f'有效坐标 {len(pts):,} / 上报 {raw:,} 条'
            if raw > len(pts):
                raw_txt += f'（{raw - len(pts):,} 条无定位已过滤）'
        else:
            raw_txt = f'该范围内有效坐标 {len(pts):,} 个'
        if not pts:
            self.stats_label.setText(f'{did}：{raw_txt}\n该范围内无有效定位')
        else:
            span = f"{pts[0][2]} ～ {pts[-1][2]}"
            self.stats_label.setText(
                f'{did}：{raw_txt}\n时间跨度：{span}'
                + ('（已抽稀显示）' if len(pts) >= 4000 else ''))

    # ---------- 动作 ----------

    def _on_show(self):
        did = self._cur_device()
        if not did:
            return
        pts = self._syncer.device_gps_points(did, days=self._cur_days())
        if not pts:
            self.notify('该范围内无定位记录')
            return
        self.show_track_requested.emit(pts)   # [(lat, lng, time_str)]
        self.notify(f'{did} 轨迹已显示：{len(pts):,} 点')

    def _on_clear(self):
        self.clear_track_requested.emit()
        self.notify('地图轨迹已清除')
