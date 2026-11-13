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
from PyQt6.QtCore import Qt, QRectF, pyqtSignal

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
        self.setStyleSheet(f'background: {rec["bg"]};'
                           f'border-bottom: 1px solid rgba(0, 0, 0, 0.04);')
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

        # —— 行3：RSSI（左半） SNR（右半，与小程序两个 flex:1 一致）——
        if rec['rssi'] or rec['snr']:
            r3 = QHBoxLayout()
            r3.setSpacing(0)
            if rec['rssi']:
                rssi_lab = QLabel(f'RSSI: {rec["rssi"]}dBm')
                rssi_lab.setStyleSheet('color: #333333; font-size: 12px;'
                                       'border: none; background: transparent;')
                r3.addWidget(rssi_lab, stretch=1)
            if rec['snr']:
                snr_lab = QLabel(f'SNR: {rec["snr"]}')
                snr_lab.setStyleSheet('color: #333333; font-size: 12px;'
                                      'border: none; background: transparent;')
                r3.addWidget(snr_lab, stretch=1)
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
        self._tabs = {k: {'page': 0, 'rows': [], 'loaded': False}
                      for k in ('all', 'gps', 'sync')}
        self.active = 'all'
        self._setup_ui()

    def _setup_ui(self):
        self.setStyleSheet('background: #f5f5f5;')
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # ---- 头部：标题 + chips + 条数（对标 .recv-header）----
        header = QWidget()
        header.setStyleSheet('background: #FFFFFF; border-bottom: 1px solid #f0f0f0;'
                             'border-radius: 8px 8px 0 0;')
        h = QHBoxLayout(header)
        h.setContentsMargins(10, 8, 10, 8)
        h.setSpacing(8)
        title = QLabel('☁️ 云端记录')
        title.setStyleSheet('color: #555555; font-size: 14px; font-weight: 600;'
                            'border: none; background: transparent;')
        h.addWidget(title)
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
        self.list_layout = QVBoxLayout(panel)
        self.list_layout.setContentsMargins(0, 0, 0, 0)
        self.list_layout.setSpacing(0)
        scroll.setWidget(panel)
        layout.addWidget(scroll, stretch=1)

        # ---- 底部：页码 + 翻页 ----
        bottom = QHBoxLayout()
        bottom.setContentsMargins(14, 6, 14, 8)
        self.page_label = QLabel('第 1 页')
        self.page_label.setFont(theme.font(10))
        prev_btn = QPushButton('上一页')
        next_btn = QPushButton('下一页')
        for b in (prev_btn, next_btn):
            b.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        prev_btn.clicked.connect(lambda: self._turn_page(-1))
        next_btn.clicked.connect(lambda: self._turn_page(+1))
        bottom.addWidget(self.page_label)
        bottom.addStretch()
        bottom.addWidget(prev_btn)
        bottom.addWidget(next_btn)
        layout.addLayout(bottom)

        self._apply_chip_styles()

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

    def _on_chip(self, key):
        if key == self.active:
            return
        self.active = key
        self._apply_chip_styles()
        tab = self._tabs[key]
        if tab['loaded']:
            self._render()
        else:
            self._render_empty('⏳ 加载中...')
            self._load(key, 1)

    def _turn_page(self, delta):
        tab = self._tabs[self.active]
        page = max(1, tab['page'] + delta)
        self._load(self.active, page)

    def _ensure_loaded(self, tab_idx=0):
        """首次进入自动拉"全部"第一页（leftpanel 调用）"""
        if not self._tabs['all']['loaded'] and not self._loading:
            self._load('all', 1)

    _loading = False

    def _load(self, key, page):
        if not self.api.logged_in:
            self.notify('请先登录')
            return
        codes = {'all': None, 'gps': 1, 'sync': 2}
        self._loading = True
        self.api.run_async(
            lambda: self.api.getlastlog(page=page, limit=10, log_type=codes[key]),
            on_ok=lambda rows, k=key, p=page: self._on_rows(k, p, rows),
            on_err=lambda m, k=key: (self._loading_done(),
                                     self.notify(f'加载失败: {m}')),
        )

    def _loading_done(self):
        self._loading = False

    def _on_rows(self, key, page, rows):
        self._loading = False
        if not rows and page > 1:
            self.notify('没有更多记录')
            return
        if not rows and page == 1:
            self._tabs[key].update({'page': 1, 'rows': [], 'loaded': True})
            if key == self.active:
                self._render()
            return
        # 翻页去重（同 rawTime+lorastr 不重复追加）
        if page > 1:
            exist = {(r.get('time'), r.get('lorastr')) for r in self._tabs[key]['rows']}
            rows = [r for r in rows if (r.get('time'), r.get('lorastr')) not in exist]
            self._tabs[key]['rows'].extend(rows)
        else:
            self._tabs[key]['rows'] = list(rows)
        self._tabs[key]['page'] = page
        self._tabs[key]['loaded'] = True
        # 首次拉数据时顺带构建 设备别名映射（对标 onLoad 的 renameMap）
        if not self._rename_map and self.api.logged_in:
            self.api.run_async(
                lambda: self.api.getDeviceList(),
                on_ok=self._on_devices_for_rename,
                on_err=lambda m: None,
            )
        if key == self.active:
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
        self._render_rows(self._tabs[self.active]['rows'])

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
        self.page_label.setText(f'第 {self._tabs[self.active]["page"] or 1} 页')

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
        for rec in recs:
            if rec['tappable']:
                rec['lat'], rec['lng'] = self._extract_coord(rec['lorastr'])
            card = _RecordCard(rec)
            card.clicked.connect(self._on_card_click)
            self.list_layout.addWidget(card)
        self.list_layout.addStretch(1)
        self.count_label.setText(f'{len(recs)} 条')
        self.page_label.setText(f'第 {self._tabs[self.active]["page"]} 页')

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
