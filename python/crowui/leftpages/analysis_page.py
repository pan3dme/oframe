# -*- coding: utf-8 -*-
"""数据分析页（左侧页签）：本机缓存概览，后续按用户指令扩展分析功能"""

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                             QPushButton, QFrame, QGridLayout)
from PyQt6.QtCore import Qt

from crowui import theme
from crowui.ots_sync import DeviceLogSyncer


class AnalysisPage(QWidget):
    """左侧"数据分析"页签：先展示本机日志缓存计数，后续扩展分析图表"""

    def __init__(self, api, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[数据分析]', msg))
        self._syncer = DeviceLogSyncer(api)
        self._setup_ui()
        self.refresh_stats()

    def _setup_ui(self):
        self.setStyleSheet('background: #f5f5f5;')
        lay = QVBoxLayout(self)
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

        lay.addStretch()
        tip = QLabel('（数据分析功能建设中，敬请期待）')
        tip.setAlignment(Qt.AlignmentFlag.AlignCenter)
        tip.setStyleSheet('color:#aaa; font-size:12px; background:transparent;')
        lay.addWidget(tip)

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

    # ---------- 数据 ----------

    def refresh_stats(self):
        st = self._syncer.stats()
        self.total_label.setText(f"{st['total']:,} 条")
        self.device_label.setText(f"{st['devices']} 台")
        self.last_sync_label.setText(st['last_sync'] or '未同步')

    def showEvent(self, ev):
        super().showEvent(ev)
        self.refresh_stats()     # 切到本页签时刷新计数
