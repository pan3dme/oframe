# -*- coding: utf-8 -*-
"""功能列表聚合页（对标小程序 pages/features/features.wxml + wxss）
- 白色卡片 + 绿色左边框标题
- 3 列功能网格：绿色圆形编号(56rpx=28px) + 文字标签(22rpx=11px)，
  浅绿渐变底 #f0faf5→#e8f8ef、描边 #d4edda、圆角 14rpx，level 门控与小程序一致
- "上报设备LORA" 弹出插入记录弹窗（对标 .track-modal：取消灰/确认绿，高80rpx）
- "设置" 子页：账号信息 + 清理缓存 + 注销登入
"""

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QLabel,
                             QPushButton, QComboBox, QLineEdit, QDialog, QMessageBox)
from PyQt6.QtCore import Qt, pyqtSignal

from crowui import theme
from crowui.wx_api import WxApi


# 功能入口定义（与小程序 features.js featureBtns 完全一致：id/label/minLevel）
# minLevel = 显示所需的最大 level（用户 level <= minLevel 才显示）
FEATURES = [
    (1,  '最近10条记录',  1, 'records'),
    (2,  '上报设备LORA',  1, 'insert'),
    (3,  '设置',          2, 'settings'),
    (4,  '管理牛羊',      1, 'livestock'),
    (5,  '道路管理',      2, 'roads'),
    (6,  '地名管理',      2, 'roads'),
    (7,  '连接蓝牙',      2, 'bluetooth'),
    (8,  '道路优化',      1, 'optimize'),
    (9,  'DTU发送指令',   1, 'cmd'),
    (10, '电量走势',      1, 'battery'),
]

_FEATURE_BTN_QSS = f"""
    QPushButton {{
        background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
            stop:0 {theme.GREEN_BG_A}, stop:1 {theme.GREEN_BG_B});
        border: 1px solid {theme.GREEN_BORDER};
        border-radius: 7px;
        padding: 10px 4px 8px;
    }}
    QPushButton:hover {{
        background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
            stop:0 #D4EDDA, stop:1 #C3E6CB);
    }}
    QPushButton:pressed {{
        background-color: #C3E6CB;
    }}
    QPushButton:disabled {{
        color: {theme.TEXT_MUTED};
    }}
"""


