# -*- coding: utf-8 -*-
"""云端记录页（对标小程序 pages/cloud-records）：卡片式记录列表，三个类型独立分页
电量走势页（对标小程序 pages/battery-chart）：对时记录取电量画曲线
"""

import calendar
import colorsys
import re
import time
from datetime import datetime

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                             QComboBox, QScrollArea)
from PyQt6.QtGui import (QPainter, QPen, QColor, QPainterPath, QBrush,
                         QLinearGradient)
from PyQt6.QtCore import Qt, QRectF, QPointF, pyqtSignal, QEvent, QTimer

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

# 可选展示点数（对标 LIMIT_OPTIONS；800 需要密码）
_LIMIT_OPTIONS = [10, 30, 50, 200, 400, 800]
_SECRET_LIMIT = 800
_SECRET_PASSWORD = '1234'
_PAGE_SIZE = 100     # 每页拉取条数
_MAX_PAGES = 20      # 最多翻 20 页（2000 条原始记录，凑够点数提前停）
_JUMP_DELTA = 15     # 电量回升 ≥15 个百分点计为换电/重新上电
_MIN_VALID = 0.05    # 忽略 <5% 的无效读数


def _parse_battery_value(v):
    """电量归一：0~1 小数或 0~100 百分比 → 0~1，无效返回 None（对标 parseBatteryValue）"""
    if v is None or v == '':
        return None
    try:
        n = float(v)
    except (TypeError, ValueError):
        return None
    if n <= 0:
        return None
    return n / 100 if n > 1 else n


def _parse_wall_time(s):
    """时间字符串 → 毫秒（墙面时间口径，与 mini 的 Date.UTC 编码同口径）；失败返回 0"""
    m = re.match(r'(\d{4})[/-](\d{1,2})[/-](\d{1,2})[ T](\d{1,2}):(\d{1,2})(?::(\d{1,2}))?',
                 str(s or ''))
    if not m:
        return 0
    y, mo, d, h, mi, se = (int(g or 0) for g in m.groups())
    try:
        return calendar.timegm((y, mo, d, h, mi, se, 0, 0, 0)) * 1000
    except (ValueError, OverflowError):
        return 0


def _fmt_chart_time(t_ms):
    """M/D HH:MM——对时记录时间戳是"设备把北京时间墙面时间直接编码成的 Unix 秒"，
    必须用 UTC 方法显示（对标 _fmtTime，本地时区会多加 8 小时）"""
    d = time.gmtime(t_ms / 1000)
    return f'{d.tm_mon}/{d.tm_mday} {d.tm_hour:02d}:{d.tm_min:02d}'


def _fmt_span(ms):
    if not ms or ms < 0:
        return '—'
    minutes = ms / 60000
    if minutes < 60:
        return f'{round(minutes)}分钟'
    hours = ms / 3600000
    if hours < 24:
        return f'{round(hours, 1)}小时'
    return f'{round(hours / 24, 1)}天'


def _fmt_delta_sec(sec):
    """偏差文案：<60 秒显示秒，否则分（对标 fmtDelta）"""
    return f'{round(sec * 10) / 10}秒' if sec < 60 else f'{round(sec / 60 * 10) / 10}分'


