# -*- coding: utf-8 -*-
"""设备设置/DTU指令页（对标小程序 pages/dtu-cmd 普通设备 + pages/relay-dtu-cmd 中继设备）
- 布局完全对照：📡目标设备 / 🔄中继转发设备 / ⚡指令内容(time参数+快捷指令) / 发送指令 / 📋发送日志
- 普通设备快捷指令：上报GPS/上报模式/位置跟踪/获取信息/设备重启/发射功率/GPS测试/配置下发/农场中心
- 中继设备快捷指令：工作时间/获取信息/发射功率/开关LORA/重启中继/配置下发/开关Debug
- 配置下发弹框：普通 ①上报周期②开机时间③GPS工作时间④主周期；中继 ①上报周期②开机时间
- 发送路径：手动选中继 → 设备自带密钥直发 → 自动查 RSSI 最优至多 2 台中继转发
"""

import json as _json
import time as _time

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                             QComboBox, QPlainTextEdit, QLineEdit, QDialog, QScrollArea,
                             QGraphicsDropShadowEffect, QDialogButtonBox, QTimeEdit)
from PyQt6.QtGui import QColor
from PyQt6.QtCore import Qt, pyqtSignal, QTime

from crowui import theme, wx_codec
from crowui.wx_api import WxApi

_PERIOD_RANGE = [5, 10, 15, 30, 60]                      # 上报周期档位（分钟，5-60）
_MAIN_PERIOD_RANGE = [i * 10 for i in range(1, 11)]      # 主周期 10-100 分钟（参数1-10）
_UPGPS_CYCLE = [0, 1, 10, 59]                            # 上报GPS循环取值
_READINFO_CYCLE = ['0', '1', '2']                        # 获取信息循环取值


def _section_title(text, color='#1989fa'):
    """卡片内小标题：加粗 + 左侧色条（对标 .section-title）"""
    lb = QLabel(text)
    lb.setStyleSheet(f'color: #333333; font-size: 14px; font-weight: bold;'
                     f'border-left: 3px solid {color}; padding-left: 5px;'
                     f'border-top: none; border-right: none; border-bottom: none;'
                     f'background: transparent;')
    return lb


def _hint_label(text):
    lb = QLabel(text)
    lb.setWordWrap(True)
    lb.setStyleSheet('color: #999999; font-size: 11px; border: none;'
                     'background: transparent; margin-top: 6px;')
    return lb


