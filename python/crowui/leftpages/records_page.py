# -*- coding: utf-8 -*-
"""云端记录页（对标小程序 pages/cloud-records）：卡片式记录列表，三个类型独立分页
电量走势页（对标小程序 pages/battery-chart）：对时记录取电量画曲线
"""

import colorsys
from datetime import datetime

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                             QTableWidget, QTableWidgetItem, QHeaderView, QTabWidget,
                             QComboBox, QScrollArea)
from PyQt6.QtGui import QPainter, QPen, QColor, QPainterPath
from PyQt6.QtCore import Qt, QRectF, pyqtSignal, QEvent, QTimer

from crowui import theme, wx_codec
from crowui.wx_api import WxApi

TYPE_NAMES = {1: '定位', 2: '对时', 3: '电量', 5: '跟踪', 6: '设置'}
# 类型徽章颜色（对标 .rec-icon-lora.type-N）
TYPE_BADGE_COLOR = {1: '#1976d2', 2: '#ff9500', 3: '#4CAF50', 5: '#8E24AA', 6: '#607D8B'}
# 设备名鲜艳色板（对标 _deviceColor 的 vividColors）
_VIVID_COLORS = ['#E53935', '#1E88E5', '#43A047', '#FB8C00', '#8E24AA',
                 '#00ACC1', '#F4511E', '#D81B60', '#5E35B1', '#039BE5',
                 '#2E7D32', '#C0CA33']


def _name_hash_idx(name, mod):
    """与小程序一致的字符 hash：h = (h*31 + code) % mod"""
    h = 0
    for ch in name:
        h = (h * 31 + ord(ch)) % mod
    return h


def _device_color(name):
    """按上传设备名生成稳定鲜艳色（对标 cloud-records._deviceColor）"""
    if not name or name == '-':
        return '#999999'
    return _VIVID_COLORS[_name_hash_idx(name, len(_VIVID_COLORS))]


def _device_pastel(name):
    """按上传设备名生成稳定浅色背景（对标 cloud-records._devicePastel，hsl→hex）"""
    if not name or name == '-':
        return '#f2f2f2'
    h = _name_hash_idx(name, 360)
    s = (35 + h % 15) / 100
    l = (86 + h % 10) / 100
    r, g, b = colorsys.hls_to_rgb(h / 360, l, s)
    return '#%02X%02X%02X' % (round(r * 255), round(g * 255), round(b * 255))


