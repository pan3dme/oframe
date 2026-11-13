# -*- coding: utf-8 -*-
"""
左侧功能侧栏（对标微信小程序的导航与登录流程）：

登录流程（与 app.js/initLogin + index._checkLogin 一致）：
- 启动时：勾选了自动登录且有本地账密 → 静默登录 → 直接进主界面；失败回登录页并显示原因
- 未勾选/无账密 → 登录页（预填上次账密）
- 注销登入：确认后清除 login_info（保留 last_login 供预填）→ 回登录页

导航（与 app.json tabBar 一致，底部 4 个主 Tab）：
- 设备详情（默认首页，双击设备列表行进入）
- 设备列表
- 功能列表（level<=2 可见，聚合页内跳转各子功能，带返回）
- 地图中心
页面通过直接持有的 GoogleScene3D 引用操控 2D/3D 显示。
"""

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                             QStackedWidget, QMessageBox)
from PyQt6.QtCore import Qt

from crowui import theme, session_store
from crowui.wx_api import WxApi
from crowui.leftpages.login_page import LoginPage
from crowui.leftpages.device_page import (DeviceListPage, DeviceDetailPage,
                                          load_home_selected, save_home_selected)
from crowui.leftpages.features_page import FeaturesPage, SettingsPage
from crowui.leftpages.map_panel import MapPanel
from crowui.leftpages.cmd_page import CmdPage
from crowui.leftpages.livestock_page import LivestockPage
from crowui.leftpages.records_page import RecordsPage, BatteryPage
from crowui.leftpages.roads_places_page import RoadsPlacesPage

# 内容栈页面索引（主 Tab）
PAGE_DETAIL, PAGE_LIST, PAGE_FEATURES, PAGE_MAP = range(4)
# 子功能页索引（功能列表进入，带返回）
PAGE_LIVESTOCK, PAGE_RECORDS, PAGE_BATTERY, PAGE_CMD, PAGE_ROADS, PAGE_SETTINGS = range(4, 10)


