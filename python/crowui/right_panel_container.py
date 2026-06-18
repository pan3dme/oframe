from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QStackedWidget, QLabel)
from PyQt6.QtGui import QFont, QColor
from PyQt6.QtCore import Qt

from crowui.submenu.tab_alldivice_list import TabAllDiviceList
from crowui.submenu.device_detail_page import DeviceDetailPage
from crowui.submenu.tab_allcorw_list import TobAllcorwList
from crowui.submenu.cowsheep_detail_page import CowSheepDetailPage
from crowui.submenu.tab_refrish_info import TobRefrishInfo


class RightPanelContainer(QWidget):
    """右侧面板容器，管理多个页面的切换"""
    
    def __init__(self, parent=None, ots_client=None, toggle_callback=None):
        super().__init__(parent)
        self.ots_client = ots_client
        self.toggle_callback = toggle_callback  # 保存切换地图位置的回调函数
        
        # 设置背景色
        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), QColor("#f5f5f5"))
        self.setPalette(palette)
        
        # 创建主布局
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(20, 20, 20, 20)
        main_layout.setSpacing(15)
        
        # ==================== 按钮区域 ====================
        button_widget = QWidget()
        button_layout = QHBoxLayout(button_widget)
        button_layout.setContentsMargins(0, 0, 0, 10)
        button_layout.setSpacing(8)
        
        # 创建统一的按钮样式
        button_style = """
            QPushButton {
                background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 %s,
                    stop:1 %s);
                color: white;
                border: none;
                border-radius: 6px;
                font-size: 13px;
                font-weight: bold;
                padding: 8px 12px;
                min-height: 40px;
            }
            QPushButton:hover {
                background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 %s,
                    stop:1 %s);
            }
            QPushButton:pressed {
                background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 %s,
                    stop:1 %s);
            }
        """
        
        # 创建设备列表按钮
        self.btn_device_list = QPushButton("📱 设备列表")
        self.btn_device_list.setStyleSheet(button_style % (
            "#3498db", "#2980b9",  # normal
            "#5dade2", "#3498db",  # hover
            "#2874a6", "#1f618d"   # pressed
        ))
        
        # 牛羊管理按钮
        self.btn_cow_sheep = QPushButton("🐄 牛羊管理")
        self.btn_cow_sheep.setStyleSheet(button_style % (
            "#2ecc71", "#27ae60",
            "#58d68d", "#2ecc71",
            "#1e8449", "#196f3d"
        ))
        
        # 最新数据按钮
        self.btn_latest_data = QPushButton("📊 最新数据")
        self.btn_latest_data.setStyleSheet(button_style % (
            "#e74c3c", "#c0392b",
            "#ec7063", "#e74c3c",
            "#922b21", "#7b241c"
        ))
        
        # 巡航画面按钮
        self.btn_cruise = QPushButton("🎥 巡航画面")
        self.btn_cruise.setStyleSheet(button_style % (
            "#f39c12", "#d68910",
            "#f5b041", "#f39c12",
            "#af601a", "#935116"
        ))
        
        # 切换位置按钮
        self.btn_toggle = QPushButton("🔄 切换地图")
        self.btn_toggle.setStyleSheet(button_style % (
            "#9b59b6", "#8e44ad",
            "#af7ac5", "#9b59b6",
            "#6c3483", "#5b2c6f"
        ))
        
        # 添加按钮到布局（使用stretch使按钮均匀分布）
        button_layout.addWidget(self.btn_device_list, stretch=1)
        button_layout.addWidget(self.btn_cow_sheep, stretch=1)
        button_layout.addWidget(self.btn_latest_data, stretch=1)
        button_layout.addWidget(self.btn_cruise, stretch=1)
        button_layout.addWidget(self.btn_toggle, stretch=1)
        
        main_layout.addWidget(button_widget)
        
        # ==================== 页面堆叠区域 ====================
        # 创建堆叠窗口用于管理多个页面
        self.stacked_widget = QStackedWidget()
        self.stacked_widget.setStyleSheet("""
            QStackedWidget {
                background-color: white;
                border: 1px solid #ddd;
                border-radius: 5px;
            }
        """)
        
        # 创建默认欢迎页面
        self.welcome_page = self._create_welcome_page()
        self.stacked_widget.addWidget(self.welcome_page)
        
        # 创建设备列表页面（初始隐藏，点击按钮时创建）
        self.device_list_page = None
        
        # 创建设备详情页面（动态创建）
        self.device_detail_page = None
        
        # 创建牛羊列表页面（初始隐藏，点击按钮时创建）
        self.cowsheep_list_page = None
        
        # 创建牛羊详情页面（动态创建）
        self.cowsheep_detail_page = None
        
        # 创建最新数据页面（初始隐藏，点击按钮时创建）
        self.latest_data_page = None
        
        main_layout.addWidget(self.stacked_widget, stretch=1)
        
        # 连接按钮信号
        self.btn_device_list.clicked.connect(self.show_device_list_page)
        self.btn_cow_sheep.clicked.connect(self.show_cowsheep_list_page)
        self.btn_latest_data.clicked.connect(self.show_latest_data_page)
        self.btn_cruise.clicked.connect(lambda: print("巡航画面按钮被点击"))
        if self.toggle_callback:
            self.btn_toggle.clicked.connect(self.toggle_callback)
        else:
            self.btn_toggle.clicked.connect(lambda: print("切换地图位置按钮被点击（未设置回调）"))
        
        print("✅ 右侧面板容器初始化完成")
    
    def _create_welcome_page(self):
        """创建欢迎页面"""
        welcome_widget = QWidget()
        welcome_layout = QVBoxLayout(welcome_widget)
        welcome_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        welcome_label = QLabel("欢迎使用\n\n请点击左侧按钮选择功能")
        welcome_label.setFont(QFont("Microsoft YaHei", 16))
        welcome_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        welcome_label.setStyleSheet("""
            QLabel {
                color: #7f8c8d;
                padding: 20px;
            }
        """)
        
        welcome_layout.addWidget(welcome_label)
        return welcome_widget
    
    def show_device_list_page(self):
        """显示设备列表页面"""
        print("📱 切换到设备列表页面")
        
        # 如果设备列表页面还未创建，则创建它
        if self.device_list_page is None:
            print("  创建设备列表页面...")
            self.device_list_page = TabAllDiviceList(client=self.ots_client)
            # 连接卡片点击信号到显示详情页
            self.device_list_page.card_clicked_signal.connect(self.show_device_detail_page)
            self.stacked_widget.addWidget(self.device_list_page)
        
        # 切换到设备列表页面
        self.stacked_widget.setCurrentWidget(self.device_list_page)
        print("  ✅ 已切换到设备列表页面")
    
    def show_device_detail_page(self, device_data):
        """显示设备详情页面"""
        print(f"📄 显示设备详情: {device_data.get('deviceId', 'Unknown')}")
        
        # 每次创建新的详情页面，移除旧的
        if self.device_detail_page is not None:
            index = self.stacked_widget.indexOf(self.device_detail_page)
            if index >= 0:
                self.stacked_widget.removeWidget(self.device_detail_page)
                self.device_detail_page.deleteLater()
        
        # 创建新的设备详情页面
        self.device_detail_page = DeviceDetailPage(device_data)
        # 连接返回信号
        self.device_detail_page.back_signal.connect(self.back_to_device_list)
        self.stacked_widget.addWidget(self.device_detail_page)
        
        # 切换到设备详情页面
        self.stacked_widget.setCurrentWidget(self.device_detail_page)
        print("  ✅ 已切换到设备详情页面")
    
    def back_to_device_list(self):
        """返回设备列表页面"""
        print("🔙 从设备详情返回设备列表")
        
        if self.device_list_page is not None:
            self.stacked_widget.setCurrentWidget(self.device_list_page)
            print("  ✅ 已返回设备列表页面")
        else:
            print("  ❌ 设备列表页面不存在")
    
    def show_cowsheep_list_page(self):
        """显示牛羊列表页面"""
        print("🐄 切换到牛羊列表页面")
        
        # 如果牛羊列表页面还未创建，则创建它
        if self.cowsheep_list_page is None:
            print("  创建牛羊列表页面...")
            self.cowsheep_list_page = TobAllcorwList(client=self.ots_client)
            # 连接卡片点击信号到显示详情页
            self.cowsheep_list_page.card_clicked_signal.connect(self.show_cowsheep_detail_page)
            self.stacked_widget.addWidget(self.cowsheep_list_page)
        
        # 切换到牛羊列表页面
        self.stacked_widget.setCurrentWidget(self.cowsheep_list_page)
        print("  ✅ 已切换到牛羊列表页面")
    
    def show_cowsheep_detail_page(self, cowsheep_data):
        """显示牛羊详情页面"""
        print(f"📄 显示牛羊详情: {cowsheep_data.get('cowsheep_id', 'Unknown')}")
        
        # 每次创建新的详情页面，移除旧的
        if self.cowsheep_detail_page is not None:
            index = self.stacked_widget.indexOf(self.cowsheep_detail_page)
            if index >= 0:
                self.stacked_widget.removeWidget(self.cowsheep_detail_page)
                self.cowsheep_detail_page.deleteLater()
        
        # 创建新的牛羊详情页面
        self.cowsheep_detail_page = CowSheepDetailPage(cowsheep_data)
        # 连接返回信号
        self.cowsheep_detail_page.back_signal.connect(self.back_to_cowsheep_list)
        self.stacked_widget.addWidget(self.cowsheep_detail_page)
        
        # 切换到牛羊详情页面
        self.stacked_widget.setCurrentWidget(self.cowsheep_detail_page)
        print("  ✅ 已切换到牛羊详情页面")
    
    def back_to_cowsheep_list(self):
        """返回牛羊列表页面"""
        print("🔙 从牛羊详情返回牛羊列表")
        
        if self.cowsheep_list_page is not None:
            self.stacked_widget.setCurrentWidget(self.cowsheep_list_page)
            print("  ✅ 已返回牛羊列表页面")
        else:
            print("  ❌ 牛羊列表页面不存在")
    
    def show_latest_data_page(self):
        """显示最新数据页面"""
        print("📊 切换到最新数据页面")
        
        # 如果最新数据页面还未创建，则创建它
        if self.latest_data_page is None:
            print("  创建最新数据页面...")
            self.latest_data_page = TobRefrishInfo(client=self.ots_client)
            self.stacked_widget.addWidget(self.latest_data_page)
        
        # 切换到最新数据页面
        self.stacked_widget.setCurrentWidget(self.latest_data_page)
        print("  ✅ 已切换到最新数据页面")