class _RecordCard(QWidget):
    """单条记录卡（对标 .record-item）：pastel 底色 + 三行内容；定位/跟踪可点击"""
    clicked = pyqtSignal(dict)

    def __init__(self, rec, parent=None):
        super().__init__(parent)
        self.rec = rec
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        # 最后一行不加分隔线（对标 .record-item:last-child { border-bottom: none }）
        border = '' if rec.get('last') else 'border-bottom: 1px solid rgba(0, 0, 0, 0.04);'
        self.setStyleSheet(f'background: {rec["bg"]}; {border}')
        if rec['tappable']:
            self.setCursor(Qt.CursorShape.PointingHandCursor)
        v = QVBoxLayout(self)
        v.setContentsMargins(12, 10, 12, 10)
        v.setSpacing(3)

        # —— 行1：时间 | 上报设备（鲜艳色 + 别名黑字）——
        r1 = QHBoxLayout()
        r1.setSpacing(0)
        time_lab = QLabel(f'{rec["date"]} {rec["time_part"]}'.strip())
        time_lab.setStyleSheet('color: #555555; font-size: 14px; font-weight: 500;'
                               'border: none; background: transparent;')
        r1.addWidget(time_lab, stretch=1)
        dev_lab = QLabel(f'| {rec["upDateDevice"]}')
        dev_lab.setStyleSheet(f'color: {rec["deviceColor"]}; font-size: 15px;'
                              f'font-weight: 700; border: none; background: transparent;')
        r1.addWidget(dev_lab)
        if rec['alias']:
            alias_lab = QLabel(f'({rec["alias"]})')
            alias_lab.setStyleSheet('color: #000000; font-size: 12px;'
                                    'border: none; background: transparent;')
            r1.addWidget(alias_lab)
        v.addLayout(r1)

        # —— 行2：类型徽章 + lorastr（蓝）+ 定位/跟踪箭头 ——
        r2 = QHBoxLayout()
        r2.setSpacing(5)
        t_name = TYPE_NAMES.get(rec['msgType'], str(rec['msgType']))
        badge = QLabel(t_name)
        badge.setStyleSheet(f'color: #FFFFFF; background: {rec["badgeColor"]};'
                            f'font-size: 11px; padding: 1px 5px; border-radius: 3px;'
                            f'border: none;')
        r2.addWidget(badge)
        lora_lab = QLabel(rec['lorastr'])
        lora_lab.setStyleSheet('color: #1565c0; font-size: 16px;'
                               'border: none; background: transparent;')
        r2.addWidget(lora_lab, stretch=1)
        if rec['tappable']:
            arrow = QLabel('›')
            arrow.setStyleSheet('color: #1976d2; font-size: 20px; font-weight: 700;'
                                'border: none; background: transparent;')
            r2.addWidget(arrow)
        v.addLayout(r2)

        # —— 行3：RSSI（左） SNR（右），两端对齐（对标 .rec-row-rssi space-between）——
        if rec['rssi'] or rec['snr']:
            r3 = QHBoxLayout()
            r3.setSpacing(0)
            if rec['rssi']:
                rssi_lab = QLabel(f'RSSI: {rec["rssi"]}dBm')
                rssi_lab.setStyleSheet('color: #333333; font-size: 12px;'
                                       'border: none; background: transparent;')
                r3.addWidget(rssi_lab)
            r3.addStretch(1)
            if rec['snr']:
                snr_lab = QLabel(f'SNR: {rec["snr"]}')
                snr_lab.setStyleSheet('color: #333333; font-size: 12px;'
                                      'border: none; background: transparent;')
                r3.addWidget(snr_lab)
            v.addLayout(r3)

    def mouseReleaseEvent(self, event):
        if self.rec['tappable'] and event.button() == Qt.MouseButton.LeftButton:
            self.clicked.emit(self.rec)
        super().mouseReleaseEvent(event)