class FeaturesPage(QWidget):
    """功能入口聚合页（3列网格，绿色编号圆，level 门控）"""

    feature_selected = pyqtSignal(str)   # 功能键：records/insert/settings/livestock/roads/cmd/battery

    def __init__(self, api: WxApi, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[功能列表]', msg))
        self._feature_btns = {}
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(0)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        # 白色卡片（对标 .feature-panel：圆角16rpx、阴影、内边距20rpx）
        panel = QWidget()
        panel.setObjectName('featurePanel')
        panel.setStyleSheet(f"""
            QWidget#featurePanel {{
                background: {theme.CARD_BG};
                border-radius: 8px;
                border: 1px solid {theme.CARD_BORDER};
            }}
        """)
        panel_layout = QVBoxLayout(panel)
        panel_layout.setContentsMargins(10, 10, 10, 12)
        panel_layout.setSpacing(10)

        # 标题（绿色左边框，32rpx 加粗）
        title = QLabel('功能列表')
        title.setFont(theme.font(13, bold=True))
        title.setStyleSheet(f"""
            color: #333333; border-left: 3px solid {theme.GREEN};
            padding-left: 4px; border-top: none; border-right: none; border-bottom: none;
        """)
        panel_layout.addWidget(title)

        # 3 列网格（对标 .feature-grid / .feature-btn）
        grid = QGridLayout()
        grid.setSpacing(8)
        for i, (fid, label, min_level, key) in enumerate(FEATURES):
            btn = QPushButton()
            btn.setFont(theme.font(10))
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setStyleSheet(_FEATURE_BTN_QSS)
            btn.setFixedHeight(64)
            # 绿色圆形编号 + 换行标签（对标 .feature-num 56rpx 圆 + .feature-label 22rpx）
            btn.setText(f'{fid}\n{label}')
            btn.clicked.connect(lambda _, k=key, l=label: self._on_feature_tap(k, l))
            grid.addWidget(btn, i // 3, i % 3, Qt.AlignmentFlag.AlignTop)
            self._register_level_gate(btn, fid, min_level)
        for col in range(3):
            grid.setColumnStretch(col, 1)
        panel_layout.addLayout(grid)
        layout.addWidget(panel)

    def _register_level_gate(self, btn, fid, min_level):
        self._feature_btns[fid] = (btn, min_level)

    def _on_feature_tap(self, key, label):
        # 未实现的功能给提示（对标小程序桌面端缺失能力）
        if key == 'bluetooth':
            self.notify('连接蓝牙为移动端功能，桌面端暂不支持')
            return
        if key == 'optimize':
            self.notify('道路优化暂未在桌面端实现')
            return
        if key == 'insert':
            self._open_insert_dialog()
            return
        self.feature_selected.emit(key)

    # ==================== level 门控 ====================

    def refresh(self, focus_key=None):
        """进入时按用户 level 过滤功能按钮（对标 features.js filterFeatureBtns）"""
        level = self.api.level if self.api.logged_in else 99
        for fid, (btn, min_level) in self._feature_btns.items():
            btn.setVisible(level <= min_level)

    # ==================== 插入记录弹窗（对标 .track-modal） ====================

    def _open_insert_dialog(self):
        if not self.api.logged_in:
            self.notify('请先登录')
            return
        dlg = InsertRecordDialog(self.api, self)
        dlg.exec()


class InsertRecordDialog(QDialog):
    """插入一条记录弹窗（对标 features.wxml .track-dialog：picker + input + 取消/确认）"""

    def __init__(self, api: WxApi, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = lambda msg: print('[插入记录]', msg)
        self.setWindowTitle('插入一条记录')
        self.setModal(True)
        self.setFixedWidth(300)
        self._device_ids = []
        self._setup_ui()
        self._load_devices()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 16)
        layout.setSpacing(8)

        title = QLabel('插入一条记录')
        title.setFont(theme.font(12, bold=True))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title.setStyleSheet('color: #333333;')
        layout.addWidget(title)

        sub1 = QLabel('请选择设备')
        sub1.setFont(theme.font(9))
        sub1.setStyleSheet(f'color: #999999;')
        sub1.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(sub1)

        self.device_combo = QComboBox()
        self.device_combo.setFont(theme.font(10))
        self.device_combo.setFixedHeight(40)
        self.device_combo.setStyleSheet(f"""
            QComboBox {{
                border: 1px solid #E0E0E0; border-radius: 6px; padding: 0 12px;
                background: white; color: #333333;
            }}
        """)
        layout.addWidget(self.device_combo)

        sub2 = QLabel('请输入LoRa数据（lorastr）')
        sub2.setFont(theme.font(9))
        sub2.setStyleSheet('color: #999999;')
        sub2.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(sub2)

        self.lora_edit = QLineEdit()
        self.lora_edit.setPlaceholderText('例如：1|v3-1|26.000000,109.360000|1')
        self.lora_edit.setFixedHeight(40)
        self.lora_edit.setFont(theme.font(10))
        self.lora_edit.setStyleSheet(
            'QLineEdit { border: 1px solid #E0E0E0; border-radius: 6px; padding: 0 12px; }'
            'QLineEdit:focus { border: 1px solid ' + theme.GREEN + '; }')
        layout.addWidget(self.lora_edit)

        layout.addSpacing(8)

        # 按钮行（取消灰 / 确认绿，高80rpx=40px）
        btns = QHBoxLayout()
        btns.setSpacing(12)
        cancel_btn = QPushButton('取消')
        cancel_btn.setFixedHeight(40)
        cancel_btn.setStyleSheet(theme.GRAY_BUTTON_QSS)
        cancel_btn.clicked.connect(self.reject)
        confirm_btn = QPushButton('确认插入')
        confirm_btn.setFixedHeight(40)
        confirm_btn.setStyleSheet(theme.GREEN_BUTTON_QSS)
        confirm_btn.clicked.connect(self._confirm)
        btns.addWidget(cancel_btn, stretch=1)
        btns.addWidget(confirm_btn, stretch=1)
        layout.addLayout(btns)

    def _load_devices(self):
        def load():
            return self.api.getDeviceList()

        def on_ok(devices):
            self.device_combo.clear()
            self._device_ids = []
            for d in devices:
                did = str(d.get('deviceId', ''))
                rename = str(d.get('rename', ''))
                self.device_combo.addItem(f'{did} ({rename})' if rename else did, did)
                self._device_ids.append(did)

        self.api.run_async(load, on_ok, self.notify)

    def _confirm(self):
        device_id = self.device_combo.currentData()
        lorastr = self.lora_edit.text().strip()
        if not device_id:
            self.notify('请选择设备')
            return
        if not lorastr:
            self.notify('请输入LoRa数据')
            return
        # 插入是单行写入（毫秒级），直接同步执行
        try:
            self.api.insertlog(device_id, lorastr)
        except Exception as e:
            QMessageBox.warning(self, '插入失败', str(e))
            return
        self.accept()


class SettingsPage(QWidget):
    """设置子页（对标小程序 pages/settings/settings.js）：账号信息 + 清理缓存 + 注销登入"""

    logout_requested = pyqtSignal()

    def __init__(self, api: WxApi, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[设置]', msg))
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 6, 10, 10)
        layout.setSpacing(10)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)

        # 账号信息卡（白色圆角卡片）
        card = QLabel()
        card.setFont(theme.font(10))
        card.setWordWrap(True)
        card.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
        card.setStyleSheet(f"""
            background: {theme.CARD_BG}; border-radius: 8px;
            border: 1px solid {theme.CARD_BORDER}; padding: 14px;
            color: {theme.TEXT_SECONDARY};
        """)
        self.account_label = card
        layout.addWidget(card)

        # 注销登入（红色，对标 index 页红色注销按钮）
        logout_btn = QPushButton('注销登入')
        logout_btn.setFixedHeight(40)
        logout_btn.setStyleSheet(theme.DANGER_BUTTON_QSS)
        logout_btn.clicked.connect(self.logout_requested.emit)
        layout.addWidget(logout_btn)
        layout.addStretch()

    def refresh(self, focus_key=None):
        api = self.api
        self.account_label.setText(
            f'当前账号: {api.username}\n'
            f'wechatid: {api.wechatid}    权限等级: {api.level}\n'
            f'农场中心: {api.mapcenter or "未设置"}')
