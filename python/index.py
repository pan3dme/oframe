import sys
import os
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QSplitter,
    QTabWidget
)

from tablestore import OTSClient
from display3d.google_scene3d import GoogleScene3D
from crowui.right_panel_container import RightPanelContainer
from crowui.leftpanel import LeftPanel
from crowui.wx_api import WxApi
from config import settings


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.client = None
        self.wx_api = WxApi()
        settings.current_mode = "small_2d"  # 跟踪当前模式："small_2d" 或 "large_2d"
        self.setWindowTitle("牛羊GPS管理")
        self.setGeometry(200, 50, 1500, 750)

        # 初始化OTS客户端（右侧面板用；左侧走小程序HTTP API）
        if not self.initTabelClient():
            print("警告: OTS客户端初始化失败，部分功能可能不可用")

        # 创建中心部件
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        # 创建水平布局（左右分布）
        main_layout = QHBoxLayout()
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)
        central_widget.setLayout(main_layout)

        # ==================== 左侧区域 - 2D/3D场景 + 功能侧栏 ====================
        left_widget = QWidget()
        left_layout = QHBoxLayout(left_widget)
        left_layout.setContentsMargins(0, 0, 0, 0)
        left_layout.setSpacing(0)

        # 左右可拖动分割器：场景 | 功能侧栏
        left_splitter = QSplitter(Qt.Orientation.Horizontal)

        # --- 2D/3D 场景 + 数据分析 双页签 ---
        from crowui.leftpages.analysis_page import AnalysisPage
        self.scene_tabs = QTabWidget()
        self.scene_tabs.setDocumentMode(True)   # 无边框页签条，贴合场景区

        self.googleMapScene3D = GoogleScene3D()
        self.googleMapScene3D.setAutoFillBackground(True)
        palette_3d = self.googleMapScene3D.palette()
        palette_3d.setColor(self.googleMapScene3D.backgroundRole(), QColor("#2C3E50"))
        self.googleMapScene3D.setPalette(palette_3d)
        self.googleMapScene3D.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.scene_tabs.addTab(self.googleMapScene3D, '🗺 3D 场景')
        self.analysis_page = AnalysisPage(self.wx_api)
        self.scene_tabs.addTab(self.analysis_page, '📊 数据分析')
        left_splitter.addWidget(self.scene_tabs)

        # --- 功能侧栏（微信小程序功能移植） ---
        self.left_panel = LeftPanel(self.wx_api, self.googleMapScene3D)
        left_splitter.addWidget(self.left_panel)

        # 场景占大部分，侧栏固定宽度可拖动调整
        left_splitter.setStretchFactor(0, 1)
        left_splitter.setStretchFactor(1, 0)
        left_splitter.setCollapsible(0, False)

        left_layout.addWidget(left_splitter)
        main_layout.addWidget(left_widget, stretch=1)

        # 右侧面板已下线：其全部功能（设备/牛羊/记录）已由左侧功能侧栏承接
        # 需要恢复时取消下面注释即可
        # right_widget = RightPanelContainer(ots_client=self.client)
        # main_layout.addWidget(right_widget, stretch=3)

        print("✅ 窗口初始化完成：左侧 2D/3D场景 + 小程序功能侧栏，右侧数据面板")

    def initTabelClient(self):
        """初始化阿里云表格存储客户端（右侧面板数据源）"""
        try:
            access_key_id = os.getenv("ALIYUN_ACCESS_KEY_ID")
            access_key_secret = os.getenv("ALIYUN_ACCESS_KEY_SECRET")

            instance_name = "tabel001"  # 填写实例名称
            endpoint = "https://tabel001.cn-shanghai.ots.aliyuncs.com"  # 填写实例访问地址

            self.client = OTSClient(endpoint, access_key_id, access_key_secret, instance_name)

            resp = self.client.list_table()
            print(f"在实例 '{instance_name}' 中共找到 {len(resp)} 个数据表:")
            for table_name in resp:
                print(f"  - {table_name}")

            resp = self.client.list_timeseries_table()
            print(f"\n在实例 '{instance_name}' 中共找到 {len(resp)} 个时序表:")
            for tableMeta in resp:
                print(f"  - {tableMeta.timeseries_table_name}")

            return True

        except Exception as e:
            print(f"操作失败: {str(e)}")
            return False

    def keyPressEvent(self, event):
        """键盘事件处理"""
        if event.key() == Qt.Key.Key_Space:
            # 空格键：切换上下视图焦点
            self.googleMapScene3D.keyPressEventDownSpace()
        else:
            super().keyPressEvent(event)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())
