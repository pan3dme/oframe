# -*- coding: utf-8 -*-
"""数据分析页（左侧）：
- 顶部缓存概览 + 分析技能卡片（SKILLS 列表扩展）
- 设备状态摘要（在线/迟滞/失联/低电量）
- 近14天上报趋势图（自绘）
- 设备健康总览表（电量/定位成功率）
- 数据构成说明
- 页内卫星地图视图（定位分布技能）"""

import time

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                             QPushButton, QFrame, QGridLayout, QSizePolicy,
                             QStackedWidget, QScrollArea, QTableWidget,
                             QTableWidgetItem, QHeaderView, QAbstractItemView,
                             QComboBox)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QPainter, QPen, QBrush, QFont

from crowui import theme
from crowui.ots_sync import DeviceLogSyncer, SYNC_DONE_CALLBACKS

# 分析技能定义：(key, 图标, 标题, 说明)
SKILLS = [
    ('gps_track', '📍', '设备定位分布',
     '把指定设备的全部有效坐标画到卫星地图（散点）'),
    ('battery', '🔋', '电池电量分析',
     '心跳电量收敛分析：消耗速率/充电检测/可用天数'),
    # 后续技能往这里加
]

# 分析时间窗：(标签, days)
_WINDOW_OPTIONS = [('近30天', 30), ('近7天', 7), ('近90天', 90), ('全部', None)]

_TYPE_CHIPS = [   # 数据构成展示顺序 (type, 名称, 是否有分析价值)
    (2, '心跳/电量', True), (1, '定位', True), (5, '跟踪', True),
    (8, '设备参数', True), (6, '配置上报', False), (9, '模式配置', False),
    (4, '杂讯', False),
]


