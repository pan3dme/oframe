# -*- coding: utf-8 -*-
"""设备列表页 + 设备详情页
- DeviceListPage：设备列表（对标小程序 pages/device/device.js 单行紧凑模式）
  白色面板行：序号 + 信号格(活跃绿/休眠灰) + 设备名(别名淡灰/电量色) + 定位圆点/对时时钟
  + 徽标（延下次绿/超时红/未上报灰，每秒跳动）；分类chips 设备/中继/离线 + ＋新增设备
- DeviceDetailPage：设备详情首页（对标小程序 pages/index/index.js 设备详情Tab）
"""

import base64
import json
import os
import re
from datetime import datetime

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                             QLineEdit, QMessageBox, QInputDialog, QScrollArea,
                             QGridLayout, QSizePolicy, QFrame, QDialog)
from PyQt6.QtCore import Qt, pyqtSignal, QTimer, QEvent
from PyQt6.QtGui import QPixmap, QPainter, QColor, QPen

from crowui import theme, wx_codec
from crowui.wx_api import WxApi
from crowui import battery_swap

_GREEN = '#4caf50'
_GRAY = '#999999'
_RED = '#f44336'
_PURPLE = '#8E24AA'

# "最近一次电量"本地缓存（对标 dataCache.setCachedDeviceLastBattery）
_BATTERY_CACHE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                   'device_battery_cache.json')


def _load_battery_cache():
    try:
        with open(_BATTERY_CACHE_PATH, 'r', encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def _save_battery_cache(cache):
    try:
        with open(_BATTERY_CACHE_PATH, 'w', encoding='utf-8') as f:
            json.dump(cache, f, ensure_ascii=False)
    except OSError:
        pass


# "上次选中的设备"本地缓存（对标 dataCache.setHomeSelectedDevice，按账号区分）
_HOME_SELECTED_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                   'home_selected_cache.json')


def load_home_selected(account):
    """读取该账号上次选中的设备ID；无记录返回 ''"""
    try:
        with open(_HOME_SELECTED_PATH, 'r', encoding='utf-8') as f:
            data = json.load(f)
        return str(data.get(account or 'default', '') or '')
    except (OSError, ValueError):
        return ''


def save_home_selected(account, device_id):
    """记录该账号当前选中的设备ID（重启/重新登录后自动恢复）"""
    try:
        try:
            with open(_HOME_SELECTED_PATH, 'r', encoding='utf-8') as f:
                data = json.load(f)
        except (OSError, ValueError):
            data = {}
        data[account or 'default'] = device_id
        with open(_HOME_SELECTED_PATH, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False)
    except OSError:
        pass


# ==================== 小图标 ====================

class SignalBars(QWidget):
    """4 条竖线信号格（对标 .item-status-signal：活跃绿色/休眠灰色）"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(11, 10)
        self._color = QColor(_GRAY)

    def set_color(self, color):
        self._color = QColor(color)
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(self._color)
        heights = (3, 5, 7, 9)
        x = 0
        for h in heights:
            p.drawRoundedRect(x, 10 - h, 2, h, 1, 1)
            x += 4  # 2px 条 + 2px 间隔
        p.end()


class ClockIcon(QWidget):
    """对时类型时钟图标（对标 .item-type-icon-clock：圆框 + 两针，颜色跟随状态）"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(11, 11)
        self._color = QColor(_GRAY)

    def set_color(self, color):
        self._color = QColor(color)
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        pen = QPen(self._color, 1.5)
        p.setPen(pen)
        p.drawEllipse(1, 1, 9, 9)
        # 分针（向上偏左 -90°）与时针（向右）
        p.drawLine(5, 5, 5, 3)
        p.drawLine(5, 5, 8, 5)
        p.end()


def _make_dot(color, invalid=False):
    """定位/跟踪记录圆点：有效=实心 7px；无效(0,0)=空心 9px 描边 2px"""
    lab = QLabel()
    if invalid:
        lab.setFixedSize(9, 9)
        lab.setStyleSheet(f'background: transparent; border: 2px solid {color};'
                          f'border-radius: 4px;')
    else:
        lab.setFixedSize(7, 7)
        lab.setStyleSheet(f'background: {color}; border-radius: 3px;')
    return lab


# ==================== 设备列表页（Tab: 设备列表） ====================

