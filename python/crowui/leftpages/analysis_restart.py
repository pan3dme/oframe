# -*- coding: utf-8 -*-
"""分析技能：设备重启统计
统计近 1 天 / 近 N(≤10) 天内发生过重启（复位）的设备台数与次数。
重启类型：rst:POR（上电）/ rst:IWDG（看门狗）/ rst:SW（软件）/ DTURST。
纯本地查询，已剔脏时间/重复上报/乱码设备ID。"""

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel,
                             QPushButton, QComboBox, QFrame, QTableWidget,
                             QTableWidgetItem, QHeaderView, QAbstractItemView,
                             QGridLayout)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor

from crowui import theme
from crowui.ots_sync import DeviceLogSyncer

_KIND_STYLE = {
    'IWDG': ('#e53935', '看门狗复位（异常重启）'),
    'POR':  ('#1e88e5', '上电复位（重新供电）'),
    'SW':   ('#8e24aa', '软件复位（远程/升级）'),
    'DTU':  ('#fb8c00', 'DTU 通信模块重启'),
}


class RestartDialog(QDialog):
    """设备重启统计窗口"""

    def __init__(self, api, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[重启统计]', msg))
        self._syncer = DeviceLogSyncer(api)
        self._rows = []
        self.setWindowTitle('分析技能 · 设备重启统计')
        self.setMinimumSize(640, 520)
        self._setup_ui()
        self._refresh()

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(18, 16, 18, 14)
        lay.setSpacing(10)

        title = QLabel('🔁 设备重启统计')
        title.setStyleSheet('font-size:15px; font-weight:600; color:#333;')
        lay.addWidget(title)
        tip = QLabel('重启 = 设备复位记录（type=8）：POR 上电 / IWDG 看门狗 / '
                     'SW 软件 / DTU 通信模块。已剔除脏时间、重复上报与乱码设备ID。'
                     '纯本地查询。')
        tip.setWordWrap(True)
        tip.setStyleSheet('color:#888; font-size:11px;')
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
        self.days_combo.currentIndexChanged.connect(self._refresh)
        row.addWidget(self.days_combo)
        row.addStretch()
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

        # ---- 核心结论：近1天 / 近N天 ----
        box = QFrame()
        box.setStyleSheet('background:#F7FBF8; border:1px solid #dcefe2;'
                          'border-radius:8px;')
        bl = QGridLayout(box)
        bl.setContentsMargins(14, 10, 14, 10)
        self.lbl_1d = self._big_cell(bl, 0, 0, '近 1 天重启')
        self.lbl_nd = self._big_cell(bl, 0, 1, '近 7 天重启')
        self.total_lbl = QLabel('查询中…')
        self.total_lbl.setStyleSheet('color:#555; font-size:12px;'
                                     'background:transparent;')
        self.total_lbl.setWordWrap(True)
        bl.addWidget(self.total_lbl, 1, 0, 1, 2)
        lay.addWidget(box)

        # ---- 类型分布 ----
        self.kind_lbl = QLabel('')
        self.kind_lbl.setStyleSheet('color:#666; font-size:12px;'
                                    'background:transparent;')
        self.kind_lbl.setWordWrap(True)
        lay.addWidget(self.kind_lbl)

        # ---- 设备表 ----
        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(
            ['设备', '重启次数', '类型', '首次', '最后'])
        self.table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.ResizeToContents)
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setShowGrid(False)
        self.table.setAlternatingRowColors(True)
        self.table.setStyleSheet(
            'QTableWidget { background:#FFF; border:1px solid #eee;'
            ' border-radius:6px; font-size:12px; }'
            'QHeaderView::section { background:#F7F7F7; border:none;'
            ' padding:5px; color:#666; }')
        self.table.itemSelectionChanged.connect(self._show_detail)
        lay.addWidget(self.table, stretch=1)

        detail_tip = QLabel('逐条重启记录（选中上表设备）')
        detail_tip.setStyleSheet('color:#888; font-size:11px;')
        lay.addWidget(detail_tip)
        self.detail = QTableWidget(0, 3)
        self.detail.setHorizontalHeaderLabels(['时间', '类型', '原始记录'])
        self.detail.horizontalHeader().setSectionResizeMode(
            2, QHeaderView.ResizeMode.Stretch)
        self.detail.verticalHeader().setVisible(False)
        self.detail.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.detail.setShowGrid(False)
        self.detail.setMaximumHeight(160)
        self.detail.setStyleSheet(
            'QTableWidget { background:#FFF; border:1px solid #eee;'
            ' font-size:11px; }'
            'QHeaderView::section { background:#F7F7F7; border:none;'
            ' padding:4px; color:#666; }')
        lay.addWidget(self.detail)

    @staticmethod
    def _big_cell(grid, r, c, caption):
        holder = QFrame()
        holder.setStyleSheet('background:transparent;')
        v = QVBoxLayout(holder)
        v.setContentsMargins(0, 0, 0, 0)
        v.setSpacing(2)
        cap = QLabel(caption)
        cap.setStyleSheet('color:#888; font-size:11px; background:transparent;')
        val = QLabel('—')
        val.setStyleSheet('font-size:20px; font-weight:700; color:#222;'
                          'background:transparent;')
        v.addWidget(cap)
        v.addWidget(val)
        grid.addWidget(holder, r, c)
        return val

    # ---------- 数据 ----------

    def _refresh(self, *_):
        days = self.days_combo.currentData() or 1
        syncer = self._syncer

        def work():
            return syncer.restart_records(days=1), \
                syncer.restart_records(days=days), days

        self.total_lbl.setText('查询中…')
        self.api.run_async(work, self._apply, self._on_err)

    def _apply(self, pack):
        rows1, rows_n, days = pack
        self._rows = rows_n
        self.lbl_1d.setText(f'{len({r["device"] for r in rows1})} 台')
        self.lbl_nd.setText(f'{len({r["device"] for r in rows_n})} 台')

        kinds = {}
        for r in rows_n:
            kinds[r['kind']] = kinds.get(r['kind'], 0) + 1
        kind_txt = '  ·  '.join(
            f'<span style="color:{_KIND_STYLE[k][0]};">{k} {_KIND_STYLE[k][1]}'
            f'×{n}</span>' for k, n in sorted(kinds.items(),
                                              key=lambda kv: -kv[1]))
        self.kind_lbl.setText('类型分布：' + (kind_txt or '无'))

        self.total_lbl.setText(
            f'近 1 天 {len(rows1)} 次重启 · 近 {days} 天共 {len(rows_n)} 次'
            f'（涉及 {len({r["device"] for r in rows_n})} 台设备）')

        # 按设备聚合
        devs = {}
        for r in rows_n:
            d = devs.setdefault(r['device'], {'n': 0, 'kinds': {},
                                              'first': r['time'],
                                              'last': r['time']})
            d['n'] += 1
            d['kinds'][r['kind']] = d['kinds'].get(r['kind'], 0) + 1
            d['last'] = r['time']
        self.table.blockSignals(True)
        self.table.setRowCount(len(devs))
        for i, (did, d) in enumerate(
                sorted(devs.items(), key=lambda kv: -kv[1]['n'])):
            ktxt = ' '.join(f'{k}×{n}' for k, n in
                            sorted(d['kinds'].items(), key=lambda kv: -kv[1]))
            vals = [did, str(d['n']), ktxt, d['first'], d['last']]
            for c, v in enumerate(vals):
                it = QTableWidgetItem(v)
                if c == 1 and d['n'] >= 3:
                    it.setForeground(QColor('#e53935'))   # 频繁重启标红
                self.table.setItem(i, c, it)
        self.table.blockSignals(False)
        self.detail.setRowCount(0)
        if devs:
            self.table.selectRow(0)
        self._show_detail()

    def _show_detail(self):
        sel = self.table.currentRow()
        if sel < 0 or not self._rows:
            self.detail.setRowCount(0)
            return
        did = self.table.item(sel, 0).text()
        items = [r for r in self._rows if r['device'] == did]
        items.reverse()          # 最新在上
        self.detail.setRowCount(len(items))
        for i, r in enumerate(items):
            color, name = _KIND_STYLE.get(r['kind'], ('#666', r['kind']))
            it_t = QTableWidgetItem(r['time'])
            it_k = QTableWidgetItem(f'{r["kind"]}·{name}')
            it_k.setForeground(QColor(color))
            self.detail.setItem(i, 0, it_t)
            self.detail.setItem(i, 1, it_k)
            self.detail.setItem(i, 2, QTableWidgetItem(r['lorastr']))

    def _on_err(self, err):
        self.total_lbl.setText(f'查询失败：{err}')