class LeftPanel(QWidget):
    """左侧功能侧栏（登录 → 底部4Tab导航 → 功能子页）"""

    def __init__(self, api: WxApi, scene, parent=None):
        super().__init__(parent)
        self.api = api
        self.scene = scene
        self.setMinimumWidth(430)
        self._setup_ui()
        self._startup_auto_login()

    def _notify(self, msg):
        """全局轻提示"""
        print("[侧栏]", msg)

    # ==================== UI 搭建 ====================

    def _setup_ui(self):
        self.root_stack = QStackedWidget(self)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)
        outer.addWidget(self.root_stack)

        # ---------- 登录层 ----------
        self.login_page = LoginPage(self.api, on_login_success=self._on_login_success)
        self.root_stack.addWidget(self.login_page)

        # ---------- 主界面层 ----------
        self.main_view = QWidget()
        main_layout = QVBoxLayout(self.main_view)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)

        self.content = QStackedWidget()
        main_layout.addWidget(self.content, stretch=1)

        # 主 Tab 页
        self.detail_page = DeviceDetailPage(self.api, self.scene, notify=self._notify)
        self.content.addWidget(self.detail_page)                      # 0 设备详情
        self.list_page = DeviceListPage(self.api, notify=self._notify)
        self.content.addWidget(self.list_page)                        # 1 设备列表
        self.features_page = FeaturesPage(self.api, notify=self._notify)
        self.content.addWidget(self.features_page)                    # 2 功能列表
        self.map_panel = MapPanel(self.api, self.scene, notify=self._notify)
        self.content.addWidget(self.map_panel)                        # 3 地图中心

        # 功能子页（带"返回功能列表"顶栏）
        self.livestock_page = self._wrap_sub(LivestockPage(self.api, notify=self._notify), "牛羊管理")
        self.content.addWidget(self.livestock_page)                   # 4
        self.records_page = self._wrap_sub(RecordsPage(self.api, notify=self._notify), "云端记录")
        self.content.addWidget(self.records_page)                     # 5
        self.battery_page = self._wrap_sub(BatteryPage(self.api, notify=self._notify), "电量走势")
        self.content.addWidget(self.battery_page)                     # 6
        self.cmd_page = self._wrap_sub(CmdPage(self.api, notify=self._notify), "设备DTU指令")
        self.content.addWidget(self.cmd_page)                         # 7
        self.roads_page = self._wrap_sub(RoadsPlacesPage(self.api, self.scene, notify=self._notify), "道路地名")
        self.content.addWidget(self.roads_page)                       # 8
        self.settings_page = self._wrap_sub(SettingsPage(self.api, notify=self._notify), "设置")
        self.content.addWidget(self.settings_page)                    # 9

        # ---------- 底部导航栏（4 主 Tab，对标小程序 tabBar） ----------
        main_layout.addWidget(self._build_navbar())

        self.root_stack.addWidget(self.main_view)
        self.root_stack.setCurrentWidget(self.login_page)

        # ---------- 信号连接 ----------
        self.list_page.detail_requested.connect(self._open_detail_from_list)
        self.list_page.loaded.connect(self._maybe_restore_last_device)
        self.detail_page.back_requested.connect(lambda: self._select_page(PAGE_LIST))
        self.detail_page.logout_requested.connect(self._on_logout)
        self.detail_page.open_records.connect(
            lambda: self._open_records_page(self.detail_page.current_device_id))
        self.detail_page.open_battery.connect(self._open_battery_page)
        self.detail_page.open_cmd.connect(
            lambda: self._open_cmd_page(self.detail_page.current_device_id))
        self.cmd_page._inner_page.mode_changed.connect(self._on_cmd_mode_changed)
        self.features_page.feature_selected.connect(self._on_feature_selected)
        self.records_page._inner_page.locate_requested.connect(self._locate_from_record)
        self.settings_page._inner_page.logout_requested.connect(self._on_logout)

    def _locate_from_record(self, rec):
        """云端记录点定位/跟踪卡 → 切到地图中心并定位到该坐标（对标 onRecordTap）"""
        self._select_page(PAGE_MAP)
        lat, lng = rec['lat'], rec['lng']
        device_id = rec.get('upDateDevice', '')
        try:
            self.scene.clear_all_place()
            self.scene.receive_place_to_scene3d((lat, lng), device_id)
            self.scene.receive_gps_coordinates(lat, lng)
            self._notify(f'已定位 {device_id}: {lat},{lng}')
        except Exception as e:
            self._notify(f'地图定位失败: {e}')

    def _build_navbar(self):
        """底部导航栏（规格对齐小程序 custom-tab-bar：白底、#eee 顶线、
        图标 48rpx=24px、文字 20rpx=10px、未选中 #999 / 选中 #07c160）"""
        from PyQt6.QtWidgets import QToolButton
        from PyQt6.QtCore import QSize
        nav = QWidget()
        nav.setFixedHeight(52)
        nav.setStyleSheet(
            f"background: #FFFFFF; border-top: 1px solid {theme.TAB_BORDER};")
        h = QHBoxLayout(nav)
        h.setContentsMargins(0, 2, 0, 2)
        h.setSpacing(0)
        self.nav_buttons = {}
        self._nav_icon_map = {
            'detail': ('tab_home', 'tab_home_active'),
            'list': ('tab_device', 'tab_device_active'),
            'features': ('tab_features', 'tab_features_active'),
            'map': ('tab_map', 'tab_map_active'),
        }
        for key, label in (('detail', '设备详情'), ('list', '设备列表'),
                           ('features', '功能列表'), ('map', '地图中心')):
            icon, active_icon = self._nav_icon_map[key]
            btn = QToolButton()
            btn.setText(label)
            btn.setIcon(theme.tab_icon(icon))
            btn.setIconSize(QSize(24, 24))
            btn.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextUnderIcon)
            btn.setCheckable(True)
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setStyleSheet(f"""
                QToolButton {{
                    background: transparent; color: {theme.TAB_TEXT};
                    border: none; padding: 2px 0px; font-size: 10px;
                }}
                QToolButton:checked {{ color: {theme.TAB_SELECTED}; }}
                QToolButton:hover:!checked {{ color: {theme.TAB_SELECTED}; }}
            """)
            btn.clicked.connect(lambda _, k=key: self._on_nav_clicked(k))
            h.addWidget(btn, stretch=1)
            self.nav_buttons[key] = btn
        return nav

    def _wrap_sub(self, page, title):
        """给功能子页包一层"← 返回功能列表 + 标题"顶栏（对标小程序 navigateTo 层级）"""
        wrapper = QWidget()
        layout = QVBoxLayout(wrapper)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        top = QHBoxLayout()
        top.setContentsMargins(10, 8, 10, 8)
        back_btn = QPushButton("← 功能列表")
        back_btn.setStyleSheet(theme.PRIMARY_BUTTON_QSS)
        # 返回目标可被子页改写（如设备记录模式 → 返回设备详情）；点击时读当前值
        wrapper._back_target = PAGE_FEATURES
        back_btn.clicked.connect(
            lambda: self._select_page(getattr(wrapper, '_back_target', PAGE_FEATURES)))
        title_label = QLabel(title)
        title_label.setFont(theme.font(13, bold=True))
        title_label.setStyleSheet(f"color: {theme.PRIMARY};")
        top.addWidget(back_btn)
        top.addWidget(title_label, stretch=1)
        layout.addLayout(top)
        layout.addWidget(page, stretch=1)
        wrapper._inner_page = page
        wrapper._title_label = title_label
        wrapper._back_btn = back_btn
        return wrapper

    # ==================== 启动登录流程 ====================

    def _startup_auto_login(self):
        """启动时尝试自动登录（对标 app.js initLogin + index._checkLogin）：
        勾选了自动登录且有本地账密 → 静默登录；失败回登录页显示原因"""
        if not self.login_page.try_auto_login(self._on_login_success):
            return  # 留在登录页
        # try_auto_login 成功回调会调 _on_login_success；失败时登录页显示错误

    # ==================== 登录/注销 ====================

    def _on_login_success(self):
        """登录成功（手动或自动）：进主界面并刷新各页数据"""
        session_store.set_login_info(
            self.api.username, self.api.password,
            self.api.wechatid, self.api.level, self.api.mapcenter)
        self._enter_main()

    def _enter_main(self):
        self.root_stack.setCurrentWidget(self.main_view)
        # 权限门控：level<=2 才显示"功能列表"（与小程序一致）
        self.nav_buttons['features'].setVisible(self.api.level <= 2)
        self._refresh_all_pages()
        self._select_page(PAGE_DETAIL)

    def _refresh_all_pages(self):
        self.list_page.refresh()
        self.cmd_page._inner_page.refresh_devices()
        self.battery_page._inner_page.refresh_devices()
        self.roads_page._inner_page.refresh()
        self.features_page.refresh()

    def _on_logout(self):
        """注销登入（对标小程序 index.onLogoutTap）：确认 → 清会话 → 回登录页"""
        ret = QMessageBox.question(self, "注销登入", "确定要退出当前账号吗？",
                                   QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if ret != QMessageBox.StandardButton.Yes:
            return
        session_store.clear_login()   # 清 login_info，保留 last_login 供预填
        self.api.logout()
        self.root_stack.setCurrentWidget(self.login_page)
        self._notify("已注销登入")

    # ==================== 导航 ====================

    def _on_nav_clicked(self, key):
        """单击导航：切 Tab；重复单击当前 Tab → 无感刷新（对标小程序 custom-tab-bar）"""
        idx = {'detail': PAGE_DETAIL, 'list': PAGE_LIST,
               'features': PAGE_FEATURES, 'map': PAGE_MAP}[key]
        nav_key = {PAGE_DETAIL: 'detail', PAGE_LIST: 'list',
                   PAGE_FEATURES: 'features', PAGE_MAP: 'map'}.get(self.content.currentIndex())
        if key == nav_key and key != 'features':
            self._silent_refresh(key)
            return
        self._select_page(idx)

    def _silent_refresh(self, key):
        """无感刷新（对标小程序"设备详情/设备/地图"Tab 单击转圈刷新）"""
        if key == 'detail':
            row = self.detail_page._current_device
            if row:
                self.detail_page.open_detail(row, self.list_page._device_rows,
                                             self.list_page._sync_map)
                self._notify('设备详情已刷新')
        elif key == 'list':
            self.list_page.refresh()
            self._notify('设备列表已刷新')
        elif key == 'map':
            refresh = getattr(self.map_panel, 'refresh', None)
            if callable(refresh):
                refresh()
                self._notify('地图数据已刷新')

    def _select_page(self, idx):
        """切换内容页并同步底部导航选中态与图标"""
        self.content.setCurrentIndex(idx)
        nav_key = {PAGE_DETAIL: 'detail', PAGE_LIST: 'list',
                   PAGE_FEATURES: 'features', PAGE_MAP: 'map'}.get(idx)
        for k, btn in self.nav_buttons.items():
            checked = (k == nav_key)
            btn.setChecked(checked)
            icon, active_icon = self._nav_icon_map[k]
            btn.setIcon(theme.tab_icon(active_icon if checked else icon))
        # 进入功能列表/设置时刷新（level 门控 / 账号信息）
        if idx == PAGE_FEATURES:
            self.features_page.refresh()
        elif idx == PAGE_SETTINGS:
            self.settings_page._inner_page.refresh()

    def _open_detail_from_list(self, row):
        """设备列表双击 → 切到"设备详情"Tab 并打开该设备（对标小程序行为）"""
        self.detail_page.open_detail(row, self.list_page._device_rows, self.list_page._sync_map)
        # 记住该账号当前选中的设备：重启/重新登录后自动恢复显示
        save_home_selected(self.api.username, str(row.get('deviceId', '')))
        self._select_page(PAGE_DETAIL)

    def _maybe_restore_last_device(self):
        """设备列表数据就绪后：若详情页还没选设备且该账号有上次选中记录 → 自动恢复显示"""
        if self.detail_page.current_device_id:
            return
        last_id = load_home_selected(self.api.username)
        if not last_id:
            return
        row = self.list_page._row_map.get(last_id)
        if row and str(row.get('visible', '1')) not in ('0', 'false', 'False'):
            self.detail_page.open_detail(row, self.list_page._device_rows,
                                         self.list_page._sync_map)
            self._notify(f'已恢复上次选中的设备 {last_id}')

    def _open_records_page(self, device_id=None):
        """详情页"数据列表" → 该设备记录（对标 device-detail?mode=records）；
        功能列表入口传 None → 全局云端记录"""
        inner = self.records_page._inner_page
        if device_id:
            self.records_page._title_label.setText('设备记录')
            self.records_page._back_target = PAGE_DETAIL   # 从设备进入 → 返回详情
            self.records_page._back_btn.setText('← 设备详情')
            inner.open_for_device(device_id)
        else:
            self.records_page._title_label.setText('云端记录')
            self.records_page._back_target = PAGE_FEATURES
            self.records_page._back_btn.setText('← 功能列表')
            inner.open_cloud()
        self._select_page(PAGE_RECORDS)

    def _open_battery_page(self):
        """设备详情"电量走势" → 电量走势页（自动选中当前设备并查询，返回目标=详情）"""
        self.battery_page._back_target = PAGE_DETAIL   # 从设备进入 → 返回设备详情
        self.battery_page._back_btn.setText('← 设备详情')
        self._select_page(PAGE_BATTERY)
        self.battery_page._inner_page.refresh_devices(
            preset_id=self.detail_page.current_device_id)

    def _open_cmd_page(self, device_id=None):
        """设备详情"设备设置" → DTU指令页（预选设备，按 ProductKey 分流普通/中继）；
        功能列表入口传 None → 普通模式"""
        inner = self.cmd_page._inner_page
        if device_id:
            self.cmd_page._back_target = PAGE_DETAIL
            self.cmd_page._back_btn.setText('← 设备详情')
            inner.open_for_device(device_id)
        else:
            self.cmd_page._back_target = PAGE_FEATURES
            self.cmd_page._back_btn.setText('← 功能列表')
            inner.refresh_devices()
        self._select_page(PAGE_CMD)

    def _on_cmd_mode_changed(self, is_relay):
        """目标设备是中继 → 标题"中继DTU指令"，普通 → "设备DTU指令"（对标 navTitle 分流）"""
        self.cmd_page._title_label.setText('中继DTU指令' if is_relay else '设备DTU指令')

    def _on_feature_selected(self, key):
        """功能列表入口 → 子功能页（进入时刷新数据）；insert/bluetooth/optimize 在功能页内处理"""
        if key == 'records':
            self._open_records_page()   # 统一走入口：重置标题/返回目标为云端模式
            return
        if key == 'cmd':
            self._open_cmd_page()   # 统一走入口：重置返回目标为功能列表
            return
        idx, page_wrapper = {
            'livestock': (PAGE_LIVESTOCK, self.livestock_page),
            'records': (PAGE_RECORDS, self.records_page),
            'battery': (PAGE_BATTERY, self.battery_page),
            'cmd': (PAGE_CMD, self.cmd_page),
            'roads': (PAGE_ROADS, self.roads_page),
            'settings': (PAGE_SETTINGS, self.settings_page),
        }.get(key, (None, None))
        if idx is None:
            return
        inner = page_wrapper._inner_page
        if key == 'livestock' and hasattr(inner, 'refresh'):
            inner.refresh()
        elif key == 'roads' and hasattr(inner, 'refresh'):
            inner.refresh()
        elif key == 'battery' and hasattr(inner, 'refresh_devices'):
            inner.refresh_devices()
        elif key == 'cmd' and hasattr(inner, 'refresh_devices'):
            inner.refresh_devices()
        self._select_page(idx)
