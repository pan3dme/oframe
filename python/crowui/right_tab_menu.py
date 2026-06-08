import os

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QPushButton,
                              QRadioButton, QCheckBox, QDialogButtonBox, QLineEdit,
                              QTabWidget, QLabel, QGridLayout, QScrollArea)
from PyQt6.QtGui import QFont, QPalette, QColor
from PyQt6.QtCore import Qt

from PyQt6.QtWebEngineWidgets import QWebEngineView
from tablestore import OTSClient, client

from crowui.button_panel import ButtonPanel
from crowui.tabmenu.tab_allcorw_list import TobAllcorwList
from crowui.tabmenu.tab_refrish_info import TobRefrishInfo


class RightTabMenu(QWidget):
    def __init__(self, parent=None, ots_client=None):
        # 如果传入的是 OTSClient，则不将其作为 parent
        if parent is not None and not isinstance(parent, QWidget):
            ots_client = parent
            parent = None
        super().__init__(parent)
        self.ots_client = ots_client

        # 设置背景颜色
        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(QPalette.ColorRole.Window, QColor(245, 245, 250))
        self.setPalette(palette)
        # 主布局
        layout = QVBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)
        layout.setSpacing(5)

        # 创建选项卡控件
        self.tab_widget = QTabWidget()
        self.tab_widget.setStyleSheet("""
                        QTabWidget::pane {
                            border: 1px solid #cccccc;
                            background: white;
                            border-radius: 5px;
                        }
                        QTabBar::tab {
                            background: #e0e0e0;
                            padding: 10px 20px;
                            margin-right: 3px;
                            border-top-left-radius: 5px;
                            border-top-right-radius: 5px;
                            font-size: 12px;
                            font-weight: bold;
                        }
                        QTabBar::tab:selected {
                            background: white;
                            border-bottom: 3px solid #0078d7;
                            color: #0078d7;
                        }
                        QTabBar::tab:hover:!selected {
                            background: #f0f0f0;
                        }
                    """)
        layout.addWidget(self.tab_widget)
        # 初始化TableStore客户端

        self.create_latest_data_tab()  # 最新数据
        self.create_cow_status_tab()  # 牛最新动态
        self.create_cruise_view_tab()  # 巡航画面




    def create_latest_data_tab(self):
        """创建最新数据选项卡"""
        tab_widget = QWidget()
        tab_layout = QVBoxLayout(tab_widget)
        tab_layout.setContentsMargins(10, 10, 10, 10)

        # 添加标题
        title = QLabel("最新数据")
        title.setFont(QFont("Microsoft YaHei", 14, QFont.Weight.Bold))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        tab_layout.addWidget(title)

        panel = TobRefrishInfo(client=self.ots_client)
        tab_layout.addWidget(panel)

        self.tab_widget.addTab(tab_widget, "最新数据")

    def create_cow_status_tab(self):
        """创建牛最新动态选项卡"""
        tab_widget = QWidget()
        tab_layout = QVBoxLayout(tab_widget)
        tab_layout.setContentsMargins(10, 10, 10, 10)

        # 添加标题
        title = QLabel("所有设备列表")
        title.setFont(QFont("Microsoft YaHei", 14, QFont.Weight.Bold))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        tab_layout.addWidget(title)

        panel = TobAllcorwList(client=self.ots_client)
        tab_layout.addWidget(panel)

        self.tab_widget.addTab(tab_widget, "设备动态")

    def create_cruise_view_tab(self):
        """创建巡航画面选项卡"""
        tab_widget = QWidget()
        tab_layout = QVBoxLayout(tab_widget)
        tab_layout.setContentsMargins(10, 10, 10, 10)

        # 添加标题
        title = QLabel("巡航画面")
        title.setFont(QFont("Microsoft YaHei", 14, QFont.Weight.Bold))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        tab_layout.addWidget(title)

        panel= ButtonPanel()
        tab_layout.addWidget(panel)

        self.tab_widget.addTab(tab_widget, "巡航画面")