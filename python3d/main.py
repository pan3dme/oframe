import sys
import os


from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout,QHBoxLayout
from tablestore import OTSClient

from display3d.google_scene3d import GoogleScene3D
from crowui.google_map2d_widget import GoogleMap2DWidget
from crowui.right_tab_menu import RightTabMenu


from config import settings


# ---------------- 主窗口 ----------------
class MapWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.client = None
        self.setWindowTitle("天地图交互")
        self.setGeometry(100, 100, 1300, 800)

        if self.initTabelClient():

            # 创建中心部件
            central_widget = QWidget()
            self.setCentralWidget(central_widget)

            # 创建水平布局（左右分布）
            layout = QHBoxLayout()
            central_widget.setLayout(layout)

            # 创建左侧部件和垂直布局
            left_widget = QWidget()
            left_layout = QVBoxLayout(left_widget)
            left_layout.setContentsMargins(0, 0, 0, 0)
            left_layout.setSpacing(0)
            layout.addWidget(left_widget, stretch=1)

            # 创建上部分部件和水平布局
            top_widget = QWidget()
            top_layout = QHBoxLayout(top_widget)
            top_layout.setContentsMargins(0, 0, 0, 0)
            top_layout.setSpacing(0)
            left_layout.addWidget(top_widget, stretch=1)

            # 创建TriangleWidget
            self.googleMapScene3D = GoogleScene3D()
            self.googleMapScene3D.setAutoFillBackground(True)
            palette_tri = self.googleMapScene3D.palette()
            palette_tri.setColor(self.googleMapScene3D.backgroundRole(), QColor("#2C3E50"))
            self.googleMapScene3D.setPalette(palette_tri)
            top_layout.addWidget(self.googleMapScene3D)



            # 创建下部分部件
            bottom_widget = QWidget()
            bottom_layout = QVBoxLayout(bottom_widget)
            bottom_layout.setContentsMargins(0, 0, 0, 0)
            bottom_layout.setSpacing(0)
            left_layout.addWidget(bottom_widget, stretch=1)

            self.basepic = GoogleMap2DWidget(self.client)
            self.basepic.receive_map_move_gps= self.googleMapScene3D.receive_gps_coordinates
            self.googleMapScene3D.change_map_gps=self.basepic.change_map_gps

            self.basepic.receive_load= self.googleMapScene3D.receive_load_to_scene
            self.basepic.clear_load= self.googleMapScene3D.clear_all_load_line

            self.basepic.receive_device= self.googleMapScene3D.receive_device_to_scene3d
            self.basepic.clear_device= self.googleMapScene3D.clear_all_device

            self.basepic.receive_place= self.googleMapScene3D.receive_place_to_scene3d
            self.basepic.clear_place= self.googleMapScene3D.clear_all_place




            # 设置焦点策略，允许通过点击和Tab键获取焦点
            self.googleMapScene3D.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
            self.basepic.setFocusPolicy(Qt.FocusPolicy.StrongFocus)

            bottom_layout.addWidget(self.basepic)


            # 随机生成100个GPS标记点，坐标在(39.5, 116.5)基础上变化不超过0.5的范围


            # self.basepic.add_gps_marker((settings.centenGps[0], settings.centenGps[1]), "中心点")



            # 创建右侧按钮面板
            self.button_panel = RightTabMenu(self.client)
            self.button_panel.setAutoFillBackground(True)
            palette_btn = self.button_panel.palette()
            palette_btn.setColor(self.button_panel.backgroundRole(), QColor("#ffffff"))
            self.button_panel.setPalette(palette_btn)
            layout.addWidget(self.button_panel, stretch=1)
            # self.button_panel.new_button.clicked.connect(self.toggle_triangle_widget)

    def initTabelClient(self):
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
                print(f"{table_name}")

            # 列举时序表
            resp = self.client.list_timeseries_table()

            print(f"\n在实例 '{instance_name}' 中共找到 {len(resp)} 个时序表:")
            for tableMeta in resp:
                print(f"{tableMeta.timeseries_table_name}")

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



    def toggle_triangle_widget(self):
        """切换TriangleWidget和地图的显示"""


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = MapWindow()
    win.show()
    sys.exit(app.exec())