class _SectionCard(QWidget):
    """白色圆角卡片（对标 .section-card：margin 16/24rpx、padding 24rpx、投影）"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self.setStyleSheet('background: #FFFFFF; border-radius: 8px;')
        shadow = QGraphicsDropShadowEffect(self)
        shadow.setBlurRadius(6)
        shadow.setOffset(0, 1)
        shadow.setColor(QColor(0, 0, 0, 15))
        self.setGraphicsEffect(shadow)
        self.body = QVBoxLayout(self)
        self.body.setContentsMargins(12, 10, 12, 10)
        self.body.setSpacing(8)


class _ConfigDialog(QDialog):
    """配置下发弹框（对标 dtu-cmd/relay-dtu-cmd 的 config modal）
    relay=False：①上报周期(分钟)②开机时间③GPS工作时间④主周期
    relay=True ：①上报周期(秒)②开机时间（中继只用前两段）
    """

    def __init__(self, api, device_id, is_relay, parent=None):
        super().__init__(parent)
        self.api = api
        self.device_id = device_id
        self.is_relay = is_relay
        self.result_value = None
        self.setWindowTitle('⚙️ 配置下发')
        self.setMinimumWidth(380)
        v = QVBoxLayout(self)
        v.setSpacing(10)

        def group_title(text, rng):
            lb = QLabel(f'{text}　<font color="#999999">{rng}</font>')
            lb.setTextFormat(Qt.TextFormat.RichText)
            lb.setStyleSheet('color: #333333; font-size: 13px; font-weight: 600;'
                             'border: none; background: transparent;')
            return lb

        def hour_combo(max_h):
            cb = QComboBox()
            for h in range(max_h + 1):
                cb.addItem(f'{h} 时', h)
            return cb

        # ① 上报周期
        v.addWidget(group_title('① 上报周期（分钟）' if not is_relay else '① 上报周期（秒）',
                                '5-60'))
        self.period_combo = QComboBox()
        for p in _PERIOD_RANGE:
            self.period_combo.addItem(f'{p} 分钟', p)
        v.addWidget(self.period_combo)

        # ② 开机时间
        v.addWidget(group_title('② 开机时间（小时）', ''))
        row = QHBoxLayout()
        self.work_start = hour_combo(23)
        self.work_end = hour_combo(24)
        row.addWidget(QLabel('开始时间'))
        row.addWidget(self.work_start, stretch=1)
        row.addSpacing(10)
        row.addWidget(QLabel('结束时间'))
        row.addWidget(self.work_end, stretch=1)
        v.addLayout(row)

        # ③ GPS工作时间（仅普通设备）
        self.gps_start = self.gps_end = None
        if not is_relay:
            v.addWidget(group_title('③ GPS工作时间（小时）', ''))
            row = QHBoxLayout()
            self.gps_start = hour_combo(23)
            self.gps_end = hour_combo(24)
            row.addWidget(QLabel('开始时间'))
            row.addWidget(self.gps_start, stretch=1)
            row.addSpacing(10)
            row.addWidget(QLabel('结束时间'))
            row.addWidget(self.gps_end, stretch=1)
            v.addLayout(row)

            # ④ 主周期
            v.addWidget(group_title('④ 主周期（分钟）', '10-100'))
            row = QHBoxLayout()
            self.main_combo = QComboBox()
            for p in _MAIN_PERIOD_RANGE:
                self.main_combo.addItem(f'{p} 分钟', p)
            row.addWidget(QLabel('主周期'))
            row.addWidget(self.main_combo, stretch=1)
            v.addLayout(row)

        v.addWidget(_hint_label(
            '开机时间窗口内按①上报周期上报（如5分钟）；不在开机时间（非工作时间）按④主周期上报'
            '（如20分钟）。③ GPS工作时间仅展示，不影响本判断。仅支持当天时间窗口（持续≥1小时），'
            '确定后编码为两位代号填入指令内容' if not is_relay else
            '仅支持当天时间窗口（持续≥1小时），确定后将编码为两位代号填入指令内容'))

        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Cancel
                              | QDialogButtonBox.StandardButton.Ok)
        bb.button(QDialogButtonBox.StandardButton.Cancel).setText('取消')
        bb.button(QDialogButtonBox.StandardButton.Ok).setText('确定')
        bb.accepted.connect(self._confirm)
        bb.rejected.connect(self.reject)
        v.addWidget(bb)

        self._prefill()

    def _prefill(self):
        """用设备配置表 lorastr 预填（对标 _parseLoraConfigToModal）"""
        try:
            cfg = self.api.getDeviceConfigById(self.device_id)
        except Exception:
            return
        lorastr = cfg.get('lorastr', '')
        if not lorastr:
            return
        parsed = wx_codec.parse_device_config_lorastr(lorastr)
        period = parsed.get('reportIntervalMin') or 10
        period = min(60, max(5, period))
        idx = self.period_combo.findData(period)
        if idx < 0:   # 旧配置不在档位上（如20分钟）取最接近档位
            idx = min(range(len(_PERIOD_RANGE)),
                      key=lambda i: abs(_PERIOD_RANGE[i] - period))
        self.period_combo.setCurrentIndex(idx)
        win = parsed.get('powerWin')
        if win:
            self.work_start.setCurrentIndex(win['start'])
            self.work_end.setCurrentIndex(min(24, win['end'] + 1)
                                          if win['end'] == 23 else win['end'])
        if not self.is_relay:
            win = parsed.get('gpsWin')
            if win:
                self.gps_start.setCurrentIndex(win['start'])
                self.gps_end.setCurrentIndex(min(24, win['end'] + 1)
                                             if win['end'] == 23 else win['end'])
            mp = parsed.get('mainPeriodMin', 0)
            param = max(1, min(10, mp // 10)) if mp >= 10 else 1
            self.main_combo.setCurrentIndex(max(0, param - 1))

    def _confirm(self):
        period = self.period_combo.currentData()
        work_code = wx_codec.encode_time_window(self.work_start.currentData(),
                                                self.work_end.currentData())
        if not work_code:
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.warning(self, '提示', '开机时间需为当天整点且持续≥1小时')
            return
        if self.is_relay:
            value = f'{period},{work_code}'
        else:
            gps_code = wx_codec.encode_time_window(self.gps_start.currentData(),
                                                   self.gps_end.currentData())
            if not gps_code:
                from PyQt6.QtWidgets import QMessageBox
                QMessageBox.warning(self, '提示', 'GPS工作时间需为当天整点且持续≥1小时')
                return
            main = self.main_combo.currentData() // 10   # 10-100分钟 → 参数1-10
            value = f'{period},{work_code},{gps_code},{main}'
        self.result_value = value
        self.accept()


class _WorkTimeDialog(QDialog):
    """中继工作时间弹框（对标 worktime modal：开始~结束，≥5小时）"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle('设置工作时间')
        self.setMinimumWidth(320)
        v = QVBoxLayout(self)
        row = QHBoxLayout()
        row.addWidget(QLabel('开始时间'))
        self.start_edit = QTimeEdit(QTime(8, 0))
        self.start_edit.setDisplayFormat('HH:mm')
        row.addWidget(self.start_edit, stretch=1)
        v.addLayout(row)
        row = QHBoxLayout()
        row.addWidget(QLabel('结束时间'))
        self.end_edit = QTimeEdit(QTime(20, 0))
        self.end_edit.setDisplayFormat('HH:mm')
        row.addWidget(self.end_edit, stretch=1)
        v.addLayout(row)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Cancel
                              | QDialogButtonBox.StandardButton.Ok)
        bb.button(QDialogButtonBox.StandardButton.Cancel).setText('取消')
        bb.button(QDialogButtonBox.StandardButton.Ok).setText('确定')
        bb.accepted.connect(self._confirm)
        bb.rejected.connect(self.reject)
        v.addWidget(bb)
        self.value = None

    def _confirm(self):
        s, e = self.start_edit.time(), self.end_edit.time()
        start_min = s.hour() * 60 + s.minute()
        end_min = e.hour() * 60 + e.minute()
        if start_min >= end_min:
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.warning(self, '提示', '开始时间必须小于结束时间')
            return
        if end_min - start_min < 300:
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.warning(self, '提示', '工作时间窗口不能小于5小时')
            return
        self.value = f'{s.toString("HH:mm")}-{e.toString("HH:mm")}'
        self.accept()