class RecordsPage(QWidget):
    """云端记录：全部/GPS/对时 chips + 卡片列表（对标小程序 cloud-records）"""

    locate_requested = pyqtSignal(dict)   # 点击定位/跟踪记录 → 跳地图

    def __init__(self, api: WxApi, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[记录]', msg))
        self._rename_map = {}
        # 三个筛选各自独立的数据与分页状态（对标 recordsByType）
        self._tabs = {k: {'page': 0, 'rows': [], 'loaded': False, 'has_more': True}
                      for k in ('all', 'gps', 'sync')}
        # 设备模式（对标 device-detail?mode=records：详情页"数据列表"→ 该设备的记录）
        self._device_id = None
        self._device_tabs = None
        self.active = 'all'
        self._setup_ui()

    def _setup_ui(self):
        self.setStyleSheet('background: #f5f5f5;')
        # 页面左右/上下 12px 边距（对标 .recv-header/.msg-panel 的 margin: 24rpx）
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(0)

        # ---- 头部：标题 + chips + 条数（对标 .recv-header）----
        header = QWidget()
        header.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        header.setStyleSheet('background: #FFFFFF; border-bottom: 1px solid #f0f0f0;'
                             'border-radius: 8px 8px 0 0;')
        h = QHBoxLayout(header)
        h.setContentsMargins(10, 8, 10, 8)
        h.setSpacing(8)
        title = QLabel('☁️ 云端记录')
        title.setStyleSheet('color: #555555; font-size: 14px; font-weight: 600;'
                            'border: none; background: transparent;')
        h.addWidget(title)
        self._title_label = title
        h.addSpacing(4)
        self._chips = {}
        for key, label in (('all', '📋 全部'), ('gps', '📍 GPS'), ('sync', '🕐 对时')):
            chip = QPushButton(label)
            chip.setFixedHeight(22)
            chip.setCursor(Qt.CursorShape.PointingHandCursor)
            chip.clicked.connect(lambda _, k=key: self._on_chip(k))
            h.addWidget(chip)
            self._chips[key] = chip
        h.addStretch()
        self.count_label = QLabel('0 条')
        self.count_label.setStyleSheet('color: #999999; font-size: 12px;'
                                       'border: none; background: transparent;')
        h.addWidget(self.count_label)
        layout.addWidget(header)

        # ---- 记录卡片列表 ----
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet(theme.SCROLLBAR_QSS)
        panel = QWidget()
        panel.setObjectName('recPanel')
        panel.setStyleSheet('QWidget#recPanel { background: #FFFFFF;'
                            'border-radius: 0 0 8px 8px; }')
        # 面板投影（对标 .msg-panel box-shadow 0 2rpx 16rpx rgba(0,0,0,0.05)）
        from PyQt6.QtWidgets import QGraphicsDropShadowEffect
        from PyQt6.QtGui import QColor as _QC
        shadow = QGraphicsDropShadowEffect(self)
        shadow.setBlurRadius(8)
        shadow.setOffset(0, 1)
        shadow.setColor(_QC(0, 0, 0, 15))
        scroll.setGraphicsEffect(shadow)
        self.list_layout = QVBoxLayout(panel)
        self.list_layout.setContentsMargins(0, 0, 0, 0)
        self.list_layout.setSpacing(0)
        scroll.setWidget(panel)
        layout.addWidget(scroll, stretch=1)
        self._scroll = scroll

        # ---- 下拉刷新（对标 refresher-enabled threshold 60）+ 滚到底自动翻页 ----
        self._pull = {'active': False, 'start_y': 0, 'dy': 0, 'suppress_click': False}
        self._refreshing = False
        self._spinner_chars = '⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏'
        self._spinner_idx = 0
        self._pull_overlay = QLabel(scroll)
        self._pull_overlay.setStyleSheet("""
            QLabel { background: rgba(0, 0, 0, 0.55); color: #fff; font-size: 12px;
                     padding: 5px 14px; border-radius: 14px; border: none; }
        """)
        self._pull_overlay.hide()
        self._spinner_timer = QTimer(self)
        self._spinner_timer.setInterval(90)
        self._spinner_timer.timeout.connect(self._tick_spinner)
        scroll.viewport().installEventFilter(self)
        panel.installEventFilter(self)
        # 滚动到距底 80px 自动加载下一页（对标 bindscrolltolower lower-threshold 80），
        # 上一页/下一页按钮已移除——翻页全由滚动接管
        scroll.verticalScrollBar().valueChanged.connect(self._on_scroll_near_bottom)

        self._apply_chip_styles()

    # ==================== 下拉刷新手势（与设备列表同款） ====================

    _PULL_THRESHOLD = 60

    def eventFilter(self, obj, ev):
        t = ev.type()
        if t == QEvent.Type.MouseButtonPress and ev.button() == Qt.MouseButton.LeftButton:
            self._pull['active'] = self._scroll.verticalScrollBar().value() == 0 \
                and not self._refreshing
            self._pull['start_y'] = ev.globalPosition().toPoint().y()
            self._pull['dy'] = 0
            self._pull['suppress_click'] = False
        elif t == QEvent.Type.MouseMove and self._pull['active']:
            dy = ev.globalPosition().toPoint().y() - self._pull['start_y']
            self._pull['dy'] = max(0, dy)
            self._update_pull_overlay()
        elif t == QEvent.Type.MouseButtonRelease and self._pull['active']:
            self._pull['active'] = False
            dy = self._pull['dy']
            self._pull['dy'] = 0
            self._pull['suppress_click'] = dy > 10   # 拖拽过的释放不当作卡片点击
            if dy >= self._PULL_THRESHOLD and self.api.logged_in:
                self._show_refreshing()
                self._load(self.active, 1)   # 刷新当前筛选：回到第 1 页替换
            else:
                self._hide_pull_overlay()
        return super().eventFilter(obj, ev)

    def _update_pull_overlay(self):
        """拖拽中：指示器跟随 + 内容随手指下移"""
        dy = self._pull['dy']
        self.list_layout.setContentsMargins(0, min(dy, 80) // 2, 0, 0)
        if dy < 8:
            self._pull_overlay.hide()
            return
        text = '↑ 释放刷新' if dy >= self._PULL_THRESHOLD else '↓ 下拉刷新'
        self._pull_overlay.setText(text)
        self._pull_overlay.adjustSize()
        self._pull_overlay.move((self._scroll.width() - self._pull_overlay.width()) // 2,
                                max(-self._pull_overlay.height() + 6,
                                    10 + int(dy * 0.4) - self._pull_overlay.height() // 2))
        self._pull_overlay.show()
        self._pull_overlay.raise_()

    def _show_refreshing(self):
        """刷新中：顶部固定指示器 + 转圈动画（对标 refresher 持续态）"""
        self._refreshing = True
        self._pull['dy'] = 0
        self._spinner_idx = 0
        self._spinner_timer.start()
        self.list_layout.setContentsMargins(0, 40, 0, 0)
        self._pull_overlay.setText(f'{self._spinner_chars[0]} 刷新中...')
        self._pull_overlay.adjustSize()
        self._pull_overlay.move((self._scroll.width() - self._pull_overlay.width()) // 2, 10)
        self._pull_overlay.show()
        self._pull_overlay.raise_()

    def _tick_spinner(self):
        self._spinner_idx = (self._spinner_idx + 1) % len(self._spinner_chars)
        self._pull_overlay.setText(f'{self._spinner_chars[self._spinner_idx]} 刷新中...')

    def _hide_pull_overlay(self):
        self._refreshing = False
        self._spinner_timer.stop()
        self._pull_overlay.hide()
        self.list_layout.setContentsMargins(0, 0, 0, 0)   # 内容回弹复位

    def _on_scroll_near_bottom(self, value):
        """距底 80px 内自动加载下一页（对标 onScrollToLower）"""
        if self._loading or self._refreshing or not self.api.logged_in:
            return
        tab = self._cur[self.active]
        if not tab['loaded'] or not tab['has_more']:
            return
        sb = self._scroll.verticalScrollBar()
        if sb.maximum() - value <= 80:
            self._load(self.active, tab['page'] + 1)

    def _apply_chip_styles(self):
        """chips：未选白底灰字灰边，选中蓝 #1989fa（对标 .recv-filter-on）"""
        for key, chip in self._chips.items():
            if key == self.active:
                chip.setStyleSheet('QPushButton { background: #1989fa; color: #fff;'
                                   'border: 1px solid #1989fa; border-radius: 10px;'
                                   'padding: 2px 7px; font-size: 11px; }')
            else:
                chip.setStyleSheet('QPushButton { background: #fff; color: #999999;'
                                   'border: 1px solid #dddddd; border-radius: 10px;'
                                   'padding: 2px 7px; font-size: 11px; }'
                                   'QPushButton:hover { color: #1989fa; }')

    @property
    def _cur(self):
        """当前模式的状态存取（设备模式/云端模式各自独立缓存）"""
        return self._device_tabs if self._device_id else self._tabs

    def _apply_mode(self):
        """标题与 chips 文案随模式切换（对标两处导航标题）"""
        if self._device_id:
            self._title_label.setText('📊 设备记录')
            labels = {'all': '📋 全部', 'gps': '📍 定位', 'sync': '🕐 对时'}
        else:
            self._title_label.setText('☁️ 云端记录')
            labels = {'all': '📋 全部', 'gps': '📍 GPS', 'sync': '🕐 对时'}
        for key, chip in self._chips.items():
            chip.setText(labels[key])

    def open_for_device(self, device_id):
        """设备详情"数据列表" → 该设备的记录页（对标 mode=records）"""
        if not device_id:
            self.notify('未选择设备')
            return
        if self._device_id != device_id or not self._device_tabs:
            self._device_id = device_id
            self._device_tabs = {k: {'page': 0, 'rows': [], 'loaded': False,
                                     'has_more': True}
                                 for k in ('all', 'gps', 'sync')}
            self.active = 'all'
        self._apply_mode()
        self._apply_chip_styles()
        if not self._device_tabs['all']['loaded'] and not self._loading:
            self._render_empty('⏳ 加载中...')
            self._load('all', 1)
        else:
            self._render()

    def open_cloud(self):
        """功能列表"云端记录" → 全部设备最新日志（保留已有缓存）"""
        self._device_id = None
        self._apply_mode()
        self._apply_chip_styles()
        if not self._tabs['all']['loaded'] and not self._loading:
            self._load('all', 1)
        else:
            self._render()

    def _on_chip(self, key):
        if key == self.active:
            return
        self.active = key
        self._apply_chip_styles()
        tab = self._cur[key]
        if tab['loaded']:
            self._render()
        else:
            self._render_empty('⏳ 加载中...')
            self._load(key, 1)

    def _ensure_loaded(self, tab_idx=0):
        """首次进入自动拉"全部"第一页（leftpanel 调用）"""
        if not self._cur['all']['loaded'] and not self._loading:
            self._load('all', 1)

    _loading = False

    def _load(self, key, page):
        if not self.api.logged_in:
            self.notify('请先登录')
            return
        codes = {'all': None, 'gps': 1, 'sync': 2}
        self._loading = True
        if self._device_id:
            # 设备模式：getDeviceLogbyId 按 offset 翻页（对标 loadTodayRecords）
            offset = (page - 1) * 10
            self.api.run_async(
                lambda: self.api.getDeviceLogbyId(self._device_id, limit=10,
                                                  offset=offset, log_type=codes[key]),
                on_ok=lambda rows, k=key, p=page: self._on_rows(k, p, rows),
                on_err=lambda m, k=key: (self._loading_done(), self._hide_pull_overlay(),
                                         self.notify(f'加载失败: {m}')),
            )
        else:
            self.api.run_async(
                lambda: self.api.getlastlog(page=page, limit=10, log_type=codes[key]),
                on_ok=lambda rows, k=key, p=page: self._on_rows(k, p, rows),
                on_err=lambda m, k=key: (self._loading_done(), self._hide_pull_overlay(),
                                         self.notify(f'加载失败: {m}')),
            )

    def _loading_done(self):
        self._loading = False

    def _on_rows(self, key, page, rows):
        self._loading = False
        # 是否还有下一页：整页拉满 10 条才可能还有更多（对标 hasMore = length >= pageSize）
        self._cur[key]['has_more'] = len(rows) >= 10
        if not rows and page > 1:
            if key == self.active:
                self._render()   # 补"没有更多了"提示
            return
        if not rows and page == 1:
            self._cur[key].update({'page': 1, 'rows': [], 'loaded': True})
            if key == self.active:
                self._render()
            return
        # 翻页去重（同 rawTime+lorastr 不重复追加）；整页全是重复 → 视为没有更多
        if page > 1:
            exist = {(r.get('time'), r.get('lorastr')) for r in self._cur[key]['rows']}
            rows = [r for r in rows if (r.get('time'), r.get('lorastr')) not in exist]
            if not rows:
                self._cur[key]['has_more'] = False
                if key == self.active:
                    self._render()
                return
            self._cur[key]['rows'].extend(rows)
        else:
            self._cur[key]['rows'] = list(rows)
        self._cur[key]['page'] = page
        self._cur[key]['loaded'] = True
        # 首次拉数据时顺带构建 设备别名映射（对标 onLoad 的 renameMap）
        if not self._rename_map and self.api.logged_in:
            self.api.run_async(
                lambda: self.api.getDeviceList(),
                on_ok=self._on_devices_for_rename,
                on_err=lambda m: None,
            )
        if key == self.active:
            if self._refreshing:
                self._hide_pull_overlay()   # 下拉刷新完成：收指示器 + 内容回弹
            self._render()

    def _on_devices_for_rename(self, rows):
        self._rename_map = {str(r.get('deviceId', '')): str(r.get('rename', '') or '')
                            for r in rows or []}
        self._render()

    # ==================== 记录解析与渲染 ====================

    def _parse_rec(self, row):
        raw_time = str(row.get('time', '') or '-')
        if ' ' in raw_time:
            date, time_part = raw_time.split(' ', 1)
        else:
            date, time_part = raw_time, ''
        lorastr = str(row.get('lorastr', '') or '-')
        segs = lorastr.split('|')
        try:
            msg_type = int(segs[0]) if segs and segs[0].strip().isdigit() else 0
        except ValueError:
            msg_type = 0
        rssi = str(row.get('rssi', '') or '')
        snr = str(row.get('snr', '') or '')
        # rssi/snr 为空时从 lorastr 末段兜底提取（对标 parseRecordList）
        if len(segs) >= 2:
            if not rssi and segs[-1].strip().lstrip('-').isdigit():
                rssi = segs[-1].strip()
            if not snr and len(segs) >= 3:
                sec = segs[-2].strip()
                try:
                    float(sec)
                    snr = sec
                except ValueError:
                    pass
        up = str(row.get('upDateDevice', '') or '-')
        return {
            'date': date, 'time_part': time_part, 'lorastr': lorastr,
            'msgType': msg_type, 'rssi': rssi, 'snr': snr,
            'upDateDevice': up, 'alias': self._rename_map.get(up, ''),
            'deviceColor': _device_color(up), 'bg': _device_pastel(up),
            'badgeColor': TYPE_BADGE_COLOR.get(msg_type, '#999999'),
            'tappable': msg_type in (1, 5),
            'lat': None, 'lng': None,
        }

    def _render(self):
        self._render_rows(self._cur[self.active]['rows'])

    def _render_empty(self, text):
        while self.list_layout.count():
            item = self.list_layout.takeAt(0)
            w = item.widget()
            if w:
                w.deleteLater()
        tip = QLabel(text)
        tip.setAlignment(Qt.AlignmentFlag.AlignCenter)
        tip.setStyleSheet('color: #bbbbbb; font-size: 14px; background: transparent;'
                          'padding: 50px 0; border: none;')
        self.list_layout.addWidget(tip)
        self.list_layout.addStretch(1)
        self.count_label.setText('0 条')

    def _render_rows(self, rows):
        while self.list_layout.count():
            item = self.list_layout.takeAt(0)
            w = item.widget()
            if w:
                w.deleteLater()
        recs = [self._parse_rec(r) for r in rows]
        # 时间降序（最新在最上面，对标 parseRecordList 排序）
        recs.sort(key=lambda r: wx_codec.parse_time_to_ts(f'{r["date"]} {r["time_part"]}'),
                  reverse=True)
        if not recs:
            self._render_empty('暂无记录')
            return
        recs[-1]['last'] = True
        for rec in recs:
            if rec['tappable']:
                rec['lat'], rec['lng'] = self._extract_coord(rec['lorastr'])
            card = _RecordCard(rec)
            card.installEventFilter(self)   # 卡片上捕获下拉拖拽
            card.clicked.connect(self._on_card_click)
            self.list_layout.addWidget(card)
        self.list_layout.addStretch(1)
        # 末尾状态提示（对标 load-more-tip / empty-tip）
        tab = self._cur[self.active]
        tip_text = None
        if not tab['has_more'] and tab['rows']:
            tip_text = '— 没有更多了 —'
        elif self._loading and tab['page'] > 1:
            tip_text = '加载中...'
        if tip_text:
            tip = QLabel(tip_text)
            tip.setAlignment(Qt.AlignmentFlag.AlignCenter)
            tip.setStyleSheet('color: #999999; font-size: 13px; background: transparent;'
                              'padding: 12px 0; border: none;')
            self.list_layout.addWidget(tip)
        self.count_label.setText(f'{len(recs)} 条')
        # 内容不足一屏时滚到底事件永远不会触发 → 主动补拉下一页直到铺满或没有更多
        QTimer.singleShot(0, self._fill_if_short)

    def _fill_if_short(self):
        if self._loading or self._refreshing or not self.api.logged_in:
            return
        tab = self._cur[self.active]
        if not tab['loaded'] or not tab['has_more']:
            return
        if self._scroll.verticalScrollBar().maximum() == 0:
            self._load(self.active, tab['page'] + 1)

    @staticmethod
    def _extract_coord(lorastr):
        """定位/跟踪 lorastr → (lat, lng)；无效返回 (None, None)"""
        segs = str(lorastr).split('|')
        if len(segs) < 3 or not segs[2]:
            return None, None
        parts = segs[2].replace('，', ',').split(',')
        if len(parts) < 2:
            return None, None
        try:
            lat, lng = float(parts[0]), float(parts[1])
        except ValueError:
            return None, None
        if lat == 0 and lng == 0:
            return None, None
        return lat, lng

    def _on_card_click(self, rec):
        # 拖拽刷新的释放不当作点击（与下拉刷新互斥）
        if self._pull.get('suppress_click'):
            self._pull['suppress_click'] = False
            return
        if rec['lat'] is None:
            self.notify('该记录无有效坐标')
            return
        self.locate_requested.emit(rec)


# ==================== 电量走势 ====================

class _BatteryChart(QWidget):
    """极简电量曲线（QPainter 自绘）"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.points = []  # [(ts, percent)] 升序
        self.setMinimumHeight(180)

    def set_points(self, points):
        self.points = sorted(points, key=lambda p: p[0])
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        painter.fillRect(self.rect(), QColor(theme.BG))
        if len(self.points) < 2:
            painter.setPen(QPen(QColor(theme.TEXT_MUTED)))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "数据不足")
            painter.end()
            return
        margin = 30
        t0, t1 = self.points[0][0], self.points[-1][0]
        if t1 <= t0:
            t1 = t0 + 1
        # 网格线
        painter.setPen(QPen(QColor(theme.DIVIDER), 1))
        for pct in (0, 25, 50, 75, 100):
            y = h - margin - (h - 2 * margin) * pct / 100
            painter.drawLine(margin, int(y), w - margin, int(y))
            painter.setPen(QPen(QColor(theme.TEXT_MUTED)))
            painter.drawText(2, int(y) + 4, f"{pct}")
            painter.setPen(QPen(QColor(theme.DIVIDER), 1))
        # 曲线
        path = QPainterPath()
        for i, (ts, pct) in enumerate(self.points):
            x = margin + (w - 2 * margin) * (ts - t0) / (t1 - t0)
            y = h - margin - (h - 2 * margin) * max(0, min(100, pct)) / 100
            if i == 0:
                path.moveTo(x, y)
            else:
                path.lineTo(x, y)
        painter.setPen(QPen(QColor(theme.PRIMARY), 2))
        painter.drawPath(path)
        # 换电检测：回升 ≥15 个百分点
        painter.setPen(QPen(QColor("#DC2626"), 1))
        for i in range(1, len(self.points)):
            if self.points[i][1] - self.points[i - 1][1] >= 15:
                ts, pct = self.points[i]
                x = margin + (w - 2 * margin) * (ts - t0) / (t1 - t0)
                y = h - margin - (h - 2 * margin) * max(0, min(100, pct)) / 100
                painter.drawEllipse(int(x) - 5, int(y) - 5, 10, 10)
        painter.end()


class BatteryPage(QWidget):
    """电量走势：分页拉对时记录，解析电量画曲线（对标 battery-chart.js）"""

    def __init__(self, api: WxApi, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[电量]', msg))
        self._device_rows = []
        self._setup_ui()
        self.refresh_devices()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(8)

        top = QHBoxLayout()
        self.device_combo = QComboBox()
        self.device_combo.setStyleSheet(
            f"QComboBox {{ border: 1px solid {theme.CARD_BORDER}; border-radius: 6px; padding: 4px 8px; }}")
        load_btn = QPushButton("加载电量")
        load_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        load_btn.clicked.connect(self._load)
        top.addWidget(self.device_combo, stretch=1)
        top.addWidget(load_btn)
        layout.addLayout(top)

        self.chart = _BatteryChart()
        layout.addWidget(self.chart, stretch=2)

        self.table = QTableWidget(0, 3)
        self.table.setHorizontalHeaderLabels(["时间", "电量%", "原始记录"])
        self.table.setStyleSheet(theme.TABLE_QSS)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.verticalHeader().setVisible(False)
        h = self.table.horizontalHeader()
        h.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        h.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        h.setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
        layout.addWidget(self.table, stretch=1)

        self.status_label = QLabel("红圈 = 检测到换电（回升≥15%）")
        self.status_label.setFont(theme.font(9))
        self.status_label.setStyleSheet(f"color: {theme.TEXT_MUTED};")
        layout.addWidget(self.status_label)

    def refresh_devices(self):
        if not self.api.logged_in:
            return
        self.api.run_async(
            lambda: self.api.getDeviceList(),
            self._on_devices, lambda m: self.notify(f"设备加载失败: {m}"),
        )

    def _on_devices(self, rows):
        self._device_rows = rows
        current = self.device_combo.currentData()
        self.device_combo.clear()
        for row in rows:
            device_id = str(row.get('deviceId', ''))
            rename = row.get('rename', '')
            self.device_combo.addItem(f"{device_id}" + (f" ({rename})" if rename else ''), device_id)
        if current:
            idx = self.device_combo.findData(current)
            if idx >= 0:
                self.device_combo.setCurrentIndex(idx)

    def _load(self):
        device_id = self.device_combo.currentData()
        if not device_id:
            self.notify("请选择设备")
            return
        self.notify("加载中（对时记录最多2000条）...")
        def load():
            records = []
            for offset in range(0, 2000, 100):
                batch = self.api.getDeviceLogbyId(device_id, limit=100,
                                                      offset=offset, log_type=2)
                records.extend(batch)
                if len(batch) < 100:
                    break
            return records
        self.api.run_async(load, self._on_loaded, lambda m: self.notify(f"加载失败: {m}"))

    def _on_loaded(self, records):
        points = []
        rows = []
        for row in records:
            ts = wx_codec.parse_time_to_ts(row.get('time', ''))
            battery = wx_codec.lorastr_battery(row.get('lorastr', ''))
            if battery is None:
                continue
            points.append((ts, battery))
            rows.append((ts, battery, row.get('lorastr', '')))
        rows.sort(key=lambda r: r[0])
        points.sort(key=lambda p: p[0])
        self.chart.set_points(points)
        self.table.setRowCount(len(rows))
        for i, (ts, battery, raw) in enumerate(rows):
            values = [datetime.fromtimestamp(ts).strftime('%m-%d %H:%M'),
                      f"{int(battery)}", raw]
            for col, text in enumerate(values):
                item = QTableWidgetItem(text)
                item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                self.table.setItem(i, col, item)
        self.notify(f"电量记录: 共 {len(points)} 条")
