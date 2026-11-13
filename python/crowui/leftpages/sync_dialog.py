# -*- coding: utf-8 -*-
"""同步云端数据独立窗口（功能列表入口）：
把阿里云 devicelog001 增量同步到本机 SQLite，进度实时显示"已同步到几月几号"
"""

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel,
                             QPushButton, QProgressBar, QFrame)
from PyQt6.QtCore import Qt

from crowui import theme
from crowui.ots_sync import DeviceLogSyncer


class SyncDialog(QDialog):
    """云端日志 → 本机缓存 同步窗口（打开自动开始，可重复点"开始同步"）"""

    def __init__(self, api, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[同步]', msg))
        self._syncing = False
        self._syncer = DeviceLogSyncer(api)
        self.setWindowTitle('同步云端数据到本机')
        self.setMinimumWidth(440)
        self._setup_ui()
        self._update_info()
        self._start()          # 打开即自动同步一次

    # ---------- UI ----------

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(18, 16, 18, 14)
        lay.setSpacing(10)

        title = QLabel('☁️ → 💻  云端日志同步')
        title.setStyleSheet('font-size:15px; font-weight:600; color:#333;')
        lay.addWidget(title)

        tip = QLabel('把阿里云端日志表保存到本机，重复同步不产生重复记录。\n'
                     '全量下载 = 整表约 20 万条（OTS 直连，约几分钟）；'
                     '日常同步 = 只拉新数据（几秒）。')
        tip.setStyleSheet('color:#888; font-size:11px;')
        lay.addWidget(tip)

        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        line.setStyleSheet('color:#eee;')
        lay.addWidget(line)

        self.info_label = QLabel('')
        self.info_label.setStyleSheet('color:#555; font-size:12px;')
        lay.addWidget(self.info_label)

        self.bar = QProgressBar()
        self.bar.setRange(0, 0)          # 忙碌指示
        self.bar.setTextVisible(False)
        self.bar.setFixedHeight(8)
        lay.addWidget(self.bar)

        self.status_label = QLabel('待开始')
        self.status_label.setStyleSheet('color:#1a73e8; font-size:12px;')
        lay.addWidget(self.status_label)

        self.result_label = QLabel('')
        self.result_label.setWordWrap(True)
        self.result_label.setStyleSheet('color:#333; font-size:12px;')
        lay.addWidget(self.result_label)

        lay.addStretch()
        btn_row = QHBoxLayout()
        btn_row.addStretch()
        self.full_btn = QPushButton('全量下载')
        self.full_btn.setFixedSize(100, 34)
        self.full_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.full_btn.setStyleSheet(theme.GRAY_BUTTON_QSS)
        self.full_btn.setToolTip('OTS 直连整表重扫（约 20 万条，几分钟），\n'
                                 '补齐本地缺失的全部历史数据')
        self.full_btn.clicked.connect(lambda: self._start(full=True))
        btn_row.addWidget(self.full_btn)
        self.start_btn = QPushButton('开始同步')
        self.start_btn.setFixedSize(110, 34)
        self.start_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.start_btn.setStyleSheet(theme.GREEN_BUTTON_QSS)
        self.start_btn.clicked.connect(self._start)
        btn_row.addWidget(self.start_btn)
        close_btn = QPushButton('关闭')
        close_btn.setFixedSize(80, 34)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet(theme.GRAY_BUTTON_QSS)
        close_btn.clicked.connect(self.close)
        btn_row.addWidget(close_btn)
        lay.addLayout(btn_row)

    # ---------- 逻辑 ----------

    def _update_info(self):
        st = self._syncer.stats()
        tip = f"本机已有 {st['total']:,} 条 / {st['devices']} 台设备"
        if st['last_sync']:
            tip += f" · 上次同步 {st['last_sync']}"
        self.info_label.setText(tip)

    def _start(self, full=False):
        if self._syncing:
            return
        self._syncing = True
        self.start_btn.setEnabled(False)
        self.full_btn.setEnabled(False)
        self.bar.setRange(0, 0)
        self.status_label.setText('连接云端…')
        self.result_label.setText('')

        def work():
            api = self.api
            return self._syncer.sync(
                progress=lambda info: api.report(self._on_progress, info),
                full=full)

        self.api.run_async(work, self._on_done, self._on_err)

    def _on_progress(self, info):
        if info.get('mode') == 'full':
            self.status_label.setText(
                f"全量下载中… 已下载 {info['cur']:,} 行"
                f"（新增 {info['new']:,} 条）")
        else:
            self.status_label.setText(
                f"同步中… 已到 {info['reached'] or '…'}"
                f"（新增 {info['new']} 条）")

    def _on_done(self, r):
        self._syncing = False
        self.start_btn.setEnabled(True)
        self.full_btn.setEnabled(True)
        self.bar.setRange(0, 1)
        self.bar.setValue(1)
        if r['mode'] == 'full':
            msg = (f"全量下载完成：新增 {r['new']:,} 条，"
                   f"本机共 {r['total']:,} 条 / {r['devices']} 台设备，"
                   f"用时 {r['elapsed']}s\n之后点\"开始同步\"只需几秒拉新数据")
        else:
            msg = (f"同步完成：新增 {r['new']} 条，本机共 {r['total']:,} 条，"
                   f"用时 {r['elapsed']}s")
            if r['reached']:
                msg += f"\n本次数据已覆盖到 {r['reached']}"
            if r['capped']:
                msg += '\n⚠ 云端翻页到上限，点\"全量下载\"可补齐更早历史'
        self.result_label.setText(msg)
        self.status_label.setText('空闲')
        self._update_info()
        self.notify(f"云端数据同步完成：新增 {r['new']} 条，本机共 {r['total']} 条")

    def _on_err(self, err):
        self._syncing = False
        self.start_btn.setEnabled(True)
        self.full_btn.setEnabled(True)
        self.bar.setRange(0, 1)
        self.bar.setValue(0)
        self.status_label.setText('同步失败')
        self.result_label.setText(f'❌ {err}')