class DailyBars(QWidget):
    """近14天上报量柱状图（自绘，无第三方依赖）"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data = []            # [(label, count)]
        self.setMinimumHeight(130)
        self.setStyleSheet('background:transparent;')

    def set_data(self, data):
        self._data = list(data)
        self.update()

    def paintEvent(self, ev):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), QColor('#FFFFFF'))
        if not self._data:
            return
        w, h = self.width(), self.height()
        pad_l, pad_b, pad_t = 34, 20, 16
        n = len(self._data)
        gap = (w - pad_l - 10) / n
        bw = max(6.0, gap * 0.62)
        mx = max(c for _, c in self._data) or 1

        p.setPen(QPen(QColor('#DDDDDD'), 1))
        p.drawLine(pad_l, h - pad_b, w - 6, h - pad_b)      # 基线
        p.setPen(QPen(QColor('#F0F0F0'), 1))
        p.drawLine(pad_l, pad_t, w - 6, pad_t)              # 峰值参考线

        for i, (label, c) in enumerate(self._data):
            x = pad_l + i * gap + (gap - bw) / 2
            bh = (h - pad_b - pad_t) * (c / mx)
            p.setBrush(QBrush(QColor(7, 193, 96, 200)))
            p.setPen(Qt.PenStyle.NoPen)
            p.drawRect(int(x), int(h - pad_b - bh), int(bw), max(1, int(bh)))
            p.setFont(QFont('Microsoft YaHei', 7))
            p.setPen(QColor('#888888'))
            p.drawText(int(x) - 6, int(h - pad_b - bh) - 4,
                       f'{c // 1000}k' if c >= 10000 else str(c))
            p.drawText(int(x) - 8, h - 6, label)


class AnalysisPage(QWidget):
    """左侧"数据分析"：概览 + 技能卡 + 趋势图 + 设备健康总览 + 页内地图"""

    def __init__(self, api, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[数据分析]', msg))
        self._syncer = DeviceLogSyncer(api)
        self._track_dialog = None
        self._setup_ui()
        self.refresh_stats()
        # 同步完成后自动重算（同步在工作线程，回调里再派发回主线程）
        SYNC_DONE_CALLBACKS.append(self._on_sync_done)

    def _on_sync_done(self, _result):
        try:
            r = self._syncer.analyze(days=self._cur_days())
            self.api.report(self._apply_analysis, r)
        except Exception:
            pass

    def _cur_days(self):
        return self.window_combo.currentData()

    def _setup_ui(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)
        self.stack = QStackedWidget()
        outer.addWidget(self.stack, stretch=1)
        self.stack.addWidget(self._build_skills_page())     # 0 总览
        self.stack.addWidget(self._build_map_page())        # 1 卫星地图

    # ==================== 页 0：总览 ====================

    def _build_skills_page(self):
        page = QWidget()
        page.setStyleSheet('background: #f5f5f5;')
        outer = QVBoxLayout(page)
        outer.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setStyleSheet('QScrollArea { background:transparent; }')
        outer.addWidget(scroll, stretch=1)

        body = QWidget()
        body.setStyleSheet('background:transparent;')
        lay = QVBoxLayout(body)
        lay.setContentsMargins(16, 16, 16, 16)
        lay.setSpacing(12)
        scroll.setWidget(body)

        # ---- 标题行 ----
        head = QHBoxLayout()
        title = QLabel('📊 数据分析')
        title.setStyleSheet('font-size:16px; font-weight:600; color:#333;'
                            'background:transparent;')
        head.addWidget(title)
        head.addStretch()
        head.addWidget(QLabel('时间段:'))
        self.window_combo = QComboBox()
        for label, d in _WINDOW_OPTIONS:
            self.window_combo.addItem(label, d)
        self.window_combo.setCurrentIndex(0)      # 默认近30天
        self.window_combo.currentIndexChanged.connect(
            self.refresh_stats)
        head.addWidget(self.window_combo)
        refresh_btn = QPushButton('刷新')
        refresh_btn.setFixedSize(64, 26)
        refresh_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        refresh_btn.setStyleSheet(theme.GREEN_BUTTON_QSS)
        refresh_btn.clicked.connect(self.refresh_stats)
        head.addWidget(refresh_btn)
        lay.addLayout(head)

        # ---- 本机缓存概览 ----
        card = self._card()
        g = QGridLayout(card)
        g.setContentsMargins(14, 12, 14, 12)
        g.setHorizontalSpacing(24)
        g.setVerticalSpacing(6)
        self.total_label = self._stat_cell(g, 0, 0, '📦 本机缓存记录')
        self.device_label = self._stat_cell(g, 0, 1, '📱 覆盖设备')
        self.last_sync_label = self._stat_cell(g, 0, 2, '⏱ 上次同步')
        lay.addWidget(card)

        # ---- 分析技能 ----
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
            grid.addWidget(self._make_skill_btn(key, icon, name, desc),
                           i // 3, i % 3)
        for col in range(3):
            grid.setColumnStretch(col, 1)
        lay.addWidget(grid_holder)

        # ---- 设备状态摘要 ----
        status_card = self._card()
        sg = QGridLayout(status_card)
        sg.setContentsMargins(14, 12, 14, 12)
        sg.setHorizontalSpacing(18)
        sg.setVerticalSpacing(6)
        self.online_label = self._stat_cell(sg, 0, 0, '🟢 在线(2h内)')
        self.lag_label = self._stat_cell(sg, 0, 1, '🟠 迟滞(2~24h)')
        self.offline_label = self._stat_cell(sg, 0, 2, '⚪ 失联(>24h)')
        self.lowbat_label = self._stat_cell(sg, 0, 3, '🔴 低电量(<20%)')
        lay.addWidget(status_card)

        # ---- 近14天上报趋势 ----
        trend_card = self._card()
        tv = QVBoxLayout(trend_card)
        tv.setContentsMargins(14, 10, 14, 10)
        t = QLabel('📈 上报量趋势')
        self.trend_title = t
        t.setStyleSheet('color:#555; font-size:12px; font-weight:600;'
                        'background:transparent;')
        tv.addWidget(t)
        self.trend_bars = DailyBars()
        self.trend_bars.setSizePolicy(QSizePolicy.Policy.Expanding,
                                      QSizePolicy.Policy.Fixed)
        tv.addWidget(self.trend_bars)
        lay.addWidget(trend_card)

        # ---- 设备健康总览表 ----
        table_card = self._card()
        tbl_v = QVBoxLayout(table_card)
        tbl_v.setContentsMargins(14, 10, 14, 10)
        t2 = QLabel('📡 设备健康总览（按最后上报排序，点击表头可排序）')
        t2.setStyleSheet('color:#555; font-size:12px; font-weight:600;'
                         'background:transparent;')
        tbl_v.addWidget(t2)
        self.dev_table = QTableWidget(0, 6)
        self.dev_table.setHorizontalHeaderLabels(
            ['设备', '状态', '最后上报', '电量', '上报条数', '定位成功率'])
        self.dev_table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.ResizeToContents)
        self.dev_table.horizontalHeader().setStretchLastSection(True)
        self.dev_table.verticalHeader().setVisible(False)
        self.dev_table.setEditTriggers(
            QAbstractItemView.EditTrigger.NoEditTriggers)
        self.dev_table.setSelectionMode(
            QAbstractItemView.SelectionMode.SingleSelection)
        self.dev_table.setShowGrid(False)
        self.dev_table.setAlternatingRowColors(True)
        self.dev_table.setSortingEnabled(True)
        self.dev_table.setStyleSheet(
            'QTableWidget { background:#FFFFFF; border:none; font-size:11px; }'
            'QHeaderView::section { background:#F7F7F7; border:none;'
            ' padding:4px; font-size:11px; color:#666; }')
        self.dev_table.setMinimumHeight(300)
        tbl_v.addWidget(self.dev_table)
        lay.addWidget(table_card)

        # ---- 数据构成 ----
        comp_card = self._card()
        cv = QVBoxLayout(comp_card)
        cv.setContentsMargins(14, 10, 14, 10)
        t3 = QLabel('🧱 数据构成（本机缓存里都有什么）')
        t3.setStyleSheet('color:#555; font-size:12px; font-weight:600;'
                         'background:transparent;')
        cv.addWidget(t3)
        self.comp_label = QLabel('-')
        self.comp_label.setWordWrap(True)
        self.comp_label.setStyleSheet('color:#666; font-size:11px;'
                                      'background:transparent;')
        cv.addWidget(self.comp_label)
        lay.addWidget(comp_card)

        lay.addStretch()
        return page

    def _card(self):
        card = QFrame()
        card.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        card.setStyleSheet('background:#FFFFFF; border:1px solid #eee;'
                           'border-radius:8px;')
        return card

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

    # ==================== 页 1：卫星地图 ====================

    def _build_map_page(self):
        from crowui.sat_map_widget import SatMapWidget
        page = QWidget()
        page.setStyleSheet('background: #1a1a2e;')
        lay = QVBoxLayout(page)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

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
        clear = QPushButton('清除')
        clear.setFixedSize(64, 30)
        clear.setCursor(Qt.CursorShape.PointingHandCursor)
        clear.setStyleSheet(theme.GRAY_BUTTON_QSS)
        clear.clicked.connect(self._clear_map_track)
        h.addWidget(clear)
        lay.addWidget(bar)

        self.sat_map = SatMapWidget()
        lay.addWidget(self.sat_map, stretch=1)
        return page

    def _clear_map_track(self):
        self.sat_map.clear_track()
        self.map_stats.setText('')

    def _show_track_on_map(self, coords_with_time, title):
        self.map_title.setText(title)
        self.sat_map.set_track(coords_with_time)
        if coords_with_time:
            self.map_stats.setText(
                f'{len(coords_with_time):,} 点 · '
                f'{coords_with_time[0][2]} ～ {coords_with_time[-1][2]}')
        else:
            self.map_stats.setText('')
        self.stack.setCurrentIndex(1)

    # ==================== 技能分发 ====================

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
        if key == 'battery':
            from crowui.leftpages.analysis_battery import BatteryDialog
            dlg = BatteryDialog(self.api, self.notify, self)
            dlg.setWindowModality(Qt.WindowModality.NonModal)
            dlg.show()
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

    # ==================== 数据刷新 ====================

    def refresh_stats(self):
        st = self._syncer.stats()
        self.total_label.setText(f"{st['total']:,} 条")
        self.device_label.setText(f"{st['devices']} 台")
        self.last_sync_label.setText(st['last_sync'] or '未同步')
        # 聚合分析放工作线程，避免卡界面
        days = self._cur_days()
        syncer = self._syncer
        self.api.run_async(lambda: syncer.analyze(days=days),
                           self._apply_analysis, self._on_analysis_err)

    def _on_analysis_err(self, err):
        print('[数据分析] 分析失败:', err)

    def _apply_analysis(self, r):
        """填充分析结果（主线程）"""
        s = r['summary']
        self.online_label.setText(str(s['online']))
        self.lag_label.setText(str(s['lag']))
        self.offline_label.setText(str(s['offline']))
        self.lowbat_label.setText(f"{s['lowbat']} 台")
        days = r.get('days')
        self.trend_title.setText(
            f"📈 上报量趋势（近 {len(r['daily'])} 天"
            f"{'' if days in (None, 14) else '，受时间段过滤影响'}）")
        self.trend_bars.set_data(r['daily'])
        self._fill_device_table(r['devices'])
        self._fill_composition(r['types'], s)

    def _fill_device_table(self, devs):
        now = time.time()
        self.dev_table.setSortingEnabled(False)
        self.dev_table.setRowCount(len(devs))
        for i, d in enumerate(devs):
            st_txt, online, lowbat = self._syncer.device_status(d, now)
            last_txt = time.strftime('%m-%d %H:%M',
                                     time.localtime(d['last_ts'])) \
                if d['last_ts'] else '—'
            rate = (f"{d['gps_ok'] / d['gps_total'] * 100:.0f}%"
                    if d['gps_total'] else '—')
            vals = [d['id'], st_txt, last_txt,
                    f"{d['battery']}%" if d['battery'] is not None else '—',
                    f"{d['count']:,}", rate]
            for c, v in enumerate(vals):
                it = QTableWidgetItem(v)
                it.setForeground(QColor('#333333'))
                if c == 1:
                    color = ('#07c160' if online else
                             '#ff9800' if st_txt == '迟滞' else '#999999')
                    it.setForeground(QColor(color))
                if c == 3 and lowbat:
                    it.setForeground(QColor('#e53935'))
                if c == 4:      # 数字列参与排序
                    it.setData(Qt.ItemDataRole.DisplayRole, int(d['count']))
                self.dev_table.setItem(i, c, it)
        self.dev_table.setSortingEnabled(True)
        self.dev_table.sortItems(2, Qt.SortOrder.DescendingOrder)

    def _fill_composition(self, types, summary):
        parts = []
        for typ, name, useful in _TYPE_CHIPS:
            cnt = types.get(typ)
            if not cnt:
                continue
            pct = cnt / sum(types.values()) * 100
            mark = '' if useful else '（参考价值低）'
            parts.append(f'{name} {cnt:,} 条（{pct:.0f}%）{mark}')
        gps_pct = (summary['gps_ok'] / summary['gps_total'] * 100
                   if summary['gps_total'] else 0)
        parts.append(f'定位成功率（全群）{gps_pct:.0f}%'
                     f'（有效 {summary["gps_ok"]:,} / 定位类 {summary["gps_total"]:,} 条）')
        self.comp_label.setText('  ·  '.join(parts[:-1]) + '\n' + parts[-1])
