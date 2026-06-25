import sys
import os
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QPushButton
)

from tablestore import OTSClient, INF_MAX, INF_MIN, Direction
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
        
        # 创建3D场景容器（用于按钮覆盖层）
        scene_container = QWidget()
        scene_layout = QVBoxLayout(scene_container)
        scene_layout.setContentsMargins(0, 0, 0, 0)
        scene_layout.setSpacing(0)
        
        # 创建GoogleScene3D实例（左侧全屏）
        self.googleMapScene3D = GoogleScene3D()
        self.googleMapScene3D.setAutoFillBackground(True)
        palette_3d = self.googleMapScene3D.palette()
        palette_3d.setColor(self.googleMapScene3D.backgroundRole(), QColor("#2C3E50"))
        self.googleMapScene3D.setPalette(palette_3d)
        self.googleMapScene3D.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        
        scene_layout.addWidget(self.googleMapScene3D)
        
        # 覆盖按钮 - 显示道路和地名（位于3D场景左上角）
        self.show_road_btn = QPushButton("显示道路", scene_container)
        self.show_road_btn.setFixedSize(80, 28)
        self.show_road_btn.move(10, 10)
        self.show_road_btn.clicked.connect(self._show_roads_on_3d)
        
        self.show_place_btn = QPushButton("显示地名", scene_container)
        self.show_place_btn.setFixedSize(80, 28)
        self.show_place_btn.move(95, 10)
        self.show_place_btn.clicked.connect(self._show_places_on_3d)
        
        btn_style = """
            QPushButton {
                background-color: rgba(0, 120, 215, 200);
                color: white;
                border: none;
                border-radius: 4px;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: rgba(0, 120, 215, 255);
            }
        """
        self.show_road_btn.setStyleSheet(btn_style)
        self.show_place_btn.setStyleSheet(btn_style)
        
        left_layout.addWidget(scene_container)
        
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
    
    def _show_roads_on_3d(self):
        """从数据库读取道路数据并显示在3D地图上"""
        columns_to_get = ['route_id', 'roadinfo', 'roadname']
        inclusive_start_primary_key = [('route_id', INF_MAX)]
        exclusive_end_primary_key = [('route_id', INF_MIN)]
        
        try:
            consumed, next_start_primary_key, route_list, next_token = self.client.get_range(
                table_name=settings.ROUTETABLE_NAME,
                direction=Direction.BACKWARD,
                inclusive_start_primary_key=inclusive_start_primary_key,
                exclusive_end_primary_key=exclusive_end_primary_key,
                columns_to_get=columns_to_get,
                limit=50
            )
            
            print(f"成功读取 {len(route_list)} 条道路记录")
            displayed_count = 0
            for row in route_list:
                attr_dict = {attr[0]: attr[1] for attr in row.attribute_columns}
                roadinfo = attr_dict.get('roadinfo', '')
                roadname = attr_dict.get('roadname', '')
                
                if not roadinfo:
                    continue
                
                # 解析坐标: "lat1,lon1,lat2,lon2,..."
                arr = [float(x.strip()) for x in roadinfo.split(',')]
                gps_coords = []
                for i in range(int(len(arr) / 2)):
                    gps_coords.append((arr[i * 2 + 0], arr[i * 2 + 1]))
                
                # 显示到3D场景
                self.googleMapScene3D.receive_load_to_scene(gps_coords)
                displayed_count += 1
                print(f"  显示道路: {roadname}, 坐标点: {len(gps_coords)}个")
            
            print(f"共显示 {displayed_count} 条道路")
            if displayed_count > 0:
                self.googleMapScene3D.gl_widget.update()
            
        except Exception as e:
            print(f"查询道路数据失败: {e}")
            import traceback
            traceback.print_exc()

    def _show_places_on_3d(self):
        """从数据库读取地名数据并显示在3D地图上"""
        columns_to_get = ['placeid', 'gps', 'name']
        inclusive_start_primary_key = [('placeid', INF_MAX)]
        exclusive_end_primary_key = [('placeid', INF_MIN)]
        
        try:
            consumed, next_start_primary_key, place_list, next_token = self.client.get_range(
                table_name=settings.PLACETABLE_NAME,
                direction=Direction.BACKWARD,
                inclusive_start_primary_key=inclusive_start_primary_key,
                exclusive_end_primary_key=exclusive_end_primary_key,
                columns_to_get=columns_to_get,
                limit=50
            )
            
            print(f"成功读取 {len(place_list)} 条地名记录")
            displayed_count = 0
            for row in place_list:
                attr_dict = {attr[0]: attr[1] for attr in row.attribute_columns}
                gps_str = attr_dict.get('gps', '')
                name = attr_dict.get('name', '')
                
                if not gps_str or ',' not in gps_str:
                    continue
                
                # 解析GPS坐标
                lat_str, lon_str = gps_str.split(',')
                gps_coord = (float(lat_str.strip()), float(lon_str.strip()))
                
                # 显示到3D场景
                self.googleMapScene3D.receive_place_to_scene3d(gps_coord, name)
                displayed_count += 1
                print(f"  显示地名: {name}, gps={gps_coord}")
            
            print(f"共显示 {displayed_count} 个地名")
            
        except Exception as e:
            print(f"查询地名数据失败: {e}")
            import traceback
            traceback.print_exc()

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