class _BatteryChart(QWidget):
    """电量走势图（对标 _drawChart：0~100% 网格 + 绿线渐变填充 + 换电橙圈 + 悬停联动）"""
    hover_changed = pyqtSignal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.points = []      # [{'t','pct','delta','time_str'}] 时间升序
        self.hover = -1
        self.setMinimumHeight(230)
        self.setMouseTracking(True)

    def set_points(self, pts):
        self.points = pts
        self.hover = -1
        self.update()

    def set_hover(self, idx):
        if idx != self.hover:
            self.hover = idx
            self.update()

    def mouseMoveEvent(self, ev):
        if len(self.points) < 2:
            return
        xs = self._xs()
        x = ev.position().x()
        best, best_d = 0, 1e18
        for i, cx in enumerate(xs):
            if abs(cx - x) < best_d:
                best_d, best = abs(cx - x), i
        self.hover_changed.emit(best)

    def leaveEvent(self, event):
        self.hover_changed.emit(-1)
        super().leaveEvent(event)

    def _xs(self):
        pad_l, pad_r = 42, 14
        plot_w = self.width() - pad_l - pad_r
        t0, t1 = self.points[0]['t'], self.points[-1]['t']
        span = (t1 - t0) or 1
        single = len(self.points) == 1
        return [pad_l + (plot_w / 2 if single else (p['t'] - t0) / span * plot_w)
                for p in self.points]

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        painter.fillRect(self.rect(), QColor('#FFFFFF'))
        pts = self.points
        if len(pts) < 2:
            painter.setPen(QPen(QColor('#bbbbbb')))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, '数据不足')
            painter.end()
            return

        pad_l, pad_r, pad_t, pad_b = 42, 14, 26, 30
        plot_w, plot_h = w - pad_l - pad_r, h - pad_t - pad_b
        xs = self._xs()
        ys = [pad_t + plot_h - max(0.0, min(100.0, p['pct'])) / 100 * plot_h
              for p in pts]

        # 网格 + Y 轴标签（固定 0~100%，不随数据区间缩放）
        painter.setFont(theme.font(10))
        for i in range(5):
            y = pad_t + plot_h - plot_h * i / 4
            painter.setPen(QPen(QColor('#eeeeee'), 1))
            painter.drawLine(pad_l, int(y), pad_l + plot_w, int(y))
            painter.setPen(QPen(QColor('#999999'), 1))
            painter.drawText(2, int(y) + 4, f'{25 * i}%')

        # 折线下方渐变填充
        grad = QLinearGradient(0, pad_t, 0, pad_t + plot_h)
        grad.setColorAt(0, QColor(7, 193, 96, 71))
        grad.setColorAt(1, QColor(7, 193, 96, 5))
        fill = QPainterPath()
        fill.moveTo(xs[0], pad_t + plot_h)
        for i in range(len(xs)):
            fill.lineTo(xs[i], ys[i])
        fill.lineTo(xs[-1], pad_t + plot_h)
        fill.closeSubpath()
        painter.fillPath(fill, QBrush(grad))

        # 折线
        path = QPainterPath()
        path.moveTo(xs[0], ys[0])
        for i in range(1, len(xs)):
            path.lineTo(xs[i], ys[i])
        painter.setPen(QPen(QColor('#07c160'), 2, Qt.PenStyle.SolidLine,
                            Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
        painter.drawPath(path)

        # 数据点（点数不多时才画，避免糊成一片）
        if len(pts) <= 80:
            painter.setBrush(QColor('#FFFFFF'))
            painter.setPen(QPen(QColor('#07c160'), 1.5))
            for i in range(len(xs)):
                painter.drawEllipse(QPointF(xs[i], ys[i]), 2.4, 2.4)

        # 换电/回升标记（橙色圆圈）
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(QPen(QColor('#ff9500'), 1.5))
        for i in range(1, len(pts)):
            if pts[i]['pct'] - pts[i - 1]['pct'] >= _JUMP_DELTA:
                painter.drawEllipse(QPointF(xs[i], ys[i]), 5, 5)

        # 图例
        painter.setPen(QPen(QColor('#07c160'), 2))
        painter.drawLine(pad_l + 4, pad_t - 11, pad_l + 22, pad_t - 11)
        painter.setPen(QPen(QColor('#666666'), 1))
        painter.drawText(QPointF(pad_l + 26, pad_t - 7), '电量%')

        # X 轴时间标签（首、中、尾）
        painter.setPen(QPen(QColor('#999999'), 1))
        label_y = pad_t + plot_h + 16
        if len(pts) == 1:
            painter.drawText(QPointF(xs[0], label_y), pts[0]['time_str'])
        else:
            painter.drawText(QPointF(pad_l, label_y), pts[0]['time_str'])
            mid = (len(pts) - 1) // 2
            tw = painter.fontMetrics().horizontalAdvance(pts[mid]['time_str'])
            painter.drawText(QPointF(xs[mid] - tw / 2, label_y), pts[mid]['time_str'])
            tw = painter.fontMetrics().horizontalAdvance(pts[-1]['time_str'])
            painter.drawText(QPointF(pad_l + plot_w - tw, label_y), pts[-1]['time_str'])

        # 悬停指示（虚线竖线 + 蓝点）
        if 0 <= self.hover < len(xs):
            painter.setPen(QPen(QColor(25, 137, 250, 153), 1, Qt.PenStyle.DashLine))
            painter.drawLine(int(xs[self.hover]), pad_t,
                             int(xs[self.hover]), pad_t + plot_h)
            painter.setBrush(QColor('#1989fa'))
            painter.setPen(QPen(QColor('#FFFFFF'), 1.5))
            painter.drawEllipse(QPointF(xs[self.hover], ys[self.hover]), 4.5, 4.5)
        painter.end()


class _DeltaChart(QWidget):
    """时间偏差图（对标 _drawDeltaChart：紫虚线波浪，高度=√(偏差/最大偏差)）"""
    hover_changed = pyqtSignal(int)

    _SECS = [5, 10, 20, 30, 60, 120, 300, 600, 1800, 3600, 7200, 14400, 86400]

    def __init__(self, parent=None):
        super().__init__(parent)
        self.points = []
        self.hover = -1
        self.setMinimumHeight(120)
        self.setMouseTracking(True)

    def set_points(self, pts):
        self.points = pts
        self.hover = -1
        self.update()

    def set_hover(self, idx):
        if idx != self.hover:
            self.hover = idx
            self.update()

    def mouseMoveEvent(self, ev):
        if len(self.points) < 2:
            return
        xs = self._xs()
        x = ev.position().x()
        best, best_d = 0, 1e18
        for i, cx in enumerate(xs):
            if abs(cx - x) < best_d:
                best_d, best = abs(cx - x), i
        self.hover_changed.emit(best)

    def leaveEvent(self, event):
        self.hover_changed.emit(-1)
        super().leaveEvent(event)

    def _xs(self):
        pad_l, pad_r = 42, 46
        plot_w = self.width() - pad_l - pad_r
        t0, t1 = self.points[0]['t'], self.points[-1]['t']
        span = (t1 - t0) or 1
        single = len(self.points) == 1
        return [pad_l + (plot_w / 2 if single else (p['t'] - t0) / span * plot_w)
                for p in self.points]

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        painter.fillRect(self.rect(), QColor('#FFFFFF'))
        pts = self.points
        if len(pts) < 2:
            painter.setPen(QPen(QColor('#bbbbbb')))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, '数据不足')
            painter.end()
            return

        pad_l, pad_r, pad_t, pad_b = 42, 46, 14, 24
        plot_w, plot_h = w - pad_l - pad_r, h - pad_t - pad_b

        # 量程：最大偏差向上取整档（秒），高度开方压缩（小偏差也有可见起伏）
        d_max_ms = max(p['delta'] for p in pts)
        d_max_s = d_max_ms / 1000
        d_top_s = self._SECS[-1]
        for s in self._SECS:
            if d_max_s <= s:
                d_top_s = s
                break

        def frac(ms):
            s = (ms or 0) / 1000
            return (min(s / d_top_s, 1.0) ** 0.5) if s > 0 else 0.0

        xs = self._xs()
        base_y = pad_t + plot_h

        # 基线（偏差=0）
        painter.setPen(QPen(QColor('#eeeeee'), 1))
        painter.drawLine(pad_l, int(base_y), pad_l + plot_w, int(base_y))

        # 偏差波浪线（紫虚线）
        painter.setPen(QPen(QColor(142, 36, 170, 217), 1.5, Qt.PenStyle.DashLine))
        path = QPainterPath()
        path.moveTo(xs[0], base_y - frac(pts[0]['delta']) * plot_h)
        for i in range(1, len(xs)):
            path.lineTo(xs[i], base_y - frac(pts[i]['delta']) * plot_h)
        painter.drawPath(path)

        # 右侧刻度（0 / 半程 / 满程）
        painter.setFont(theme.font(10))
        painter.setPen(QPen(QColor('#8E24AA'), 1))
        painter.drawText(QPointF(pad_l + plot_w + 4, base_y), '0')
        painter.drawText(QPointF(pad_l + plot_w + 4, pad_t + plot_h / 2),
                         _fmt_delta_sec(d_top_s / 2))
        painter.drawText(QPointF(pad_l + plot_w + 4, pad_t + 4),
                         _fmt_delta_sec(d_top_s))

        # X 轴时间标签（首、尾）
        painter.setPen(QPen(QColor('#999999'), 1))
        painter.drawText(QPointF(pad_l, base_y + 16), pts[0]['time_str'])
        if len(pts) > 1:
            tw = painter.fontMetrics().horizontalAdvance(pts[-1]['time_str'])
            painter.drawText(QPointF(pad_l + plot_w - tw, base_y + 16),
                             pts[-1]['time_str'])

        # 悬停指示（虚线竖线 + 紫点）
        if 0 <= self.hover < len(xs):
            painter.setPen(QPen(QColor(25, 137, 250, 153), 1, Qt.PenStyle.DashLine))
            painter.drawLine(int(xs[self.hover]), pad_t,
                             int(xs[self.hover]), int(base_y))
            painter.setBrush(QColor('#8E24AA'))
            painter.setPen(QPen(QColor('#FFFFFF'), 1.5))
            painter.drawEllipse(QPointF(xs[self.hover],
                                        base_y - frac(pts[self.hover]['delta']) * plot_h),
                                4, 4)
        painter.end()


class BatteryPage(QWidget):
    """电量走势（对标 pages/battery-chart）：翻页拉对时记录取电量，走势+时间偏差双图"""

    def __init__(self, api: WxApi, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[电量]', msg))
        self._preset_device_id = ''
        self._need = 10
        self._loading = False
        self._setup_ui()

    # ---------- UI ----------

    def _setup_ui(self):
        self.setStyleSheet('background: #f5f5f5;')
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet(theme.SCROLLBAR_QSS)
        outer.addWidget(scroll)
        body = QWidget()
        body.setObjectName('batBody')
        body.setStyleSheet('QWidget#batBody { background: #f5f5f5; }')
        scroll.setWidget(body)
        layout = QVBoxLayout(body)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(10)

        # 控制面板：设备 + 条数档位 + 刷新（对标 ctrl-panel）
        ctrl = QWidget()
        ctrl.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        ctrl.setStyleSheet('background: #FFFFFF; border-radius: 8px;')
        cv = QVBoxLayout(ctrl)
        cv.setContentsMargins(12, 10, 12, 10)
        cv.setSpacing(8)
        row1 = QHBoxLayout()
        row1.addWidget(self._ctrl_label('🔋 设备'))
        self.device_combo = QComboBox()
        self.device_combo.setStyleSheet(
            'QComboBox { border: 1px solid #dddddd; border-radius: 6px; padding: 4px 8px;'
            ' font-size: 12px; background: #fff; }')
        self.device_combo.currentIndexChanged.connect(self._on_device_changed)
        row1.addWidget(self.device_combo, stretch=1)
        cv.addLayout(row1)
        row2 = QHBoxLayout()
        row2.addWidget(self._ctrl_label('📊 条数'))
        self._limit_chips = {}
        for n in _LIMIT_OPTIONS:
            chip = QPushButton(str(n))
            chip.setCheckable(True)
            chip.setFixedHeight(22)
            chip.setCursor(Qt.CursorShape.PointingHandCursor)
            chip.clicked.connect(lambda _, n=n: self._on_limit(n))
            row2.addWidget(chip)
            self._limit_chips[n] = chip
        row2.addStretch()
        refresh_btn = QPushButton('🔄 刷新')
        refresh_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        refresh_btn.clicked.connect(self._load)
        row2.addWidget(refresh_btn)
        cv.addLayout(row2)
        layout.addWidget(ctrl)
        self._apply_limit_styles()

        # 状态条（加载中 / 空态 / 错误，对标 state-box）
        self.state_label = QLabel('')
        self.state_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.state_label.setWordWrap(True)
        self.state_label.setStyleSheet('color: #bbbbbb; font-size: 13px;'
                                       'padding: 30px 0; border: none;'
                                       'background: transparent;')
        self.state_label.hide()
        layout.addWidget(self.state_label)

        # 走势图面板（对标 chart-panel）
        self.chart_panel, chart_v, chart_head = self._make_panel('📈 电量走势')
        self.chart_count = self._count_label()
        chart_head.addWidget(self.chart_count)
        self.hover_label = self._hover_label()
        chart_v.addWidget(self.hover_label)
        self.chart = _BatteryChart()
        self.chart.hover_changed.connect(self._on_hover)
        chart_v.addWidget(self.chart)
        layout.addWidget(self.chart_panel)

        # 时间偏差独立面板（对标 delta-panel）
        self.delta_panel, delta_v, delta_head = self._make_panel('⏱ 时间偏差（TIME vs 设备时间）')
        self.delta_count = self._count_label()
        delta_head.addWidget(self.delta_count)
        self.delta_chart = _DeltaChart()
        self.delta_chart.hover_changed.connect(self._on_hover)
        delta_v.addWidget(self.delta_chart)
        layout.addWidget(self.delta_panel)

        # 说明面板（对标 tip-panel）
        tips = QWidget()
        tips.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        tips.setObjectName('batTips')
        tips.setStyleSheet('QWidget#batTips { background: #FFFFFF; border-radius: 8px; }')
        tv = QVBoxLayout(tips)
        tv.setContentsMargins(12, 10, 12, 10)
        tv.setSpacing(4)
        self.tip_span = self._tip_label('')
        self.tip_jump = self._tip_label('🟠 橙色圆圈 = 电量回升≥15%（疑似换电/重新上电）')
        self.tip_delta = self._tip_label('紫虚线 = TIME 与设备时间偏差（按最大偏差开方压缩）；'
                                         '偏差超过上报周期一半的点已剔除')
        for lb in (self.tip_span, self.tip_jump, self.tip_delta):
            tv.addWidget(lb)
        layout.addWidget(tips)
        self.tips_panel = tips
        layout.addStretch(1)

    @staticmethod
    def _ctrl_label(text):
        lb = QLabel(text)
        lb.setStyleSheet('color: #555555; font-size: 13px; border: none;'
                         'background: transparent;')
        return lb

    @staticmethod
    def _count_label():
        lb = QLabel('')
        lb.setStyleSheet('color: #999999; font-size: 11px; border: none;'
                         'background: transparent;')
        return lb

    @staticmethod
    def _hover_label():
        lb = QLabel('鼠标移入图表查看某点详情')
        lb.setStyleSheet('color: #1989fa; font-size: 11px; background: #eef5ff;'
                         'border-radius: 4px; padding: 3px 8px; border: none;')
        lb.hide()
        return lb

    @staticmethod
    def _tip_label(text):
        lb = QLabel(text)
        lb.setWordWrap(True)
        lb.setStyleSheet('color: #999999; font-size: 11px; border: none;'
                         'background: transparent;')
        return lb

    def _make_panel(self, title):
        panel = QWidget()
        panel.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        panel.setStyleSheet('background: #FFFFFF; border-radius: 8px;')
        v = QVBoxLayout(panel)
        v.setContentsMargins(12, 10, 12, 10)
        v.setSpacing(6)
        head = QHBoxLayout()
        t = QLabel(title)
        t.setStyleSheet('color: #333333; font-size: 13px; font-weight: 600;'
                        'border: none; background: transparent;')
        head.addWidget(t)
        head.addStretch()
        v.addLayout(head)
        return panel, v, head

    def _apply_limit_styles(self):
        for n, chip in self._limit_chips.items():
            if n == self._need:
                chip.setStyleSheet('QPushButton { background: #1989fa; color: #fff;'
                                   'border: 1px solid #1989fa; border-radius: 10px;'
                                   'padding: 2px 9px; font-size: 11px; }')
            else:
                chip.setStyleSheet('QPushButton { background: #fff; color: #666666;'
                                   'border: 1px solid #dddddd; border-radius: 10px;'
                                   'padding: 2px 9px; font-size: 11px; }'
                                   'QPushButton:hover { color: #1989fa; }')

    # ---------- 交互 ----------

    def _on_limit(self, n):
        if n == self._need:
            return
        if n == _SECRET_LIMIT:
            from PyQt6.QtWidgets import QInputDialog, QLineEdit
            text, ok = QInputDialog.getText(self, '需要密码',
                                            f'查询 {n} 条会明显增加服务器压力，请输入密码：',
                                            QLineEdit.EchoMode.Password)
            if not ok:
                return
            if text.strip() != _SECRET_PASSWORD:
                self.notify('密码错误')
                return
        self._need = n
        self._apply_limit_styles()
        self._load()

    def _on_device_changed(self, _idx):
        if not self._loading:
            self._load()

    def _on_hover(self, idx):
        """两图联动高亮 + 顶部提示文字（对标 _setHover）"""
        self.chart.set_hover(idx)
        self.delta_chart.set_hover(idx)
        pts = self.chart.points
        if 0 <= idx < len(pts):
            p = pts[idx]
            self.hover_label.setText(
                f"{p['time_str']}  {p['pct']}%  偏差{_fmt_delta_sec(p['delta'] / 1000)}")
            self.hover_label.show()
        else:
            self.hover_label.hide()

    # ---------- 数据 ----------

    def refresh_devices(self, preset_id=None):
        """拉设备列表（preset_id=设备详情带过来的设备，自动选中并查询）"""
        if not self.api.logged_in:
            return
        if preset_id:
            self._preset_device_id = str(preset_id)
        self.api.run_async(
            lambda: self.api.getDeviceList(),
            self._on_devices, lambda m: self.notify(f'设备加载失败: {m}'),
        )

    def _on_devices(self, rows):
        rows = sorted(rows or [], key=lambda r: str(r.get('deviceId', '')))
        current = self.device_combo.currentData()
        self.device_combo.blockSignals(True)
        self.device_combo.clear()
        for row in rows:
            device_id = str(row.get('deviceId', ''))
            if not device_id or device_id == '-':
                continue
            rename = str(row.get('rename', '') or '')
            self.device_combo.addItem(f'{device_id}（{rename}）' if rename else device_id,
                                      device_id)
        # 预选：① 详情页带过来的设备（不在列表则置顶保证能查）② 保留当前选择
        idx = -1
        if self._preset_device_id:
            idx = self.device_combo.findData(self._preset_device_id)
            if idx < 0:
                self.device_combo.insertItem(0, self._preset_device_id,
                                             self._preset_device_id)
                idx = 0
        if idx < 0 and current:
            idx = self.device_combo.findData(current)
        if idx >= 0:
            self.device_combo.setCurrentIndex(idx)
        self.device_combo.blockSignals(False)
        self._load()   # 对标 _loadDevices 成功回调 → _query 自动查询

    def _set_loading(self, on):
        self._loading = on
        if on:
            self.state_label.setText('⏳ 加载中...')
            self.state_label.show()
            self.chart_panel.hide()
            self.delta_panel.hide()
            self.tips_panel.hide()
        else:
            self.state_label.hide()

    def _load(self):
        device_id = self.device_combo.currentData()
        if not device_id or self._loading:
            return
        need = self._need
        self._set_loading(True)

        def work():
            # 翻页拉取：每页 min(need,100) 条，最多 20 页，凑够点数/无更多提前停
            acc, raw_total = [], 0
            for page_i in range(_MAX_PAGES):
                limit = min(need, _PAGE_SIZE)
                raw = self.api.getDeviceLogbyId(device_id, limit=limit,
                                                offset=page_i * limit)
                raw_total += len(raw)
                acc.extend(self._extract_points(raw, device_id))
                if len(raw) < limit or len(acc) >= need:
                    break
            interval_min = 30
            try:
                ri = int(float(self.api.getDeviceConfigById(device_id)
                               .get('reportIntervalMin', 0) or 0))
                if ri > 0:
                    interval_min = ri
            except Exception:
                pass
            return self._finish_points(acc, raw_total, need, interval_min)

        self.api.run_async(
            work, self._on_data,
            lambda m: (self._set_loading(False), self.notify(f'加载失败: {m}')),
        )

    @staticmethod
    def _extract_points(records, device_id):
        """从原始记录提取电量点：只认 type=2 对时记录（对标 _extractPoints）"""
        out = []
        for row in records:
            lorastr = str(row.get('lorastr', '') or '')
            if not lorastr.startswith('2|'):
                continue
            parts = lorastr.split('|')
            # type=2 记录的实际设备在 parts[1]（中继转发时行 deviceId 是中继）
            inner = parts[1] if len(parts) > 1 else ''
            rec_device = str(row.get('deviceId', '') or '')
            match = inner or rec_device
            if device_id and match and match != device_id:
                continue
            # 时间优先 lorastr 内时间戳（设备时间），回退服务器接收时间；记录两者偏差
            t = 0
            if len(parts) >= 3:
                try:
                    n = int(parts[2])
                    if n > 0:
                        t = n * 1000 if n < 1e10 else n
                except ValueError:
                    t = 0
            time_ms = _parse_wall_time(row.get('time', ''))
            delta = abs(t - time_ms) if (t > 0 and time_ms > 0) else 0
            if not t and time_ms > 0:
                t = time_ms
            if not t:
                continue
            # 电量在 parts[3]，行属性 battery 兜底
            b = _parse_battery_value(parts[3]) if len(parts) >= 4 else None
            if b is None:
                b = _parse_battery_value(row.get('battery'))
            if b is None or b < _MIN_VALID:
                continue
            out.append({'t': t, 'pct': round(b * 1000) / 10, 'delta': delta})
        return out

    @staticmethod
    def _filter_abnormal_time(pts):
        """LORA 失真时间过滤：①绝对范围 2020-01-01~now+2天 ②月份跨度收敛到中位数锚点±1月"""
        if len(pts) < 3:
            return pts
        min_t = calendar.timegm((2020, 1, 1, 0, 0, 0, 0, 0, 0)) * 1000
        max_t = time.time() * 1000 + 2 * 86400000
        kept = [p for p in pts if min_t <= p['t'] <= max_t]
        if len(kept) >= 3:
            mid = sorted(kept, key=lambda p: p['t'])[len(kept) // 2]
            g = time.gmtime(mid['t'] / 1000)
            anchor = g.tm_year * 12 + g.tm_mon - 1

            def month_idx(t):
                gg = time.gmtime(t / 1000)
                return gg.tm_year * 12 + gg.tm_mon - 1

            converged = [p for p in kept if abs(month_idx(p['t']) - anchor) <= 1]
            if len(converged) >= 2:   # 收敛后至少剩 2 点才采用，否则退回绝对范围过滤
                return converged
        return kept

    @staticmethod
    def _finish_points(all_points, raw_total, need, interval_min):
        """收尾：去重 → 升序 → 异常时间过滤 → 偏差超限剔除 → 取最新 need 条"""
        seen = set()
        pts = []
        for p in all_points:
            if p['t'] in seen:
                continue
            seen.add(p['t'])
            pts.append(p)
        pts.sort(key=lambda p: p['t'])
        pts = BatteryPage._filter_abnormal_time(pts)
        delta_limit = interval_min * 60000 / 2
        pts = [p for p in pts if p['delta'] <= delta_limit]
        if len(pts) > need:
            pts = pts[-need:]
        for p in pts:
            p['time_str'] = _fmt_chart_time(p['t'])
        return {'raw': raw_total, 'need': need, 'points': pts}

    def _on_data(self, result):
        self._set_loading(False)
        pts = result['points']
        self.chart_panel.show()
        self.delta_panel.show()
        self.tips_panel.show()
        if not pts:
            self.chart.set_points([])
            self.delta_chart.set_points([])
            self.chart_count.setText('')
            self.delta_count.setText('')
            self.tip_span.setText('')
            self.state_label.setText(
                f'未找到该设备的对时电量记录（已扫描 {result["raw"]} 条）')
            self.state_label.show()
            return
        jump = sum(1 for i in range(1, len(pts))
                   if pts[i]['pct'] - pts[i - 1]['pct'] >= _JUMP_DELTA)
        max_p = max(pts, key=lambda p: p['pct'])
        min_p = min(pts, key=lambda p: p['pct'])
        d_max_ms = max(p['delta'] for p in pts)
        self.chart_count.setText(f'{len(pts)} 点 / 扫描 {result["raw"]} 条')
        self.delta_count.setText(f'最大 {_fmt_delta_sec(d_max_ms / 1000)}')
        self.tip_span.setText(
            f'时间跨度：{pts[0]["time_str"]} → {pts[-1]["time_str"]}'
            f'（{_fmt_span(pts[-1]["t"] - pts[0]["t"])}），'
            f'换电 {jump} 次，最高 {round(max_p["pct"])}%，最低 {round(min_p["pct"])}%')
        self.chart.set_points(pts)
        self.delta_chart.set_points(pts)
        self.notify(f'电量走势: {len(pts)} 点 / 扫描 {result["raw"]} 条')
