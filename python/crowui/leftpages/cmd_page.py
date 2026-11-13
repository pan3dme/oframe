# -*- coding: utf-8 -*-
"""DTU 指令页（对标小程序 pages/dtu-cmd + pages/relay-dtu-cmd + settings 的中继广播）
- 普通设备指令：A(配置) B(对时) C(农场中心) upgps sendmode readinfo txpower test_gps relayreboot
- 中继指令：A(周期,开机窗) readinfo txpower lorasw relayreboot debug worktime
- 发送路径：设备自带三元组直发；否则 RSSI 最优至多 2 台中继转发
"""

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                             QComboBox, QLineEdit, QSpinBox, QCheckBox, QGroupBox,
                             QFormLayout, QMessageBox)
from PyQt6.QtCore import Qt

from crowui import theme, wx_codec
from crowui.wx_api import WxApi


class CmdPage(QWidget):
    def __init__(self, api: WxApi, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[指令]', msg))
        self._device_rows = []
        self._setup_ui()
        self.refresh_devices()

    # ==================== UI ====================

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(10)

        title = QLabel("DTU 指令下发")
        title.setFont(theme.font(13, bold=True))
        title.setStyleSheet(f"color: {theme.TEXT_MAIN};")
        layout.addWidget(title)

        # 目标设备
        form = QFormLayout()
        form.setSpacing(8)
        self.target_combo = QComboBox()
        self.target_combo.setStyleSheet(
            f"QComboBox {{ border: 1px solid {theme.CARD_BORDER}; border-radius: 6px; padding: 4px 8px; }}")
        form.addRow("目标设备:", self.target_combo)
        self.relay_check = QCheckBox("自动选择最优中继转发（设备无密钥时）")
        self.relay_check.setFont(theme.font(10))
        self.relay_check.setChecked(True)
        form.addRow("", self.relay_check)
        layout.addLayout(form)

        # 快捷指令
        cmd_group = QGroupBox("快捷指令")
        cmd_group.setFont(theme.font(11, bold=True))
        cmd_layout = QVBoxLayout(cmd_group)
        cmd_layout.setSpacing(8)

        # A 配置下发
        cfg_row = QHBoxLayout()
        cfg_row.addWidget(QLabel("配置:"))
        self.period_spin = QSpinBox(); self.period_spin.setRange(1, 1440); self.period_spin.setValue(30)
        self.power_start = QSpinBox(); self.power_start.setRange(0, 23); self.power_start.setValue(8)
        self.power_end = QSpinBox(); self.power_end.setRange(1, 24); self.power_end.setValue(20)
        self.gps_start = QSpinBox(); self.gps_start.setRange(0, 23); self.gps_start.setValue(8)
        self.gps_end = QSpinBox(); self.gps_end.setRange(1, 24); self.gps_end.setValue(20)
        self.main_period = QSpinBox(); self.main_period.setRange(0, 10); self.main_period.setValue(6)
        for w, tip in ((self.period_spin, "周期(分)"), (self.power_start, "开机起"), (self.power_end, "开机止"),
                       (self.gps_start, "GPS起"), (self.gps_end, "GPS止"), (self.main_period, "主周期")):
            w.setToolTip(tip)
            cfg_row.addWidget(w)
        a_btn = QPushButton("下发A")
        a_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        a_btn.clicked.connect(self._send_a)
        cfg_row.addWidget(a_btn)
        cmd_layout.addLayout(cfg_row)

        # 常用开关按钮（每行4个）
        def btn(text, handler):
            b = QPushButton(text)
            b.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
            b.clicked.connect(handler)
            return b
        row1 = QHBoxLayout()
        row1.addWidget(btn("upgps 上报", self._send_upgps))
        row1.addWidget(btn("对时 B", lambda: self._send_simple('B', '30,5')))
        row1.addWidget(btn("sendmode 0/1", lambda: self._send_toggle('sendmode')))
        row1.addWidget(btn("readinfo", lambda: self._send_cycle('readinfo', ['0', '1', '2'])))
        cmd_layout.addLayout(row1)

        row2 = QHBoxLayout()
        row2.addWidget(btn("txpower 20", lambda: self._send_simple('txpower', 20)))
        row2.addWidget(btn("test_gps", lambda: self._send_simple('test_gps', 1)))
        row2.addWidget(btn("relayreboot", lambda: self._send_toggle('relayreboot')))
        row2.addWidget(btn("lorasw 0/1", lambda: self._send_toggle('lorasw')))
        cmd_layout.addLayout(row2)

        row3 = QHBoxLayout()
        row3.addWidget(btn("debug 0/1", lambda: self._send_toggle('debug')))
        row3.addWidget(btn("worktime", self._send_worktime))
        row3.addWidget(btn("C 农场中心", self._send_center))
        row3.addWidget(btn("mapcenter 广播", self._broadcast_mapcenter))
        cmd_layout.addLayout(row3)
        layout.addWidget(cmd_group)

        # 自定义指令
        custom_group = QGroupBox("自定义指令 JSON")
        custom_group.setFont(theme.font(11, bold=True))
        custom_layout = QVBoxLayout(custom_group)
        self.cmd_edit = QLineEdit()
        self.cmd_edit.setPlaceholderText('{"cmd":"xxx","value":1}')
        self.cmd_edit.setStyleSheet(
            f"QLineEdit {{ border: 1px solid {theme.CARD_BORDER}; border-radius: 6px; padding: 6px; }}")
        send_btn = QPushButton("发送自定义指令")
        send_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        send_btn.clicked.connect(self._send_custom)
        custom_layout.addWidget(self.cmd_edit)
        custom_layout.addWidget(send_btn)
        layout.addWidget(custom_group)

        self.status_label = QLabel("")
        self.status_label.setFont(theme.font(10))
        self.status_label.setStyleSheet(f"color: {theme.TEXT_SECONDARY};")
        self.status_label.setWordWrap(True)
        layout.addWidget(self.status_label)
        layout.addStretch()

    # ==================== 工具 ====================

    def refresh_devices(self):
        if not self.api.logged_in:
            return
        self.api.run_async(
            lambda: self.api.getDeviceList(),
            self._on_devices, lambda m: self.notify(f"设备列表加载失败: {m}"),
        )

    def _on_devices(self, rows):
        self._device_rows = rows
        current = self.target_combo.currentData()
        self.target_combo.clear()
        for row in rows:
            device_id = str(row.get('deviceId', ''))
            rename = row.get('rename', '')
            tag = ' [中继]' if row.get('ProductKey') else ''
            self.target_combo.addItem(f"{device_id}{tag}" + (f" ({rename})" if rename else ''), device_id)
        if current:
            idx = self.target_combo.findData(current)
            if idx >= 0:
                self.target_combo.setCurrentIndex(idx)

    def _resolve_send_targets(self, device_id, on_targets):
        """解析发送路径：设备自带密钥直发；否则查最优中继转发（至多2台）"""
        def load():
            devices = self.api.getDeviceList() if not self._device_rows else self._device_rows
            device = next((d for d in devices if str(d.get('deviceId', '')) == device_id), {})
            targets = []
            if device.get('ProductKey') and device.get('DeviceName'):
                targets.append(device)
            if not targets and self.relay_check.isChecked():
                relays = self.api.getDeviceBestRssibyId(device_id, limit=10)
                device_map = {str(d.get('deviceId', '')): d for d in devices}
                for r in relays[:2]:
                    relay = device_map.get(str(r.get('upDateDevice', '')))
                    if relay and relay.get('ProductKey') and relay.get('DeviceName') \
                            and relay not in targets:
                        targets.append(relay)
            return targets
        self.api.run_async(load, on_targets,
                           lambda m: self.notify(f"解析发送路径失败: {m}"))

    def _dispatch(self, cmd_dict):
        """下发指令到当前选中的目标设备"""
        if not self.api.logged_in:
            self.notify("请先登录")
            return
        device_id = self.target_combo.currentData()
        if not device_id:
            self.notify("请选择目标设备")
            return

        def send(targets):
            if not targets:
                self.notify("没有可用发送路径（设备无密钥且无中继）")
                return
            for relay in targets:
                cmd = dict(cmd_dict)
                cmd.setdefault('deviceId', device_id)
                self.api.run_async(
                    lambda r=relay, c=cmd: self.api.sendtodtucmd(r['DeviceName'], r['ProductKey'], c),
                    on_ok=lambda _, r=relay: self.notify(
                        f"✓ {cmd_dict.get('cmd')} → 经 {r.get('deviceId', '')} 下发"),
                    on_err=lambda m: self.notify(f"下发失败: {m}"),
                )
        self._resolve_send_targets(device_id, send)

    # ==================== 各指令 ====================

    def _send_a(self):
        """A 配置：普通设备 "周期,开机代号,GPS代号,主周期"；中继 "周期,开机代号" """
        device_id = self.target_combo.currentData()
        is_relay = next((str(d.get('deviceId', '')) == device_id and d.get('ProductKey')
                         for d in self._device_rows), False)
        period = self.period_spin.value()
        power_code = wx_codec.encode_time_window(self.power_start.value(), self.power_end.value())
        if is_relay:
            value = f"{period},{power_code}"
        else:
            gps_code = wx_codec.encode_time_window(self.gps_start.value(), self.gps_end.value())
            main = self.main_period.value()
            value = f"{period},{power_code},{gps_code}" + (f",{main}" if main else '')
        self._dispatch({'cmd': 'A', 'value': value})

    def _send_upgps(self):
        """upgps：按目标设备配置算上报间隔（工作/大周期）"""
        device_id = self.target_combo.currentData()
        def load():
            cfg = self.api.getDeviceConfigById(device_id)
            return wx_codec.calc_upgps_value(cfg.get('lorastr', ''))
        def on_ok(value):
            self._dispatch({'cmd': 'upgps', 'value': value or 0})
            self.notify(f"upgps 取值: {value or 0} 分钟")
        self.api.run_async(load, on_ok, lambda m: self.notify(f"取配置失败: {m}"))

    def _send_simple(self, cmd, value):
        self._dispatch({'cmd': cmd, 'value': value})

    def _send_toggle(self, cmd):
        """0/1 切换类指令（页内记忆上次值）"""
        key = f'_toggle_{cmd}'
        next_val = 0 if getattr(self, key, 0) else 1
        setattr(self, key, next_val)
        self._dispatch({'cmd': cmd, 'value': next_val})

    def _send_cycle(self, cmd, values):
        """循环取值类指令"""
        key = f'_cycle_{cmd}'
        idx = getattr(self, key, -1) + 1
        setattr(self, key, idx)
        self._dispatch({'cmd': cmd, 'value': values[idx % len(values)]})

    def _send_worktime(self):
        """worktime：弹窗输入 HH:mm-HH:mm"""
        from PyQt6.QtWidgets import QInputDialog
        text, ok = QInputDialog.getText(self, "中继工作时间", "格式 HH:mm-HH:mm（≥5小时）:",
                                        text="08:00-20:00")
        if ok and text.strip():
            self._dispatch({'cmd': 'worktime', 'value': text.strip()})

    def _send_center(self):
        """C 农场中心：value="lat,lng"（5位小数，取登录 mapcenter）"""
        center = self.api.mapcenter
        if not center:
            self.notify("登录数据中没有农场中心坐标")
            return
        try:
            lat, lng = [f"{float(v):.5f}" for v in center.split(',')]
        except ValueError:
            self.notify(f"mapcenter 格式异常: {center}")
            return
        self._dispatch({'cmd': 'C', 'value': f"{lat},{lng}"})

    def _broadcast_mapcenter(self):
        """mapcenter 广播：发给所有中继（对标小程序设置页）"""
        if not self.api.mapcenter:
            self.notify("登录数据中没有农场中心坐标")
            return
        relays = [d for d in self._device_rows if d.get('ProductKey') and d.get('DeviceName')]
        if not relays:
            self.notify("没有带密钥的中继")
            return
        for relay in relays:
            cmd = {'cmd': 'mapcenter', 'value': self.api.mapcenter, 'deviceId': relay['deviceId']}
            self.api.run_async(
                lambda r=relay, c=cmd: self.api.sendtodtucmd(r['DeviceName'], r['ProductKey'], c),
                on_ok=lambda _, r=relay: self.notify(f"mapcenter → {r.get('deviceId', '')}"),
                on_err=lambda m: self.notify(f"mapcenter 失败: {m}"),
            )
        self.notify(f"mapcenter 广播: 共 {len(relays)} 台中继")

    def _send_custom(self):
        import json as _json
        raw = self.cmd_edit.text().strip()
        if not raw:
            return
        try:
            cmd = _json.loads(raw)
        except _json.JSONDecodeError as e:
            QMessageBox.warning(self, "格式错误", f"JSON 解析失败: {e}")
            return
        self._dispatch(cmd)