class DeviceListPage(QWidget):
    """设备列表：分类chips(设备/中继/离线) + ＋新增设备 + 单行设备卡列表（每秒倒计时）"""

    detail_requested = pyqtSignal(dict)   # 携带设备行原始数据
    loaded = pyqtSignal()                 # 设备列表数据就绪（登录后恢复上次选中设备用）

    def __init__(self, api: WxApi, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[设备列表]', msg))
        self._device_rows = []      # 设备表原始数据（deviceId → row 映射用）
        self._row_map = {}
        self._sync_map = {}
        self._config_map = {}
        self._merged = []           # 合并后的展示数据
        self._battery_cache = _load_battery_cache()
        self.active_category = 'device'   # device / relay / offline
        self.device_online_only = False   # 再点"设备"切换 仅在线/全部
        self._setup_ui()

        # 每秒刷新倒计时徽标（对标 _startCountdownTimer）
        self._timer = QTimer(self)
        self._timer.setInterval(1000)
        self._timer.timeout.connect(self._tick)
        self._timer.start()

    # ==================== UI ====================

    def _setup_ui(self):
        self.setStyleSheet(f'background: #f5f5f5;')
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # ---- 固定头部：分类 chips + ＋新增设备（对标 .category-bar）----
        header = QWidget()
        header.setStyleSheet('background: #FFFFFF; border-bottom: 1px solid #f0f0f0;')
        h = QHBoxLayout(header)
        h.setContentsMargins(4, 7, 8, 7)
        h.setSpacing(5)
        self._chips = {}
        for cat, label in (('device', '设备'), ('relay', '中继'), ('offline', '离线')):
            chip = QPushButton(f'{label} 0')
            chip.setFixedHeight(30)
            chip.setCursor(Qt.CursorShape.PointingHandCursor)
            chip.clicked.connect(lambda _, c=cat: self._on_chip(c))
            h.addWidget(chip)
            self._chips[cat] = chip
        h.addStretch()
        self.add_btn = QPushButton('＋ 新增设备')
        self.add_btn.setFixedHeight(30)
        self.add_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.add_btn.setStyleSheet(f"""
            QPushButton {{ background: {theme.GREEN}; color: #fff; border: none;
                          border-radius: 5px; padding: 0 10px; font-size: 12px; font-weight: 600; }}
            QPushButton:hover {{ background: {theme.GREEN_HOVER}; }}
        """)
        self.add_btn.clicked.connect(self._on_add)
        h.addWidget(self.add_btn)
        layout.addWidget(header)

        # ---- 列表（白色圆角面板）----
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet(theme.SCROLLBAR_QSS)
        panel = QWidget()
        panel.setObjectName('listPanel')
        panel.setStyleSheet("""
            QWidget#listPanel { background: #FFFFFF; border-radius: 8px; }
        """)
        self.rows_layout = QVBoxLayout(panel)
        self.rows_layout.setContentsMargins(12, 4, 12, 8)
        self.rows_layout.setSpacing(0)
        scroll.setWidget(panel)
        layout.addWidget(scroll, stretch=1)
        self._scroll = scroll

        # ---- 下拉刷新（对标小程序 refresher-enabled）：鼠标在列表顶部按下下拉触发 ----
        self._pull = {'active': False, 'start_y': 0, 'dy': 0}
        self._refreshing = False
        self._pull_overlay = None
        self._spinner_timer = None
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
        # 行子控件会忽略/接受鼠标事件，需在 viewport、面板和每行上统一捕获拖拽
        scroll.viewport().installEventFilter(self)
        panel.installEventFilter(self)

        self._apply_chip_styles()

    # ==================== 下拉刷新 ====================

    _PULL_THRESHOLD = 60   # 下拉超过 60px 释放即刷新

    def eventFilter(self, obj, ev):
        t = ev.type()
        if t == QEvent.Type.MouseButtonPress and ev.button() == Qt.MouseButton.LeftButton:
            # 只有列表已在顶部时才允许下拉刷新（对标 scroll-view 在顶部下拉才出 refresher）
            self._pull['active'] = self._scroll.verticalScrollBar().value() == 0 \
                and not self._refreshing
            self._pull['start_y'] = ev.globalPosition().toPoint().y()
            self._pull['dy'] = 0
            self._pull['suppress_click'] = False   # 新按下时重置点击抑制
        elif t == QEvent.Type.MouseMove and self._pull['active']:
            dy = ev.globalPosition().toPoint().y() - self._pull['start_y']
            self._pull['dy'] = max(0, dy)
            self._update_pull_overlay()
        elif t == QEvent.Type.MouseButtonRelease and self._pull['active']:
            self._pull['active'] = False
            dy = self._pull['dy']
            self._pull['dy'] = 0
            # 拖拽过的释放不当作行点击（>10px 视为拖拽，对标小程序 tap 位移容差）
            self._pull['suppress_click'] = dy > 10
            if dy >= self._PULL_THRESHOLD and self.api.logged_in:
                self._show_refreshing()
                self.refresh()
            else:
                self._hide_pull_overlay()
        return super().eventFilter(obj, ev)

    def _update_pull_overlay(self):
        """拖拽中：指示器跟随 + 列表内容随手指下移（对标小程序 refresher 手感）"""
        dy = self._pull['dy']
        # 内容跟随下移（最多 40px，松手回弹由 _hide/_show 复位）
        self.rows_layout.setContentsMargins(12, 4 + min(dy, 80) // 2, 12, 8)
        if dy < 8:
            self._pull_overlay.hide()
            return
        text = '↑ 释放刷新' if dy >= self._PULL_THRESHOLD else '↓ 下拉刷新'
        self._pull_overlay.setText(text)
        self._pull_overlay.adjustSize()
        x = (self._scroll.width() - self._pull_overlay.width()) // 2
        y = max(-self._pull_overlay.height() + 6, 10 + int(dy * 0.4)
                - self._pull_overlay.height() // 2)
        self._pull_overlay.move(x, y)
        self._pull_overlay.show()
        self._pull_overlay.raise_()

    def _show_refreshing(self):
        """刷新中：顶部固定指示器 + 转圈动画，内容保持下拉位（对标 refresher 持续态）"""
        self._refreshing = True
        self._pull['dy'] = 0
        self._spinner_idx = 0
        self._spinner_timer.start()
        self.rows_layout.setContentsMargins(12, 44, 12, 8)
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
        # 内容回弹：复位列表上边距（对标 refresher 收回）
        self.rows_layout.setContentsMargins(12, 4, 12, 8)

    def _apply_chip_styles(self):
        """chips 样式（对标 .category-tab：灰底 #f2f2f2 / 选中微信绿，数字带灰色/白色）"""
        for cat, chip in self._chips.items():
            active = (cat == self.active_category)
            if active:
                chip.setStyleSheet(f"""
                    QPushButton {{ background: {theme.GREEN}; color: #fff; border: none;
                                  border-radius: 5px; padding: 0 13px; font-size: 13px; font-weight: 700; }}
                """)
            else:
                chip.setStyleSheet(f"""
                    QPushButton {{ background: #f2f2f2; color: #8a8f99; border: none;
                                  border-radius: 5px; padding: 0 13px; font-size: 13px; font-weight: 600; }}
                    QPushButton:hover {{ background: #e8e8e8; }}
                """)

    # ==================== 数据加载（3 数据源合并，对标 fetchDeviceList） ====================

    def refresh(self):
        """刷新全部 3 个数据源：设备表 + 对时表 + 配置表"""
        if not self.api.logged_in:
            self.notify('请先登录')
            return

        def load():
            devices = self.api.getDeviceList()
            sync = self.api.getDeviceSyncAll()
            config = self.api.getDeviceConfigAll()
            return devices, sync, config

        self.api.run_async(load, self._on_loaded, self._on_error)

    def _on_loaded(self, result):
        devices, sync, config = result
        self._device_rows = devices
        self._row_map = {str(d.get('deviceId', '')): d for d in devices}
        self._sync_map = sync
        self._config_map = config
        self._apply_merged(show_toast=True)
        self._hide_pull_overlay()

    def _on_error(self, msg):
        self._hide_pull_overlay()
        self.notify(f'加载失败: {msg}')

    # ==================== 合并（对标 _buildMergedList） ====================

    def _apply_merged(self, show_toast=False):
        level = self.api.level if self.api.logged_in else 99
        self.show_relay_tab = level <= 2
        self.show_add_btn = level <= 1
        self.add_btn.setVisible(self.show_add_btn)
        self._chips['relay'].setVisible(self.show_relay_tab)

        self._battery_cache = _load_battery_cache()
        merged = []
        for item in self._device_rows:
            device_id = str(item.get('deviceId', ''))
            merged.append(self._merge_one(item, self._sync_map.get(device_id),
                                          self._config_map.get(device_id)))
        _save_battery_cache(self._battery_cache)

        # 排序：无 ProductKey 在前，中继在后；组内按 "-" 后序号升序
        def has_pk(it):
            return bool(it['relay'])
        def seq(it):
            m = re.search(r'-(\d+)$', it['device_id'])
            return int(m.group(1)) if m else 0
        merged.sort(key=lambda it: (has_pk(it), seq(it)))
        self._merged = merged
        self._render()
        self.loaded.emit()
        if show_toast:
            self.notify('已刷新')

    def _merge_one(self, item, sync_info, cfg):
        device_id = str(item.get('deviceId', ''))
        is_relay = bool(item.get('ProductKey') and item.get('ProductKey') != '-')

        # —— 最后上报时间（来源：对时同步表；蓝牙来源桌面端无）——
        last_ts_ms = 0
        last_type = ''          # 'gps' | 'track' | 'time'
        last_lorastr = ''
        if sync_info and sync_info.get('ts', 0) > 0:
            last_ts_ms = sync_info['ts'] * 1000
            last_lorastr = str(sync_info.get('lorastr', '') or '')
            t = str(last_lorastr).split('|')[0].strip() if last_lorastr else ''
            last_type = {'1': 'gps', '5': 'track', '2': 'time'}.get(t, '')

        # 定位/跟踪坐标有效性（0,0 视为无效 → 空心点）
        gps_invalid = self._is_invalid_gps_lorastr(last_lorastr) \
            if last_type in ('gps', 'track') else False

        has_report = last_ts_ms > 0

        # —— 电量：只认对时记录；多级兜底 + "最近一次电量"缓存 ——
        battery = ''
        if sync_info:
            b = sync_info.get('battery')
            if b is not None:
                battery = str(int(round(b)))
        if not battery and sync_info:
            b = wx_codec.lorastr_battery(sync_info.get('lorastr', ''))
            if b is not None:
                battery = str(int(round(b)))
        if battery:
            self._battery_cache[device_id] = battery
        else:
            cached = self._battery_cache.get(device_id)
            if cached:
                battery = str(cached)

        # —— 配置：上报周期/主周期/开机窗口/休眠 ——
        cfg = cfg or {}
        report_interval_min = cfg.get('reportIntervalMin', 0) or 0
        main_period_min = cfg.get('mainPeriodMin', 0) or 0
        power_win = cfg.get('powerWin')
        is_dormant = self._in_power_window(power_win) is False  # 有窗口且不在窗口内

        now_ms = battery_swap._now_ms()
        eff_min = self._effective_interval(report_interval_min, main_period_min, power_win, now_ms)
        cd = self._build_countdown(last_ts_ms, eff_min, now_ms)

        # 信号/名称颜色：中继不在工作区间 → 灰；其它超过1个周期未上报 → 灰
        signal_gray = is_dormant if is_relay else cd['overdue']
        signal_color = _GRAY if signal_gray else _GREEN
        name_color = _GRAY if signal_gray else '#333333'
        battery_color = _GRAY if is_dormant else (_RED if battery and battery.isdigit()
                                                  and int(battery) < 50 else '#333333')
        # 离线：从未上报 或 超过1个完整周期（中继休眠不计离线）
        is_offline = (not has_report or cd['overdue']) and not (is_relay and is_dormant)

        return {
            'device_id': device_id, 'rename': str(item.get('rename', '') or ''),
            'relay': is_relay, 'visible': str(item.get('visible', '1')),
            'row': item,
            'has_report': has_report, 'last_ts': last_ts_ms,
            'last_type': last_type, 'gps_invalid': gps_invalid,
            'battery': battery, 'battery_color': battery_color,
            'report_interval_min': report_interval_min, 'main_period_min': main_period_min,
            'power_win': power_win, 'is_dormant': is_dormant,
            'signal_color': signal_color, 'name_color': name_color,
            'cd': cd, 'is_offline': is_offline,
        }

    @staticmethod
    def _in_power_window(win, now=None):
        """当前是否在开机窗口内（无窗口视为不在 → 用于休眠判定）；win=None 返回 None"""
        if not win:
            return None
        d = now or datetime.now()
        cur = d.hour * 60 + d.minute
        start = win['start'] * 60
        end = 23 * 60 + 59 if win['end'] == 23 else win['end'] * 60
        return start <= cur < end

    @staticmethod
    def _effective_interval(report_min, main_min, power_win, now_ms):
        """当前推算周期：开机窗口内用上报周期；窗口外用主周期（未配置回退上报周期）"""
        if not power_win:
            return report_min
        d = datetime.fromtimestamp(now_ms / 1000)
        in_work = DeviceListPage._in_power_window(power_win, d)
        if in_work:
            return report_min
        return main_min or report_min

    @staticmethod
    def _build_countdown(last_ts_ms, interval_min, now_ms):
        """倒计时状态（对标 _buildCountdownState，文案按截图用"延下次/超时/未上报"）"""
        empty = {'text': '', 'color': _GRAY, 'bg': '#f5f5f5', 'overdue': False}
        if not last_ts_ms:
            return empty
        if not interval_min or interval_min <= 0:
            return {'text': '周期未配置', 'color': _GRAY, 'bg': '#f5f5f5', 'overdue': False}
        period_ms = interval_min * 60000
        next_ts = last_ts_ms + period_ms
        remain = next_ts - now_ms
        if remain > 0:
            return {'text': '延下次 ' + DeviceListPage._format_clock(remain),
                    'color': _GREEN, 'bg': '#e8f5e9', 'overdue': False}
        past = -remain
        if past >= period_ms:
            # 超过1个完整周期 → 灰色"未上报 X天 HH:MM:SS"（累计时长）
            return {'text': '未上报 ' + DeviceListPage._format_long_clock(now_ms - last_ts_ms),
                    'color': _GRAY, 'bg': '#f5f5f5', 'overdue': True}
        return {'text': '超时 ' + DeviceListPage._format_clock(past),
                'color': _RED, 'bg': '#ffebee', 'overdue': False}

    @staticmethod
    def _format_clock(ms):
        total = max(0, int(ms // 1000))
        h, m, s = total // 3600, (total % 3600) // 60, total % 60
        return f'{h:02d}:{m:02d}:{s:02d}' if h > 0 else f'{m:02d}:{s:02d}'

    @staticmethod
    def _format_long_clock(ms):
        total = max(0, int(ms // 1000))
        d, rest = divmod(total, 86400)
        h, m, s = rest // 3600, (rest % 3600) // 60, rest % 60
        if d > 0:
            return f'{d}天 {h:02d}:{m:02d}:{s:02d}'
        if h > 0:
            return f'{h:02d}:{m:02d}:{s:02d}'
        return f'{m:02d}:{s:02d}'

    @staticmethod
    def _is_invalid_gps_lorastr(lorastr):
        """定位坐标是否无效（0,0 / 缺失 / 不可解析 → True）"""
        if not lorastr:
            return True
        parts = str(lorastr).split('|')
        if len(parts) < 3 or not parts[2]:
            return True
        coord = re.split(r'[,，]', parts[2])
        if len(coord) < 2:
            return True
        try:
            lat, lng = float(coord[0]), float(coord[1])
        except ValueError:
            return True
        return lat == 0 and lng == 0

    # ==================== 分类过滤与渲染（对标 _applyCategoryFilter） ====================

    def _on_chip(self, cat):
        if cat == self.active_category:
            if cat == 'device':
                self.device_online_only = not self.device_online_only
                self._render()
                self.notify('仅显示在线设备' if self.device_online_only else '显示全部设备')
            return
        self.active_category = cat
        self._apply_chip_styles()
        self._render()

    def _render(self):
        # 清空旧行前先摘除缓存的控件引用：
        # 行控件 deleteLater 后，it['_badge']/it['_sig'] 会变成悬空引用，
        # 每秒 _tick 触碰已删除控件会在 Qt 槽内抛 RuntimeError 导致进程 abort
        for it in self._merged:
            it.pop('_badge', None)
            it.pop('_sig', None)
        # 清空旧行
        while self.rows_layout.count():
            item = self.rows_layout.takeAt(0)
            w = item.widget()
            if w:
                w.deleteLater()

        all_items = self._merged
        non_relay = [it for it in all_items if not it['relay']]
        counts = {'device': len(non_relay),
                  'relay': len(all_items) - len(non_relay),
                  'offline': sum(1 for it in all_items if it['is_offline'])}
        for cat, chip in self._chips.items():
            label = {'device': '设备', 'relay': '中继', 'offline': '离线'}[cat]
            chip.setText(f'{label} {counts[cat]}')

        if self.active_category == 'relay':
            items = [it for it in all_items if it['relay']]
        elif self.active_category == 'offline':
            items = [it for it in all_items if it['is_offline']]
        else:
            items = non_relay
            if self.device_online_only:
                items = [it for it in items if not it['is_offline']]
        # visible 过滤（未开启"显示所有设备"时只显示可见设备）
        items = [it for it in items if it['visible'] not in ('0', 'false', 'False')]

        if not items:
            tip_text = {'relay': '暂无中继设备', 'offline': '暂无离线设备',
                        'device': '暂无在线设备' if self.device_online_only else '暂无设备'}[self.active_category]
            tip = QLabel(tip_text)
            tip.setFont(theme.font(10))
            tip.setAlignment(Qt.AlignmentFlag.AlignCenter)
            tip.setStyleSheet('color: #bbbbbb; background: transparent; padding: 20px 0;')
            self.rows_layout.addWidget(tip)
            self.rows_layout.addStretch(1)
            return

        for i, it in enumerate(items):
            it['_index'] = i + 1
            self.rows_layout.addWidget(self._build_row(it))
        # 列表末尾加弹簧：行从上往下紧凑排列，剩余空间留白（否则滚动区把空间均分给行导致行被拉高）
        self.rows_layout.addStretch(1)

    def _build_row(self, it):
        """单行设备卡（对标 .list-item-compact：序号+信号格+名称/别名/电量+圆点/时钟+徽标）"""
        row = _RowWidget()
        row.setCursor(Qt.CursorShape.PointingHandCursor)
        h = row.h_layout

        # 序号（列表索引在 _render 里已知 → 由外部补）
        idx_lab = QLabel(str(it.get('_index', '')))
        idx_lab.setFixedWidth(28)
        idx_lab.setAlignment(Qt.AlignmentFlag.AlignCenter)
        idx_lab.setStyleSheet('color: #666666; font-size: 15px; font-weight: 700;'
                              'border: none; background: transparent;')
        h.addWidget(idx_lab)

        # 信号格
        sig = SignalBars()
        sig.set_color(it['signal_color'])
        h.addWidget(sig)
        h.addSpacing(5)

        # 名称 + 别名（淡 70%）+ 电量（电量色）
        name_color = it['name_color']
        alias_rgba = {'#999999': 'rgba(153,153,153,0.7)',
                      '#333333': 'rgba(51,51,51,0.7)'}.get(name_color, name_color)
        html = (f'<span style="color:{name_color}; font-weight:600;">{it["device_id"]}</span>')
        if it['rename']:
            html += f'<span style="color:{alias_rgba};">（{it["rename"]}）</span>'
        if it['battery']:
            html += f'<span style="color:{it["battery_color"]};">（{it["battery"]}）</span>'
        name_lab = QLabel(html)
        name_lab.setTextFormat(Qt.TextFormat.RichText)
        name_lab.setStyleSheet('font-size: 14px; border: none; background: transparent;')
        h.addWidget(name_lab)
        h.addSpacing(4)

        # 记录类型图标：gps 定位圆点 / track 紫点 / time 时钟
        if it['last_type'] in ('gps', 'track'):
            color = _PURPLE if it['last_type'] == 'track' else it['cd']['color']
            h.addWidget(_make_dot(color, it['gps_invalid']))
        elif it['last_type'] == 'time':
            clock = ClockIcon()
            clock.set_color(it['cd']['color'])
            h.addWidget(clock)

        h.addStretch()

        # 状态徽标（延下次绿 / 超时红 / 未上报灰 / 周期未配置灰 / 暂无上报）
        cd = it['cd']
        badge = QLabel()
        badge.setIndent(0)
        if cd['text']:
            badge.setText(cd['text'])
            badge_color, badge_bg = cd['color'], cd['bg']
        else:
            badge.setText('暂无上报')
            badge_color, badge_bg = _GRAY, '#f5f5f5'
        badge.setStyleSheet(f'color: {badge_color}; background: {badge_bg};'
                            f'font-size: 13px; font-weight: 700;'
                            f'padding: 3px 8px; border-radius: 4px; border: none;')
        it['_badge'] = badge
        it['_sig'] = sig
        h.addWidget(badge)

        row.item = it
        row.installEventFilter(self)   # 行上捕获下拉拖拽
        row.clicked.connect(lambda: self._on_row_clicked(it['row']))
        return row

    def _on_row_clicked(self, row):
        # 拖拽过的释放不当作点击（release 时已标记 suppress_click；消费后立即复位）
        if self._pull.get('suppress_click'):
            self._pull['suppress_click'] = False
            return
        self.detail_requested.emit(row)

    def _tick(self):
        """每秒刷新徽标文本/颜色 + 信号色（对标 _tickCountdown）"""
        now_ms = battery_swap._now_ms()
        for it in self._merged:
            eff = self._effective_interval(it['report_interval_min'],
                                           it['main_period_min'], it['power_win'], now_ms)
            cd = self._build_countdown(it['last_ts'], eff, now_ms)
            it['cd'] = cd
            badge = it.get('_badge')
            try:
                badge_live = badge is not None and badge.isVisibleTo(self)
            except RuntimeError:
                # 控件已被删除（悬空引用兜底，正常不应发生）
                it.pop('_badge', None)
                it.pop('_sig', None)
                continue
            if badge_live:
                if cd['text']:
                    badge.setText(cd['text'])
                    badge.setStyleSheet(f'color: {cd["color"]}; background: {cd["bg"]};'
                                        f'font-size: 13px; font-weight: 700;'
                                        f'padding: 3px 8px; border-radius: 4px; border: none;')
                else:
                    badge.setText('暂无上报')
                    badge.setStyleSheet(f'color: {_GRAY}; background: #f5f5f5;'
                                        f'font-size: 13px; font-weight: 700;'
                                        f'padding: 3px 8px; border-radius: 4px; border: none;')
            sig = it.get('_sig')
            if sig is not None:
                try:
                    gray = it['is_dormant'] if it['relay'] else cd['overdue']
                    sig.set_color(_GRAY if gray else _GREEN)
                except RuntimeError:
                    it.pop('_sig', None)

    # ==================== 新增设备弹窗（对标 onAdd 弹窗） ====================

    def _on_add(self):
        if not self.api.logged_in:
            self.notify('请先登录')
            return
        dlg = QDialog(self)
        dlg.setWindowTitle('新增设备')
        dlg.setModal(True)
        dlg.setFixedWidth(300)
        v = QVBoxLayout(dlg)
        v.setContentsMargins(20, 20, 20, 16)
        v.setSpacing(8)
        title = QLabel('新增设备')
        title.setFont(theme.font(12, bold=True))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title.setStyleSheet('color: #333333;')
        v.addWidget(title)
        lab = QLabel('设备ID')
        lab.setFont(theme.font(9))
        lab.setStyleSheet('color: #666666;')
        v.addWidget(lab)
        edit = QLineEdit()
        edit.setPlaceholderText('请输入设备ID')
        edit.setFixedHeight(40)
        edit.setStyleSheet('QLineEdit { border: 1px solid #e0e0e0; border-radius: 6px; padding: 0 12px; }'
                           'QLineEdit:focus { border: 1px solid ' + theme.PRIMARY + '; }')
        v.addWidget(edit)
        v.addSpacing(8)
        btns = QHBoxLayout()
        btns.setSpacing(12)
        cancel = QPushButton('取消')
        cancel.setFixedHeight(40)
        cancel.setStyleSheet(theme.GRAY_BUTTON_QSS)
        cancel.clicked.connect(dlg.reject)
        confirm = QPushButton('确认新增')
        confirm.setFixedHeight(40)
        confirm.setStyleSheet(f"""
            QPushButton {{ background: #1565c0; color: #fff; border: none;
                          border-radius: 6px; padding: 8px 24px; }}
            QPushButton:hover {{ background: #0d47a1; }}
        """)
        confirm.clicked.connect(dlg.accept)
        btns.addWidget(cancel)
        btns.addWidget(confirm)
        v.addLayout(btns)

        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        device_id = edit.text().strip()
        if not device_id:
            self.notify('请输入设备ID')
            return
        self.notify('提交中...')
        self.api.run_async(
            lambda: self.api.addDevice(device_id),
            on_ok=lambda _: (self.notify('新增成功'), self.refresh()),
            on_err=lambda m: self.notify(f'提交失败: {m}'),
        )


class _RowWidget(QWidget):
    """可点击的设备行：固定高度紧凑排列 + 悬停变色（对标小程序 :active #f5faff）"""
    clicked = pyqtSignal()

    _STYLE_BASE = 'border-bottom: 1px solid #f0f0f0; background: transparent;'
    _STYLE_HOVER = 'border-bottom: 1px solid #f0f0f0; background: #f5faff;'

    def __init__(self, parent=None):
        super().__init__(parent)
        # 普通 QWidget 子类默认不绘制样式表 background，必须开启 WA_StyledBackground
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self.h_layout = QHBoxLayout(self)
        self.h_layout.setContentsMargins(4, 9, 4, 9)
        self.h_layout.setSpacing(2)
        self.item = None
        self.setStyleSheet(self._STYLE_BASE)
        self.setFixedHeight(40)   # 18rpx*2 padding + 内容行，防止被滚动区拉伸

    def enterEvent(self, event):
        self.setStyleSheet(self._STYLE_HOVER)
        super().enterEvent(event)

    def leaveEvent(self, event):
        self.setStyleSheet(self._STYLE_BASE)
        super().leaveEvent(event)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.clicked.emit()
        super().mouseReleaseEvent(event)


# ==================== 设备详情页（Tab: 设备详情，对标小程序首页） ====================

# 功能按钮定义（与小程序 index.js featureBtns / relayFeatureBtns 完全一致）
FEATURE_BTNS = [
    ('location', '实时定位', '#F56C6C', '📍'),
    ('records',  '数据列表', '#00ACC1', '📋'),
    ('track',    '轨迹地图', '#F0A020', '🗺'),
    ('setting',  '设备设置', '#26A69A', '⚙'),
    ('battery',  '电量走势', '#1E88E5', '🔋'),
    ('password', '修改密码', '#5C6BC0', '🔑'),
    ('logout',   '注销登入', '#EF5350', '🚪'),
]
RELAY_FEATURE_BTNS = [
    ('records',  '数据列表', '#00ACC1', '📋'),
    ('setcoord', '中继坐标', '#8E24AA', '📌'),
    ('setting',  '设备设置', '#26A69A', '⚙'),
    ('battery',  '电量走势', '#1E88E5', '🔋'),
    ('password', '修改密码', '#5C6BC0', '🔑'),
    ('logout',   '注销登入', '#EF5350', '🚪'),
]


class DeviceDetailPage(QWidget):
    """设备详情首页：蓝色头部卡 + 圆形图标功能网格（3列）"""

    back_requested = pyqtSignal()
    logout_requested = pyqtSignal()
    open_records = pyqtSignal()
    open_battery = pyqtSignal()
    open_cmd = pyqtSignal()

    def __init__(self, api: WxApi, scene, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.scene = scene
        self.notify = notify or (lambda msg: print('[设备详情]', msg))
        self._device_rows = []
        self._sync_map = {}
        self._current_device = None
        self._setup_ui()

    # ==================== UI ====================

    def _setup_ui(self):
        from PyQt6.QtWidgets import QStackedWidget
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        self.stack = QStackedWidget()
        outer.addWidget(self.stack)
        self.stack.addWidget(self._build_empty_view())   # 0 未选设备
        self.stack.addWidget(self._build_detail_view())  # 1 设备详情

    def _build_empty_view(self):
        """未选择设备空状态（对标 .home-empty）"""
        w = QWidget()
        layout = QVBoxLayout(w)
        layout.setContentsMargins(40, 110, 40, 0)
        layout.setSpacing(10)

        icon = QLabel("📡")
        icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
        icon.setStyleSheet("font-size: 60px; border: none; background: transparent;")
        layout.addWidget(icon)

        title = QLabel("暂未选择设备")
        title.setFont(theme.font(13, bold=True))
        title.setStyleSheet(f"color: #333333; border: none; background: transparent;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)

        desc = QLabel("请在设备列表中选择一台设备，选择后会被记住，\n"
                      "下次打开会直接显示该设备详情")
        desc.setFont(theme.font(10))
        desc.setStyleSheet(f"color: #999999; border: none; background: transparent;")
        desc.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(desc)

        layout.addSpacing(14)
        go_btn = QPushButton("去设备列表选择")
        go_btn.setFixedSize(180, 44)
        go_btn.setFont(theme.font(11))
        go_btn.setStyleSheet(f"""
            QPushButton {{ background: {theme.PRIMARY}; color: white; border: none;
                          border-radius: 22px; font-size: 15px; }}
            QPushButton:hover {{ background: {theme.PRIMARY_HOVER}; }}
        """)
        go_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        go_btn.clicked.connect(self.back_requested.emit)
        layout.addWidget(go_btn, alignment=Qt.AlignmentFlag.AlignHCenter)
        layout.addStretch()
        return w

    def _build_detail_view(self):
        w = QWidget()
        outer = QVBoxLayout(w)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet(theme.SCROLLBAR_QSS)
        content = QWidget()
        content.setStyleSheet(f"background: {theme.BG};")
        cl = QVBoxLayout(content)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.setSpacing(0)

        cl.addWidget(self._build_header_card())
        cl.addWidget(self._build_feature_grid())
        cl.addStretch()
        scroll.setWidget(content)
        outer.addWidget(scroll)
        return w

    def _build_header_card(self):
        """蓝色渐变头部卡（对标 .detail-header：渐变#1a73e8→#1565c0 圆角10px）"""
        card = QFrame()
        card.setObjectName('headerCard')
        card.setStyleSheet("""
            QFrame#headerCard {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 #1a73e8, stop:1 #1565c0);
                border-radius: 10px;
                border: none;
            }
        """)
        card.setContentsMargins(0, 0, 0, 0)
        h = QHBoxLayout(card)
        h.setContentsMargins(14, 16, 12, 16)
        h.setSpacing(12)

        # ---- 左：设备图片（75x75，半透明白底 + 白描边，对标 .pic-area 150rpx）----
        self.pic_label = QLabel("📡\n设备图片")
        self.pic_label.setFixedSize(75, 75)
        self.pic_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.pic_label.setStyleSheet(f"""
            QLabel {{
                background: rgba(255, 255, 255, 0.2);
                border: 2px solid rgba(255, 255, 255, 0.5);
                border-radius: 6px;
                color: rgba(255, 255, 255, 0.7);
                font-size: 9px;
            }}
        """)
        h.addWidget(self.pic_label, alignment=Qt.AlignmentFlag.AlignTop)

        # ---- 右：标题 + 信息行 ----
        info = QVBoxLayout()
        info.setSpacing(4)

        self.detail_title = QLabel("")
        self.detail_title.setFont(theme.font(11, bold=True))
        self.detail_title.setStyleSheet(
            "color: #FFFFFF; border: none; background: transparent;")
        self.detail_title.setWordWrap(True)
        info.addWidget(self.detail_title)
        info.addSpacing(3)

        # 信息行（label 65px rgba(255,255,255,.65)，value rgba(255,255,255,.9)，28rpx=14px）
        self.row_report_value = QLabel("-")
        self.row_power_value = QLabel("-")
        self.row_gps_value = QLabel("-")
        self.row_swap_value = QLabel("-")
        self.row_gps_box, self.row_gps = self._info_row("GPS时间", self.row_gps_value)
        self.row_swap_box, self.row_swap = self._info_row("上次换电", self.row_swap_value)
        self.row_swap_value.setStyleSheet(
            "color: #A7F0BA; font-size: 14px; font-weight: 500;"
            " border: none; background: transparent;")
        info.addWidget(self._info_row("上报周期", self.row_report_value)[1])
        info.addWidget(self._info_row("开机时间", self.row_power_value)[1])
        info.addWidget(self.row_gps)
        info.addWidget(self.row_swap)
        self.row_report_value.setTextFormat(Qt.TextFormat.RichText)
        h.addLayout(info, stretch=1)

        # ---- 右上角：编辑按钮（56rpx=28px 圆，半透明白底）----
        edit_btn = QPushButton("✏")
        edit_btn.setFixedSize(28, 28)
        edit_btn.setStyleSheet("""
            QPushButton { background: rgba(255, 255, 255, 0.2); border: none;
                          border-radius: 14px; font-size: 13px; color: white; }
            QPushButton:hover { background: rgba(255, 255, 255, 0.4); }
        """)
        edit_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        edit_btn.clicked.connect(self._rename_device)
        h.addWidget(edit_btn, alignment=Qt.AlignmentFlag.AlignTop)
        return card

    def _info_row(self, label, value_widget):
        """信息行：label 定宽 + value（返回 [整行容器, 行widget]）"""
        row = QWidget()
        row.setStyleSheet("border: none; background: transparent;")
        h = QHBoxLayout(row)
        h.setContentsMargins(0, 0, 0, 0)
        h.setSpacing(0)
        lab = QLabel(label)
        lab.setFixedWidth(65)
        lab.setStyleSheet(
            "color: rgba(255, 255, 255, 0.65); font-size: 14px;"
            " border: none; background: transparent;")
        h.addWidget(lab)
        h.addWidget(value_widget, stretch=1)
        return row, row

    def _build_feature_grid(self):
        """圆形图标功能网格（对标 .home-features：圆100rpx=50px，emoji 44rpx=22px，label 26rpx=13px）"""
        self.grid_host = QWidget()
        self.grid_host.setStyleSheet(f"background: {theme.BG}; border: none;")
        self.grid_layout = QGridLayout(self.grid_host)
        self.grid_layout.setContentsMargins(12, 8, 12, 8)
        self.grid_layout.setHorizontalSpacing(0)
        self.grid_layout.setVerticalSpacing(0)
        return self.grid_host

    def _render_features(self, is_relay):
        """按设备类型渲染功能按钮（清空重建；level 门控与小程序一致）"""
        while self.grid_layout.count():
            item = self.grid_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        btns = RELAY_FEATURE_BTNS if is_relay else FEATURE_BTNS
        level = self.api.level if self.api.logged_in else 99
        for i, (key, label, color, emoji) in enumerate(btns):
            # 门控：records/setting 需 level<=1，track 需 level<=2
            if key == 'records' and level > 1:
                continue
            if key == 'setting' and level > 1:
                continue
            if key == 'track' and level > 2:
                continue
            cell = QWidget()
            cell.setStyleSheet("border: none; background: transparent;")
            v = QVBoxLayout(cell)
            v.setContentsMargins(0, 8, 0, 6)
            v.setSpacing(7)
            v.setAlignment(Qt.AlignmentFlag.AlignHCenter)
            circle = QPushButton(emoji)
            circle.setFixedSize(50, 50)
            circle.setFont(theme.font(11))
            circle.setCursor(Qt.CursorShape.PointingHandCursor)
            circle.setStyleSheet(f"""
                QPushButton {{
                    background-color: {color}; color: white; border: none;
                    border-radius: 25px; font-size: 22px;
                }}
                QPushButton:pressed {{ background-color: {color}; }}
            """)
            circle.clicked.connect(lambda _, k=key: self._on_feature(k))
            v.addWidget(circle, alignment=Qt.AlignmentFlag.AlignHCenter)
            lab = QLabel(label)
            lab.setFont(theme.font(10))
            lab.setStyleSheet("color: #333333; border: none; background: transparent;")
            v.addWidget(lab, alignment=Qt.AlignmentFlag.AlignHCenter)
            self.grid_layout.addWidget(cell, i // 3, i % 3)

    # ==================== 打开详情 ====================

    def open_detail(self, row, device_rows=None, sync_map=None):
        """由外部（设备列表双击/无感刷新）打开某台设备的详情"""
        if device_rows is not None:
            self._device_rows = device_rows
        if sync_map is not None:
            self._sync_map = sync_map
        self._current_device = row
        device_id = str(row.get('deviceId', ''))
        rename = row.get('rename', '')
        self.detail_title.setText(f"{device_id} ({rename})" if rename else device_id)
        self.stack.setCurrentIndex(1)
        is_relay = bool(row.get('ProductKey') or row.get('DeviceName'))
        self._render_features(is_relay)
        # 中继（太阳能供电）不显示 GPS时间 与 上次换电
        self.row_gps.setVisible(not is_relay)
        self.row_swap.setVisible(not is_relay)
        self._apply_config('-')
        self.row_swap_value.setText("-")
        self._load_pic(row.get('picurl', ''))
        self._load_config()
        if not is_relay:
            battery_swap.get_last_swap(
                self.api, device_id,
                lambda rel: self.row_swap_value.setText(rel or '-'))

    @property
    def current_device_id(self):
        return str(self._current_device.get('deviceId', '')) if self._current_device else ''

    # ==================== 数据加载 ====================

    def _load_pic(self, picurl):
        """设备图片：有 picurl 则异步拉取，否则显示占位"""
        picurl = str(picurl or '').strip()
        if not picurl:
            self.pic_label.setText("📡\n设备图片")
            self.pic_label.setStyleSheet(self.pic_label.styleSheet())
            return

        def load():
            import requests
            resp = requests.get(picurl, timeout=10)
            return base64.b64encode(resp.content).decode('ascii')

        def on_ok(b64):
            pm = QPixmap()
            pm.loadFromData(base64.b64decode(b64))
            if not pm.isNull():
                self.pic_label.setPixmap(pm.scaled(
                    75, 75,
                    Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                    Qt.TransformationMode.SmoothTransformation))
                self.pic_label.setStyleSheet(
                    "border: 2px solid rgba(255,255,255,0.5); border-radius: 6px;"
                    "background: rgba(255,255,255,0.2);")

        def on_err(_msg):
            self.pic_label.setText("📡\n设备图片")

        self.api.run_async(load, on_ok, on_err)

    def _load_config(self):
        """拉配置 lorastr 并刷新头部卡信息行"""
        if not self._current_device:
            return
        device_id = self._current_device.get('deviceId', '')

        def load():
            return self.api.getDeviceConfigById(device_id)
        self.api.run_async(load, self._on_config, lambda m: None)

    def _on_config(self, cfg_row):
        self._apply_config(cfg_row.get('lorastr', ''))

    @staticmethod
    def _is_in_power_on(config_lorastr):
        """当前时间是否在开机窗口内（对标 index._isInPowerOnNow）"""
        if not config_lorastr:
            return True
        parts = str(config_lorastr).split('|')
        if len(parts) < 3 or not parts[2]:
            return True
        cfg_parts = parts[2].split(',')
        if len(cfg_parts) < 2 or not cfg_parts[1].strip():
            return True
        win = wx_codec.parse_time_window(cfg_parts[1].strip())
        if not win:
            return True
        now = datetime.now()
        cur = now.hour * 60 + now.minute
        start = win['start'] * 60
        end = 23 * 60 + 59 if win['end'] == 23 else win['end'] * 60
        return start <= cur < end

    def _apply_config(self, config_lorastr):
        """解析配置 lorastr → 上报周期/开机时间/GPS时间（对标 _applyConfigFromLorastr）"""
        report_interval = '-'
        power_on = '-'
        gps_time = '-'
        main_period = 0
        if config_lorastr:
            parts = str(config_lorastr).split('|')
            if len(parts) >= 3 and parts[2]:
                cfg_parts = parts[2].split(',')
                if len(cfg_parts) >= 1 and cfg_parts[0].strip():
                    report_interval = cfg_parts[0].strip()
                if len(cfg_parts) >= 2 and cfg_parts[1].strip():
                    power_on = wx_codec.format_time_range(cfg_parts[1].strip()) or '-'
                if len(cfg_parts) >= 3 and cfg_parts[2].strip():
                    gps_time = wx_codec.format_time_range(cfg_parts[2].strip()) or '-'
                if len(cfg_parts) >= 4:
                    try:
                        mp = int(cfg_parts[3].strip())
                        if 1 <= mp <= 10:
                            main_period = mp
                    except ValueError:
                        pass

        in_power_on = self._is_in_power_on(config_lorastr)
        # 上报周期：当前生效区间白色高亮，另一区间 40% 白弱化（对标 hi-val-main/hi-val-dim）
        main_txt = report_interval if in_power_on else str(main_period * 10)
        dim_txt = str(main_period * 10) if in_power_on else report_interval
        html = (f'<span style="color:#FFFFFF; font-weight:bold;">{main_txt}分钟</span>'
                f'<span style="color:rgba(255,255,255,0.4)">（{dim_txt}分钟）</span>'
                if main_period else
                f'<span style="color:#FFFFFF; font-weight:bold;">{report_interval}分钟</span>')
        self.row_report_value.setText(html)
        self.row_power_value.setText(power_on)
        self.row_gps_value.setText(gps_time)

    # ==================== 功能按钮 ====================

    def _on_feature(self, key):
        if key == 'location':
            self._realtime_locate()
        elif key == 'records':
            self.open_records.emit()
        elif key == 'track':
            self._show_track()
        elif key == 'setting':
            self.open_cmd.emit()
        elif key == 'battery':
            self.open_battery.emit()
        elif key == 'password':
            self.notify("修改密码开发中")
        elif key == 'setcoord':
            self.notify("中继坐标开发中")
        elif key == 'logout':
            self.logout_requested.emit()

    # ==================== 定位/轨迹 ====================

    def _realtime_locate(self):
        """实时定位：优先取 device_gps 缓存表最新坐标显示；否则发 upgps 指令"""
        if not self._current_device:
            return
        device_id = self._current_device.get('deviceId', '')

        def load():
            gps_list = self.api.getDeviceGpsAll()
            return next((g for g in gps_list if g['deviceId'] == device_id), None)

        def on_ok(gps_row):
            fresh = gps_row and (datetime.now().timestamp() - gps_row.get('ts', 0)) < 3600
            if fresh:
                lat, lng = gps_row['lat'], gps_row['lng']
                self.notify(f"{device_id} 最新坐标: {lat},{lng}")
                if self.scene:
                    self.scene.clear_all_place()
                    self.scene.receive_place_to_scene3d((lat, lng), device_id)
                    self.scene.receive_gps_coordinates(lat, lng)
            else:
                self.notify("缓存坐标不新鲜，正在下发 upgps 指令...")
                self._send_upgps(device_id)
        self.api.run_async(load, on_ok, lambda m: self.notify(f"加载失败: {m}"))

    def _send_upgps(self, device_id):
        """按目标设备工作周期/大周期计算 upgps 分钟数，经最优中继下发"""
        def load():
            cfg_row = self.api.getDeviceConfigById(device_id)
            value = wx_codec.calc_upgps_value(cfg_row.get('lorastr', ''))
            relays = self.api.getDeviceBestRssibyId(device_id, limit=10)
            devices = self.api.getDeviceList()
            return value, relays, devices

        def on_ok(result):
            value, relays, devices = result
            # 设备自带三元组则直发，否则找 RSSI 最优且有密钥的中继转发
            target = next((d for d in devices if str(d.get('deviceId', '')) == device_id), {})
            candidates = []
            if target.get('ProductKey') and target.get('DeviceName'):
                candidates.append(target)
            device_map = {str(d.get('deviceId', '')): d for d in devices}
            for r in relays[:2]:
                relay = device_map.get(str(r.get('upDateDevice', '')))
                if relay and relay.get('ProductKey') and relay.get('DeviceName') \
                        and relay not in candidates:
                    candidates.append(relay)
            if not candidates:
                self.notify("没有可用中继/凭据，无法下发指令")
                return
            for relay in candidates:
                cmd = {'cmd': 'upgps', 'value': value or 0, 'deviceId': device_id}
                self.api.run_async(
                    lambda r=relay, c=cmd: self.api.sendtodtucmd(
                        r['DeviceName'], r['ProductKey'], c),
                    on_ok=lambda _, r=relay: self.notify(f"指令已发往中继 {r.get('deviceId', '')}"),
                    on_err=lambda m: self.notify(f"下发失败: {m}"),
                )
        self.api.run_async(load, on_ok, lambda m: self.notify(f"加载失败: {m}"))

    def _show_track(self):
        """今日轨迹：拉当天 GPS 记录画到 3D 场景"""
        if not self._current_device:
            return
        device_id = self._current_device.get('deviceId', '')
        curdate = datetime.now().strftime('%Y-%m-%d')
        self.api.run_async(
            lambda: self.api.getDeviceLogGpsbyId(device_id, curdate),
            on_ok=self._on_track,
            on_err=lambda m: self.notify(f"加载失败: {m}"),
        )

    def _on_track(self, rows):
        points = []
        for row in rows:
            gps = str(row.get('gps', ''))
            parts = gps.split(',')
            if len(parts) >= 2:
                try:
                    lat, lng = float(parts[0]), float(parts[1])
                    if -90 <= lat <= 90 and -180 <= lng <= 180 and (lat, lng) != (0.0, 0.0):
                        points.append((lat, lng))
                except ValueError:
                    continue
        if not points:
            self.notify("今天没有 GPS 轨迹")
            return
        if self.scene:
            self.scene.clear_all_load_line()
            self.scene.receive_load_to_scene(points)
        self.notify(f"已显示轨迹，共 {len(points)} 个点")

    def _rename_device(self):
        """改名（对标小程序编辑设备弹窗的改名项）"""
        if not self._current_device:
            return
        row = self._current_device
        name, ok = QInputDialog.getText(self, "设备改名", "新名称:",
                                        text=str(row.get('rename', '')))
        if not ok:
            return
        device_id = row.get('deviceId', '')
        self.api.run_async(
            lambda: self.api.updateDevice(
                device_id, rename=name.strip(),
                product_key=row.get('ProductKey', ''), device_name=row.get('DeviceName', ''),
                device_secret=row.get('DeviceSecret', ''),
                visible=str(row.get('visible', '1')), picurl=row.get('picurl', '')),
            on_ok=lambda _: self.notify("改名成功"),
            on_err=lambda m: self.notify(f"改名失败: {m}"),
        )
