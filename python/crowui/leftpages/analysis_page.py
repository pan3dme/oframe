# -*- coding: utf-8 -*-
"""数据分析页（左侧页签）：顶部缓存概览 + 分析技能卡片区。
新技能 = SKILLS 加一项 + _open_skill 加分支，卡片网格自动排布"""

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                             QPushButton, QFrame, QGridLayout, QSizePolicy)
from PyQt6.QtCore import Qt, pyqtSignal

from crowui import theme
from crowui.ots_sync import DeviceLogSyncer

# 分析技能定义：(key, 图标, 标题, 说明)
SKILLS = [
    ('gps_track', '📍', '设备定位分布',
     '把指定设备的全部定位/跟踪坐标画到 3D 地图'),
    # 后续技能往这里加
]


class AnalysisPage(QWidget):
    """左侧"数据分析"页签：技能入口聚合 + 页内地图视图（返回可回技能页）"""

    def __init__(self, api, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[数据分析]', msg))
        self._syncer = DeviceLogSyncer(api)
        self._track_dialog = None
        self._setup_ui()
        self.refresh_stats()

    def _setup_ui(self):
        from PyQt6.QtWidgets import QStackedWidget
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        self.stack = QStackedWidget()
        outer.addWidget(self.stack, stretch=1)
        self.stack.addWidget(self._build_skills_page())     # 0 技能列表
        self.stack.addWidget(self._build_map_page())        # 1 卫星地图

    # ==================== 页 0：技能列表 ====================

    def _build_skills_page(self):
        page = QWidget()
        page.setStyleSheet('background: #f5f5f5;')
        lay = QVBoxLayout(page)
        lay.setContentsMargins(16, 16, 16, 16)
        lay.setSpacing(10)

        # ---- 标题行 ----
        head = QHBoxLayout()
        title = QLabel('📊 数据分析')
        title.setStyleSheet('font-size:16px; font-weight:600; color:#333;'
                            'background:transparent;')
        head.addWidget(title)
        head.addStretch()
        refresh_btn = QPushButton('刷新')
        refresh_btn.setFixedSize(64, 26)
        refresh_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        refresh_btn.setStyleSheet(theme.GREEN_BUTTON_QSS)
        refresh_btn.clicked.connect(self.refresh_stats)
        head.addWidget(refresh_btn)
        lay.addLayout(head)

        # ---- 本机缓存概览卡片 ----
        card = QFrame()
        card.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        card.setStyleSheet('background:#FFFFFF; border:1px solid #eee;'
                           'border-radius:8px;')
        g = QGridLayout(card)
        g.setContentsMargins(14, 12, 14, 12)
        g.setHorizontalSpacing(24)
        g.setVerticalSpacing(6)
        self.total_label = self._stat_cell(g, 0, 0, '📦 本机缓存记录')
        self.device_label = self._stat_cell(g, 0, 1, '📱 覆盖设备')
        self.last_sync_label = self._stat_cell(g, 0, 2, '⏱ 上次同步')
        lay.addWidget(card)

        # ---- 分析技能卡片区 ----
        sec = QLabel('分析技能')
        sec.setStyleSheet('color:#666; font-size:13px; font-weight:600;'
                          'background:transparent;')
        lay.addWidget(sec)

        grid_holder = QWidget()
        grid_holder.setStyleSheet('background:transparent;')
        grid = QGridLayout(grid_holder)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(10)
        for i, (key, icon, name, desc) in enumerate(SKILLS):
            btn = self._make_skill_btn(key, icon, name, desc)
            grid.addWidget(btn, i // 3, i % 3)
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)
        grid.setColumnStretch(2, 1)
        lay.addWidget(grid_holder)

        lay.addStretch()
        return page

    # ==================== 页 1：卫星地图 ====================

    def _build_map_page(self):
        from crowui.sat_map_widget import SatMapWidget
        page = QWidget()
        page.setStyleSheet('background: #1a1a2e;')
        lay = QVBoxLayout(page)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

        # ---- 顶栏：返回 / 标题 / 统计 ----
        bar = QWidget()
        bar.setStyleSheet('background:#FFFFFF; border-bottom:1px solid #e5e5e5;')
        bar.setFixedHeight(44)
        h = QHBoxLayout(bar)
        h.setContentsMargins(10, 4, 12, 4)
        back = QPushButton('← 返回')
        back.setFixedSize(76, 30)
        back.setCursor(Qt.CursorShape.PointingHandCursor)
        back.setStyleSheet(theme.GRAY_BUTTON_QSS)
        back.clicked.connect(lambda: self.stack.setCurrentIndex(0))
        h.addWidget(back)
        self.map_title = QLabel('设备定位分布')
        self.map_title.setStyleSheet('font-size:14px; font-weight:600;'
                                     'color:#333; border:none;'
                                     'background:transparent;')
        h.addWidget(self.map_title)
        h.addStretch()
        self.map_stats = QLabel('')
        self.map_stats.setStyleSheet('color:#888; font-size:11px;'
                                     'border:none; background:transparent;')
        h.addWidget(self.map_stats)
        clear = QPushButton('清除轨迹')
        clear.setFixedSize(84, 30)
        clear.setCursor(Qt.CursorShape.PointingHandCursor)
        clear.setStyleSheet(theme.GRAY_BUTTON_QSS)
        clear.clicked.connect(self._clear_map_track)
        h.addWidget(clear)
        lay.addWidget(bar)

        # ---- 卫星地图 ----
        self.sat_map = SatMapWidget()
        lay.addWidget(self.sat_map, stretch=1)
        return page

    def _clear_map_track(self):
        self.sat_map.clear_track()
        self.map_stats.setText('')

    def _show_track_on_map(self, coords_with_time, title):
        """页内卫星地图显示轨迹（先出底图，轨迹随瓦片落地即画）"""
        self.map_title.setText(title)
        self.sat_map.set_track(coords_with_time)
        if coords_with_time:
            self.map_stats.setText(
                f'{len(coords_with_time):,} 点 · '
                f'{coords_with_time[0][2]} ～ {coords_with_time[-1][2]}')
        else:
            self.map_stats.setText('')
        self.stack.setCurrentIndex(1)

    def _make_skill_btn(self, key, icon, name, desc):
        btn = QPushButton(f'{icon}  {name}\n{desc}')
        btn.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setSizePolicy(QSizePolicy.Policy.Expanding,
                          QSizePolicy.Policy.Fixed)
        btn.setMinimumHeight(58)
        btn.setStyleSheet(
            'QPushButton { background:#FFFFFF; border:1px solid #e0e0e0;'
            ' border-radius:8px; padding:10px 12px; text-align:left;'
            ' font-size:12px; color:#555; }'
            'QPushButton:hover { border-color:#07c160; color:#222; }')
        btn.clicked.connect(lambda _, k=key: self._open_skill(k))
        return btn

    def _stat_cell(self, grid, row, col, cap):
        cap_lbl = QLabel(cap)
        cap_lbl.setStyleSheet('color:#888; font-size:11px;'
                              'background:transparent;')
        val_lbl = QLabel('-')
        val_lbl.setStyleSheet('color:#222; font-size:20px; font-weight:600;'
                              'background:transparent;')
        box = QVBoxLayout()
        box.addWidget(cap_lbl)
        box.addWidget(val_lbl)
        holder = QWidget()
        holder.setLayout(box)
        holder.setStyleSheet('background:transparent;')
        grid.addWidget(holder, row, col)
        return val_lbl

    # ---------- 技能分发 ----------

    def _open_skill(self, key):
        if key == 'gps_track':
            from crowui.leftpages.analysis_gps_track import GpsTrackDialog
            dlg = GpsTrackDialog(self.api, self.notify, self)
            dlg.show_track_requested.connect(self._on_dialog_show_track)
            dlg.clear_track_requested.connect(self._clear_map_track)
            dlg.setWindowModality(Qt.WindowModality.NonModal)
            dlg.show()
            self._track_dialog = dlg
            return

    def _on_dialog_show_track(self, points):
        from crowui.leftpages.analysis_gps_track import _RANGE_OPTIONS
        dlg = self._track_dialog
        title = '设备定位分布'
        if dlg is not None:
            did = dlg._cur_device()
            rng = dict(_RANGE_OPTIONS).get(dlg._cur_days(), '')
            title = f'{did} 定位分布{(" · " + rng) if rng else ""}'
        self._show_track_on_map(points, title)

    # ---------- 数据 ----------

    def refresh_stats(self):
        st = self._syncer.stats()
        self.total_label.setText(f"{st['total']:,} 条")
        self.device_label.setText(f"{st['devices']} 台")
        self.last_sync_label.setText(st['last_sync'] or '未同步')

    def showEvent(self, ev):
        super().showEvent(ev)
        self.refresh_stats()     # 切到本页签时刷新计数