class CmdPage(QWidget):
    """设备设置/DTU指令（对标 pages/dtu-cmd + pages/relay-dtu-cmd，同一页双模式）"""

    mode_changed = pyqtSignal(bool)   # True=中继模式（leftpanel 据此改标题）

    def __init__(self, api: WxApi, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[指令]', msg))
        self._device_rows = []
        self._is_relay = False
        # 快捷指令循环/切换状态（对标 upgpsIndex/sendmodeValue/...）
        self._upgps_idx = 0
        self._readinfo_idx = -1
        self._sendmode_val = 0
        self._relayreboot_val = 0
        self._lorasw_val = 0
        self._debug_val = 0
        self._setup_ui()
        self.refresh_devices()

    # ==================== UI ====================

    def _setup_ui(self):
        self.setStyleSheet('background: #f5f5f5;')
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet(theme.SCROLLBAR_QSS)
        outer.addWidget(scroll)
        body = QWidget()
        body.setObjectName('cmdBody')
        body.setStyleSheet('QWidget#cmdBody { background: #f5f5f5; }')
        scroll.setWidget(body)
        layout = QVBoxLayout(body)
        layout.setContentsMargins(12, 8, 12, 20)
        layout.setSpacing(8)

        # ---- 📡 目标设备 ----
        card1 = _SectionCard()
        card1.body.addWidget(_section_title('📡 目标设备'))
        self.target_combo = QComboBox()
        self.target_combo.setStyleSheet(
            'QComboBox { border: 1px solid #e0e0e0; border-radius: 6px; padding: 6px 10px;'
            ' font-size: 13px; background: #fff; }')
        self.target_combo.currentIndexChanged.connect(self._on_target_changed)
        card1.body.addWidget(self.target_combo)
        layout.addWidget(card1)

        # ---- 🔄 中继转发设备（普通设备模式才有；中继模式页无此节，对标 relay-dtu-cmd）----
        self.relay_card = _SectionCard()
        self.relay_card.body.addWidget(_section_title('🔄 中继转发设备'))
        self.relay_combo = QComboBox()
        self.relay_combo.setStyleSheet(
            'QComboBox { border: 1px solid #ff9500; border-radius: 6px; padding: 6px 10px;'
            ' font-size: 13px; background: #fffaf5; }')
        self.relay_card.body.addWidget(self.relay_combo)
        self.relay_card.body.addWidget(_hint_label(
            '自动模式下将根据RSSI信号自动选择信号最好的两台中继同时下发（仅有一台时只发一条）'))
        layout.addWidget(self.relay_card)

        # ---- ⚡ 指令内容 ----
        card3 = _SectionCard()
        card3.body.addWidget(_section_title('⚡ 指令内容'))
        self.cmd_edit = QPlainTextEdit()
        self.cmd_edit.setPlaceholderText('输入指令JSON或文本...')
        self.cmd_edit.setFixedHeight(48)   # 2 行足够，指令 JSON 不会太长
        self.cmd_edit.setStyleSheet(
            "QPlainTextEdit { border: 1px solid #e0e0e0; border-radius: 6px;"
            " padding: 6px; font-size: 12px; font-family: 'Courier New', monospace;"
            " background: #fff; }")
        card3.body.addWidget(self.cmd_edit)

        # time 参数行
        row = QHBoxLayout()
        lb = QLabel('⏱ time参数')
        lb.setStyleSheet('color: #666666; font-size: 12px; border: none;'
                         'background: transparent;')
        self.time_edit = QLineEdit()
        self.time_edit.setPlaceholderText('0')
        self.time_edit.setFixedWidth(80)
        self.time_edit.setStyleSheet(
            'QLineEdit { border: 1px solid #e0e0e0; border-radius: 6px; padding: 4px 8px;'
            ' font-size: 12px; background: #fff; }')
        row.addWidget(lb)
        row.addWidget(self.time_edit)
        row.addStretch()
        card3.body.addLayout(row)

        # 快捷指令（3 个一行，对标 .quick-row flex-wrap）
        lb = QLabel('快捷指令')
        lb.setStyleSheet('color: #999999; font-size: 12px; border: none;'
                         'background: transparent;')
        card3.body.addWidget(lb)
        self._quick_grid = QVBoxLayout()
        self._quick_grid.setSpacing(6)
        card3.body.addLayout(self._quick_grid)
        self.quick_btns = {}
        layout.addWidget(card3)

        # ---- 发送指令（绿色大按钮，对标 .send-btn #07c160）----
        self.send_btn = QPushButton('发送指令')
        self.send_btn.setFixedHeight(42)
        self.send_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.send_btn.setStyleSheet(
            'QPushButton { background: #07c160; color: #fff; font-size: 15px;'
            ' font-weight: 600; border: none; border-radius: 7px; }'
            'QPushButton:hover { background: #06ad56; }'
            'QPushButton:pressed { background: #06ad56; }')
        self.send_btn.clicked.connect(self._on_send)
        layout.addWidget(self.send_btn)

        # ---- 📋 发送日志（橙色左条标题，对标 .log-card）----
        self.log_card = _SectionCard()
        self.log_card.body.addWidget(_section_title('📋 发送日志', color='#ff9500'))
        self._log_layout = QVBoxLayout()
        self._log_layout.setSpacing(3)
        self.log_card.body.addLayout(self._log_layout)
        self.log_card.hide()
        layout.addWidget(self.log_card)
        self._scroll = scroll
        self._apply_mode()

    # ==================== 快捷指令 ====================

    def _apply_mode(self):
        """模式切换：中继转发节显隐 + 快捷指令组重建（对标两套 quick-row）"""
        self.relay_card.setVisible(not self._is_relay)
        while self._quick_grid.count():
            it = self._quick_grid.takeAt(0)
            if it.widget():
                it.widget().deleteLater()
            elif it.layout():
                self._clear_layout(it.layout())
        self.quick_btns = {}

        def btn(text, key, handler):
            b = QPushButton(text)
            b.setCheckable(True)
            b.setFixedHeight(30)
            b.setCursor(Qt.CursorShape.PointingHandCursor)
            b.clicked.connect(handler)
            self.quick_btns[key] = b
            return b

        if self._is_relay:
            defs = [
                ('🕐 工作时间', 'worktime', self._quick_worktime),
                ('ℹ️ 获取信息', 'readinfo', self._quick_readinfo),
                ('📡 发射功率', 'txpower', self._quick_txpower),
                (f'🔘 开关LORA({self._lorasw_val})', 'lorasw', self._quick_lorasw),
                ('🔄 重启中继', 'relayreboot', self._quick_relay_reboot),
                ('⚙️ 配置下发', 'config', self._quick_config),
                (f'🐛 开关Debug({self._debug_val})', 'debug', self._quick_debug),
            ]
        else:
            defs = [
                ('📍 上报GPS', 'upgps', self._quick_upgps),
                (f'📶 上报模式({self._sendmode_val})', 'sendmode', self._quick_sendmode),
                ('📍 位置跟踪', 'b', self._quick_b),
                ('ℹ️ 获取信息', 'readinfo', self._quick_readinfo),
                (f'🔁 设备重启({self._relayreboot_val})', 'relayreboot',
                 self._quick_relay_reboot),
                ('📡 发射功率', 'txpower', self._quick_txpower),
                ('🧪 GPS测试', 'test_gps', self._quick_test_gps),
                ('⚙️ 配置下发', 'config', self._quick_config),
                ('🏠 农场中心', 'c', self._quick_farm_center),
            ]
        row = None
        for i, (text, key, handler) in enumerate(defs):
            if i % 3 == 0:
                row = QHBoxLayout()
                row.setSpacing(6)
                self._quick_grid.addLayout(row)
            b = btn(text, key, handler)
            self._style_quick(b, False)
            row.addWidget(b, stretch=1)

    @staticmethod
    def _clear_layout(layout):
        while layout.count():
            it = layout.takeAt(0)
            if it.widget():
                it.widget().deleteLater()
            elif it.layout():
                CmdPage._clear_layout(it.layout())

    def _style_quick(self, b, active):
        if active:
            b.setStyleSheet('QPushButton { background: #e3f2fd; color: #1989fa;'
                            'border: 1px solid #1989fa; border-radius: 5px;'
                            'font-size: 11px; font-weight: 600; }')
        else:
            b.setStyleSheet('QPushButton { background: #f5f5f5; color: #666666;'
                            'border: 1px solid #e0e0e0; border-radius: 5px;'
                            'font-size: 11px; }'
                            'QPushButton:hover { border-color: #1989fa; }')

    def _fill_cmd(self, cmd_dict, key=None):
        """快捷指令 → 生成 JSON 填入指令内容（不自动发送，对标 mini 行为）"""
        payload = dict(cmd_dict)
        payload['time'] = self._time_value()
        self.cmd_edit.setPlainText(_json.dumps(payload, ensure_ascii=False))
        for k, b in self.quick_btns.items():
            self._style_quick(b, active=(k == key))

    def _time_value(self):
        try:
            return int(self.time_edit.text().strip() or 0)
        except ValueError:
            return 0

    def _quick_upgps(self):
        val = _UPGPS_CYCLE[self._upgps_idx % len(_UPGPS_CYCLE)]
        self._upgps_idx += 1
        self._fill_cmd({'cmd': 'upgps', 'value': val}, 'upgps')

    def _quick_sendmode(self):
        self._sendmode_val = 1 if self._sendmode_val == 0 else 0
        self.quick_btns['sendmode'].setText(f'📶 上报模式({self._sendmode_val})')
        self._fill_cmd({'cmd': 'sendmode', 'value': self._sendmode_val}, 'sendmode')

    def _quick_b(self):
        self._fill_cmd({'cmd': 'B', 'value': '30,5'}, 'b')

    def _quick_readinfo(self):
        self._readinfo_idx += 1
        val = _READINFO_CYCLE[self._readinfo_idx % len(_READINFO_CYCLE)]
        self._fill_cmd({'cmd': 'readinfo', 'value': val}, 'readinfo')

    def _quick_relay_reboot(self):
        # 普通设备页：0/1 循环并在按钮上显示下一次的值；中继页：固定发 1（对标两页差异）
        if self._is_relay:
            self._fill_cmd({'cmd': 'relayreboot', 'value': 1}, 'relayreboot')
        else:
            val = self._relayreboot_val
            self._relayreboot_val = 1 if val == 0 else 0
            self.quick_btns['relayreboot'].setText(f'🔁 设备重启({self._relayreboot_val})')
            self._fill_cmd({'cmd': 'relayreboot', 'value': val}, 'relayreboot')

    def _quick_txpower(self):
        self._fill_cmd({'cmd': 'txpower', 'value': 20}, 'txpower')

    def _quick_test_gps(self):
        self._fill_cmd({'cmd': 'test_gps', 'value': 1}, 'test_gps')

    def _quick_lorasw(self):
        self._lorasw_val = 1 if self._lorasw_val == 0 else 0
        self.quick_btns['lorasw'].setText(f'🔘 开关LORA({self._lorasw_val})')
        self._fill_cmd({'cmd': 'lorasw', 'value': self._lorasw_val}, 'lorasw')

    def _quick_debug(self):
        self._debug_val = 1 if self._debug_val == 0 else 0
        self.quick_btns['debug'].setText(f'🐛 开关Debug({self._debug_val})')
        self._fill_cmd({'cmd': 'debug', 'value': self._debug_val}, 'debug')

    def _quick_config(self):
        device_id = self.target_combo.currentData()
        if not device_id:
            self.notify('请先选择目标设备')
            return
        dlg = _ConfigDialog(self.api, device_id, self._is_relay, self)
        if dlg.exec() and dlg.result_value:
            self._fill_cmd({'cmd': 'A', 'value': dlg.result_value}, 'config')
            self._add_log('info', f'已生成配置指令: {dlg.result_value}')

    def _quick_farm_center(self):
        center = self.api.mapcenter
        if not center:
            self.notify('未获取到农场坐标')
            return
        import re as _re
        parts = _re.split(r'[｜|,，]\s*', str(center))
        if len(parts) < 2:
            self.notify('坐标格式有误')
            return
        try:
            lat, lng = float(parts[0]), float(parts[1])
        except ValueError:
            self.notify('坐标格式有误')
            return
        if lat == 0 and lng == 0:
            self.notify('坐标格式有误')
            return
        self._fill_cmd({'cmd': 'C', 'value': f'{lat:.5f},{lng:.5f}'}, 'c')
        self._add_log('info', f'已生成农场中心指令: {lat:.5f},{lng:.5f}')

    def _quick_worktime(self):
        dlg = _WorkTimeDialog(self)
        if dlg.exec() and dlg.value:
            self._fill_cmd({'cmd': 'worktime', 'value': dlg.value}, 'worktime')

    # ==================== 设备列表与模式 ====================

    def open_for_device(self, device_id):
        """设备详情"设备设置"进入：预选该设备，按 ProductKey 判定普通/中继模式"""
        self._preset_id = device_id
        if self._device_rows:
            self._select_preset()
        else:
            self.refresh_devices(preset_id=device_id)

    def refresh_devices(self, preset_id=None):
        if not self.api.logged_in:
            return
        if preset_id:
            self._preset_id = preset_id
        self.api.run_async(
            lambda: self.api.getDeviceList(),
            self._on_devices, lambda m: self.notify(f'设备列表加载失败: {m}'),
        )

    def _on_devices(self, rows):
        self._device_rows = rows or []
        current = self.target_combo.currentData()
        self.target_combo.blockSignals(True)
        self.target_combo.clear()
        seen = set()
        for row in self._device_rows:
            device_id = str(row.get('deviceId', ''))
            if not device_id or device_id == '-' or device_id in seen:
                continue
            seen.add(device_id)
            rename = str(row.get('rename', '') or '')
            has_key = row.get('ProductKey') and row.get('DeviceName')
            label = f'{device_id} ({rename})' if rename and rename != '-' else device_id
            if not has_key:
                label += ' [无密钥]'
            self.target_combo.addItem(label, device_id)
        self._fill_relay_combo()
        idx = -1
        if getattr(self, '_preset_id', None):
            idx = self.target_combo.findData(self._preset_id)
        if idx < 0 and current:
            idx = self.target_combo.findData(current)
        if idx >= 0:
            self.target_combo.setCurrentIndex(idx)
        self.target_combo.blockSignals(False)
        self._on_target_changed()

    def _fill_relay_combo(self):
        """中继转发下拉：自动 + 带密钥设备（对标 relayDisplayList）"""
        self.relay_combo.blockSignals(True)
        self.relay_combo.clear()
        self.relay_combo.addItem('自动', None)
        for row in self._device_rows:
            if row.get('ProductKey') and row.get('DeviceName'):
                device_id = str(row.get('deviceId', ''))
                rename = str(row.get('rename', '') or '')
                label = f'{device_id} ({rename})' if rename and rename != '-' else device_id
                self.relay_combo.addItem(label, row)
        self.relay_combo.blockSignals(False)

    def _select_preset(self):
        if getattr(self, '_preset_id', None):
            idx = self.target_combo.findData(self._preset_id)
            if idx >= 0:
                self.target_combo.setCurrentIndex(idx)

    def _on_target_changed(self, *_):
        """目标设备变化 → 按 ProductKey 切换普通/中继模式（对标两个页面的分流）"""
        device_id = self.target_combo.currentData()
        device = next((d for d in self._device_rows
                       if str(d.get('deviceId', '')) == device_id), None)
        is_relay = bool(device and device.get('ProductKey') and device.get('DeviceName'))
        if is_relay != self._is_relay:
            self._is_relay = is_relay
            self._apply_mode()
        self.mode_changed.emit(self._is_relay)

    # ==================== 发送 ====================

    def _on_send(self):
        device_id = self.target_combo.currentData()
        if not device_id:
            self.notify('请选择目标设备')
            return
        cmd_text = self.cmd_edit.toPlainText().strip()
        if not cmd_text:
            self.notify('请输入指令内容')
            return
        # tm 不能超过 60；指令 JSON 没有 time 字段时注入默认值（对标 onSend）
        try:
            obj = _json.loads(cmd_text)
            if isinstance(obj, dict):
                if obj.get('tm') is not None and Number_gt(obj['tm'], 60):
                    obj['tm'] = 60
                    cmd_text = _json.dumps(obj, ensure_ascii=False)
                    self._add_log('info', 'tm超限，已自动调整为60')
                if 'time' not in obj:
                    obj['time'] = self._time_value()
                    cmd_text = _json.dumps(obj, ensure_ascii=False)
        except (ValueError, TypeError):
            pass   # 非 JSON 跳过校验

        # 手动选中继 → 直接用它发；否则设备自带密钥直发；再否则自动查最优中继
        relay_row = self.relay_combo.currentData() if not self._is_relay else None
        if relay_row:
            self._add_log('info', f'中继: {relay_row.get("deviceId", "")}')
            self._do_send(relay_row, device_id, cmd_text)
            return
        self._resolve_send(device_id, cmd_text)

    @staticmethod
    def _gt60(v):
        try:
            return float(v) > 60
        except (TypeError, ValueError):
            return False

    def _resolve_send(self, device_id, cmd_text):
        """自动路径：设备有密钥直发；否则查 RSSI 最优至多 2 台中继各发一条"""
        def load():
            devices = self._device_rows or self.api.getDeviceList()
            device = next((d for d in devices
                           if str(d.get('deviceId', '')) == device_id), {})
            if device.get('ProductKey') and device.get('DeviceName'):
                return [device]
            relays = self.api.getDeviceBestRssibyId(device_id, limit=10)
            device_map = {str(d.get('deviceId', '')): d for d in devices}
            targets = []
            for r in relays[:2]:
                relay = device_map.get(str(r.get('upDateDevice', '')))
                if relay and relay.get('ProductKey') and relay.get('DeviceName') \
                        and relay not in targets:
                    targets.append(relay)
            return targets

        def on_targets(targets):
            if not targets:
                self._add_log('error', '该设备当天无记录且无可用中继，无法发送')
                self.notify('没有可用发送路径（设备无密钥且无中继）')
                return
            for relay in targets:
                self._do_send(relay, device_id, cmd_text)

        self.api.run_async(load, on_targets, lambda m: self.notify(f'解析发送路径失败: {m}'))

    def _do_send(self, relay, device_id, cmd_text):
        """经中继密钥下发一条指令并记日志"""
        def work():
            payload = _safe_json(cmd_text) or {'text': cmd_text}
            payload['deviceId'] = device_id
            self.api.sendtodtucmd(relay['DeviceName'], relay['ProductKey'], payload)
            return relay
        self.api.run_async(
            work,
            on_ok=lambda r: self._add_log('success',
                                          f'{cmd_text} → 经 {r.get("deviceId", "")} 下发'),
            on_err=lambda m: self._add_log('error', f'{cmd_text} 发送失败: {m}'),
        )

    # ==================== 日志 ====================

    def _add_log(self, log_type, text):
        """发送日志：[HH:MM:SS] ✓/✗/→ 文本（对标 .log-item）"""
        self.log_card.show()
        mark = {'success': '✓', 'error': '✗'}.get(log_type, '→')
        color = {'success': '#07c160', 'error': '#fa5151'}.get(log_type, '#1989fa')
        row = QHBoxLayout()
        row.setSpacing(4)
        lb_time = QLabel(f'[{_time.strftime("%H:%M:%S")}]')
        lb_time.setStyleSheet('color: #999999; font-size: 11px; border: none;'
                              'background: transparent;')
        lb_mark = QLabel(mark)
        lb_mark.setStyleSheet(f'color: {color}; font-size: 12px; font-weight: 600;'
                              'border: none; background: transparent;')
        lb_text = QLabel(text)
        lb_text.setWordWrap(True)
        lb_text.setStyleSheet('color: #333333; font-size: 11px; border: none;'
                              'background: transparent;')
        row.addWidget(lb_time)
        row.addWidget(lb_mark)
        row.addWidget(lb_text, stretch=1)
        holder = QWidget()
        holder.setStyleSheet('border-bottom: 1px solid #fafafa; background: transparent;')
        holder.setLayout(row)
        self._log_layout.addWidget(holder)
        # 最多保留 50 条
        while self._log_layout.count() > 50:
            it = self._log_layout.takeAt(0)
            if it.widget():
                it.widget().deleteLater()
        from PyQt6.QtCore import QTimer
        QTimer.singleShot(0, lambda: self._scroll.verticalScrollBar().setValue(
            self._scroll.verticalScrollBar().maximum()))


def Number_gt(v, n):
    try:
        return float(v) > n
    except (TypeError, ValueError):
        return False


def _safe_json(text):
    try:
        obj = _json.loads(text)
        return obj if isinstance(obj, dict) else None
    except ValueError:
        return None
