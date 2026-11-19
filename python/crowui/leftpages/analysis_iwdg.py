# -*- coding: utf-8 -*-
"""分析技能：看门狗复位设备列表
列出最近 1~N(≤10) 天内上报 type=8 且含 rst:IWDG 的设备（纯本地缓存查询）。
可切换复位类型（POR 上电 / SW 软件 / 全部 r），点设备看逐条记录。"""

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel,
                             QPushButton, QComboBox, QFrame, QTableWidget,
                             QTableWidgetItem, QHeaderView, QAbstractItemView)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor

from crowui import theme
from crowui.ots_sync import DeviceLogSyncer

_KINDS = [('rst:IWDG（看门狗）', 'rst:IWDG'),
          ('rst:POR（上电）', 'rst:POR'),
          ('rst:SW（软件）', 'rst:SW'),
          ('全部 rst 类', None)]


class IwdgDialog(QDialog):
    """复位设备列表窗口"""

    def __init__(self, api, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[复位分析]', msg))
        self._syncer = DeviceLogSyncer(api)
        self._rows = []
        self.setWindowTitle('分析技能 · 看门狗复位设备')
        self.setMinimumSize(620, 500)
        self._setup_ui()
        self._refresh()

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(18, 16, 18, 14)
        lay.setSpacing(10)

        title = QLabel('🐕 看门狗复位设备')
        title.setStyleSheet('font-size:15px; font-weight:600; color:#333;')
        lay.addWidget(title)
        tip = QLabel('列出近 N 天内上报 type=8 复位记录的设备（N 最大 10 天）。'
                     '已剔除 1970/2036 等脏时间与重复上报。纯本地查询。')
        tip.setStyleSheet('color:#888; font-size:11px;')
        tip.setWordWrap(True)
        lay.addWidget(tip)

        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        line.setStyleSheet('color:#eee;')
        lay.addWidget(line)

        row = QHBoxLayout()
        row.addWidget(QLabel('最近:'))
        self.days_combo = QComboBox()
        for d in range(1, 11):
            self.days_combo.addItem(f'{d} 天', d)
        self.days_combo.setCurrentIndex(0)
        self.days_combo.currentIndexChanged.connect(self._refresh)
        row.addWidget(self.days_combo)
        row.addWidget(QLabel('复位类型:'))
        self.kind_combo = QComboBox()
        for label, k in _KINDS:
            self.kind_combo.addItem(label, k)
        self.kind_combo.currentIndexChanged.connect(self._refresh)
        row.addWidget(self.kind_combo, stretch=1)
        refresh = QPushButton('刷新')
        refresh.setFixedSize(64, 28)
        refresh.setCursor(Qt.CursorShape.PointingHandCursor)
        refresh.setStyleSheet(theme.GREEN_BUTTON_QSS)
        refresh.clicked.connect(self._refresh)
        row.addWidget(refresh)
        close = QPushButton('关闭')
        close.setFixedSize(64, 28)
        close.setCursor(Qt.CursorShape.PointingHandCursor)
        close.setStyleSheet(theme.GRAY_BUTTON_QSS)
        close.clicked.connect(self.close)
        row.addWidget(close)
        lay.addLayout(row)

        # ---- 摘要 ----
        self.summary = QLabel('查询中…')
        self.summary.setStyleSheet(
            'background:#F7FBF8; border:1px solid #dcefe2;'
            'border-radius:8px; padding:10px; font-size:13px;'
            'font-weight:600; color:#222;')
        self.summary.setWordWrap(True)
        lay.addWidget(self.summary)

        # ---- 设备表 ----
        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(
            ['设备', '复位次数', '首次', '最后'])
        self.table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.ResizeToContents)
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(
            QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setShowGrid(False)
        self.table.setAlternatingRowColors(True)
        self.table.setStyleSheet(
            'QTableWidget { background:#FFFFFF; border:1px solid #eee;'
            ' border-radius:6px; font-size:12px; }'
            'QHeaderView::section { background:#F7F7F7; border:none;'
            ' padding:5px; color:#666; }')
        self.table.itemSelectionChanged.connect(self._show_detail)
        lay.addWidget(self.table, stretch=1)

        # ---- 明细 ----
        detail_tip = QLabel('逐条复位记录（选中上表设备）')
        detail_tip.setStyleSheet('color:#888; font-size:11px;')
        lay.addWidget(detail_tip)
        self.detail = QTableWidget(0, 2)
        self.detail.setHorizontalHeaderLabels(['时间', '原始记录'])
        self.detail.horizontalHeader().setSectionResizeMode(
            1, QHeaderView.ResizeMode.Stretch)
        self.detail.verticalHeader().setVisible(False)
        self.detail.setEditTriggers(
            QAbstractItemView.EditTrigger.NoEditTriggers)
        self.detail.setShowGrid(False)
        self.detail.setMaximumHeight(160)
        self.detail.setStyleSheet(
            'QTableWidget { background:#FFFFFF; border:1px solid #eee;'
            ' font-size:11px; }'
            'QHeaderView::section { background:#F7F7F7; border:none;'
            ' padding:4px; color:#666; }')
        lay.addWidget(self.detail)

    # ---------- 数据 ----------

    def _refresh(self, *_):
        days = self.days_combo.currentData() or 1
        kind = self.kind_combo.currentData()
        syncer = self._syncer

        def work():
            return syncer.iwdg_resets(days=days, kind=kind), days, kind

        self.summary.setText('查询中…')
        self.api.run_async(work, self._apply, self._on_err)

    def _apply(self, pack):
        rows, days, kind = pack
        self._rows = rows
        # 按设备聚合
        devs = {}
        for r in rows:
            d = devs.setdefault(r['device'], {'n': 0, 'first': r['time'],
                                              'last': r['time']})
            d['n'] += 1
            d['last'] = r['time']
        self.table.blockSignals(True)
        self.table.setRowCount(len(devs))
        for i, (did, d) in enumerate(
                sorted(devs.items(), key=lambda kv: -kv[1]['n'])):
            vals = [did, str(d['n']), d['first'], d['last']]
            for c, v in enumerate(vals):
                it = QTableWidgetItem(v)
                if c == 1 and d['n'] >= 3:
                    it.setForeground(QColor('#e53935'))   # 频繁复位标红
                self.table.setItem(i, c, it)
        self.table.blockSignals(False)

        label = dict((k, lab) for lab, k in _KINDS).get(kind, '复位')
        if not rows:
            self.summary.setText(
                f'近 {days} 天：没有上报 {label} 的设备')
        else:
            self.summary.setText(
                f'近 {days} 天 · {label}：{len(devs)} 台设备 / '
                f'{len(rows)} 次复位（点设备看逐条记录）')
        self.detail.setRowCount(0)
        if devs:
            self.table.selectRow(0)
        self._show_detail()          # 选中行未变时信号不触发，显式刷新

    def _show_detail(self):
        sel = self.table.currentRow()
        if sel < 0 or not self._rows:
            self.detail.setRowCount(0)
            return
        did = self.table.item(sel, 0).text()
        items = [r for r in self._rows if r['device'] == did]
        items.reverse()       # 最新在上
        self.detail.setRowCount(len(items))
        for i, r in enumerate(items):
            self.detail.setItem(i, 0, QTableWidgetItem(r['time']))
            self.detail.setItem(i, 1, QTableWidgetItem(r['lorastr']))

    def _on_err(self, err):
        self.summary.setText(f'查询失败：{err}')
