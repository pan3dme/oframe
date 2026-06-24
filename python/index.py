import sys
import os
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout
)

from tablestore import OTSClient
from display3d.google_scene3d import GoogleScene3D
from crowui.right_panel_container import RightPanelContainer
from config import settings


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.client = None
        settings.current_mode = "small_2d"  # 跟踪当前模式："small_2d" 或 "large_2d"
        self.setWindowTitle("高德地图应用")
        self.setGeometry(0, 0, 1900, 1000)
        # self.setGeometry(200, 50, 1700, 750)

        # 初始化OTS客户端
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
        
        # ==================== 左侧区域 - 3D场景 ====================
        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)
        left_layout.setContentsMargins(0, 0, 0, 0)
        left_layout.setSpacing(0)
        
        # 创建GoogleScene3D实例（左侧全屏）
        self.googleMapScene3D = GoogleScene3D()
        self.googleMapScene3D.setAutoFillBackground(True)
        palette_3d = self.googleMapScene3D.palette()
        palette_3d.setColor(self.googleMapScene3D.backgroundRole(), QColor("#2C3E50"))
        self.googleMapScene3D.setPalette(palette_3d)
        self.googleMapScene3D.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        
        left_layout.addWidget(self.googleMapScene3D)
        
        # 将左侧部件添加到主布局，设置拉伸因子为5
        main_layout.addWidget(left_widget, stretch=5)
        
        # ==================== 右侧区域 - 按钮面板（独立组件）====================
        right_widget = RightPanelContainer(ots_client=self.client)
        
        # 将右侧部件添加到主布局，设置拉伸因子为3
        main_layout.addWidget(right_widget, stretch=3)
        
        print("✅ 窗口初始化完成，3D场景和2D地图已添加")

    def initTabelClient(self):
        """初始化阿里云表格存储客户端"""
        try:
            access_key_id = os.getenv("ALIYUN_ACCESS_KEY_ID")
            access_key_secret = os.getenv("ALIYUN_ACCESS_KEY_SECRET")
            
            # TODO: 根据实例信息修改以下配置
            instance_name = "tabel001"  # 填写实例名称
            endpoint = "https://tabel001.cn-shanghai.ots.aliyuncs.com"  # 填写实例访问地址
            
            # 创建客户端实例
            self.client = OTSClient(endpoint, access_key_id, access_key_secret, instance_name)
            
            # 列举数据表
            resp = self.client.list_table()
            
            print(f"在实例 '{instance_name}' 中共找到 {len(resp)} 个数据表:")
            for table_name in resp:
                print(f"  - {table_name}")
            
            # 列举时序表
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
