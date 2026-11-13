# -*- coding: utf-8 -*-
"""登录页（对标小程序 pages/login/login.js）
- 预填上次账密（last_login，注销后保留）
- 自动登录勾选（setting_auto_login，持久化）
- 登录成功保存 login_info（自动登录免密依据）
- 支持启动时自动登录失败的错误回显
"""

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QLabel, QLineEdit,
                             QPushButton, QFormLayout, QCheckBox)
from PyQt6.QtCore import Qt

from crowui import theme, session_store
from crowui.wx_api import WxApi


class LoginPage(QWidget):
    """账号密码登录；成功后保存 wechatid/level/mapcenter 与本地会话"""

    def __init__(self, api: WxApi, on_login_success=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.on_login_success = on_login_success
        self._login_in_progress = False
        self._setup_ui()
        self._prefill()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 30, 20, 20)
        layout.setSpacing(12)

        title = QLabel("账号登录")
        title.setFont(theme.font(16, bold=True))
        title.setStyleSheet(f"color: {theme.TEXT_MAIN};")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)

        self.tip_label = QLabel("与微信小程序共用同一账号体系")
        self.tip_label.setFont(theme.font(9))
        self.tip_label.setStyleSheet(f"color: {theme.TEXT_MUTED};")
        self.tip_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.tip_label)

        form = QFormLayout()
        form.setSpacing(10)
        self.user_edit = QLineEdit()
        self.user_edit.setPlaceholderText("账号")
        self.pass_edit = QLineEdit()
        self.pass_edit.setPlaceholderText("密码")
        self.pass_edit.setEchoMode(QLineEdit.EchoMode.Password)
        for edit in (self.user_edit, self.pass_edit):
            edit.setFont(theme.font(11))
            edit.setStyleSheet(
                f"QLineEdit {{ border: 1px solid {theme.CARD_BORDER}; border-radius: 6px;"
                f" padding: 8px; }}"
                f"QLineEdit:focus {{ border: 1px solid {theme.PRIMARY}; }}")
        form.addRow("账号:", self.user_edit)
        form.addRow("密码:", self.pass_edit)
        layout.addLayout(form)

        # 自动登录（对标小程序登录页复选框）
        self.auto_login_check = QCheckBox("自动登录（下次打开免密进入）")
        self.auto_login_check.setFont(theme.font(10))
        self.auto_login_check.setStyleSheet(f"color: {theme.TEXT_SECONDARY};")
        self.auto_login_check.setChecked(session_store.get_auto_login())
        layout.addWidget(self.auto_login_check)

        self.login_btn = QPushButton("登 录")
        self.login_btn.setFont(theme.font(12, bold=True))
        self.login_btn.setFixedHeight(40)
        self.login_btn.setStyleSheet(theme.GREEN_BUTTON_QSS)
        self.login_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.login_btn.clicked.connect(self._do_login)
        layout.addWidget(self.login_btn)

        self.status_label = QLabel("")
        self.status_label.setFont(theme.font(10))
        self.status_label.setWordWrap(True)
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.status_label)

        layout.addStretch()

    def _prefill(self):
        """预填上次账密（优先取登录态里的用户名）"""
        last = session_store.get_last_login()
        if last:
            self.user_edit.setText(str(last.get('username', '')))
            self.pass_edit.setText(str(last.get('password', '')))

    def set_error(self, msg):
        """外部（自动登录失败）回显错误"""
        self.status_label.setText(msg)

    def try_auto_login(self, on_success):
        """启动时自动登录：有本地账密且勾选了自动登录才尝试；返回是否已发起"""
        if not session_store.get_auto_login():
            return False
        last = session_store.get_last_login()
        if not last:
            return False
        username, password = last['username'], last['password']
        if not username or not password:
            return False
        self._login_in_progress = True
        self.login_btn.setEnabled(False)
        self.status_label.setText("自动登录中...")

        def job():
            self.api.login(username, password)
        self.api.run_async(job, on_ok=lambda _: self._finish_login_ok(),
                           on_err=lambda m: self._finish_login_err(m, username, password))
        return True

    def _do_login(self):
        if self._login_in_progress:
            return
        username = self.user_edit.text().strip()
        password = self.pass_edit.text().strip()
        if not username or not password:
            self.status_label.setText("请输入账号和密码")
            return
        # 登录是单行 OTS 查询（百毫秒级），直接同步执行，不走线程，
        # 彻底排除异步回调丢失导致界面卡在"登录中..."的可能
        self._login_in_progress = True
        self.login_btn.setEnabled(False)
        self.status_label.setText("登录中...")
        try:
            self.api.login(username, password)
        except Exception as e:
            self._finish_login_err(str(e), username, password)
            return
        self._finish_login_ok()

    def _finish_login_ok(self):
        """登录成功：保存会话（login_info/last_login/auto_login）→ 回调"""
        session_store.set_login_info(
            self.api.username, self.api.password,
            self.api.wechatid, self.api.level, self.api.mapcenter)
        session_store.set_last_login(self.api.username, self.api.password)
        session_store.set_auto_login(self.auto_login_check.isChecked())
        self._login_in_progress = False
        self.login_btn.setEnabled(True)
        self.status_label.setText(
            f"登录成功  wechatid: {self.api.wechatid}  权限: {self.api.level}\n"
            f"农场中心: {self.api.mapcenter or '未设置'}")
        if self.on_login_success:
            self.on_login_success()

    def _finish_login_err(self, msg, username='', password=''):
        self._login_in_progress = False
        self.login_btn.setEnabled(True)
        self.status_label.setText(f"登录失败: {msg}")
        if username:
            session_store.set_last_login(username, password)
