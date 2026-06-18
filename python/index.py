import sys
import os
from PyQt6.QtCore import Qt, QUrl, QTimer, QSize
from PyQt6.QtGui import QColor, QIcon, QPainter, QPixmap, QPainterPath, QPen, QFont
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
    QGraphicsDropShadowEffect
)
from PyQt6.QtWebEngineWidgets import QWebEngineView
from PyQt6.QtWebEngineCore import QWebEngineSettings
from tablestore import OTSClient, INF_MAX, INF_MIN, Direction

from display3d.google_scene3d import GoogleScene3D
from crowui.google_map2d_widget import GoogleMap2DWidget
from crowui.right_panel_container import RightPanelContainer
from config import settings


# ==================== 地图控制按钮图标创建函数 ====================
def create_route_icon():
    """创建道路图标"""
    pixmap = QPixmap(30, 30)
    pixmap.fill(QColor(0, 0, 0, 0))  # transparent
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    
    pen = QPen(QColor(0, 120, 215))
    pen.setWidth(3)
    pen.setCapStyle(Qt.PenCapStyle.RoundCap)
    painter.setPen(pen)
    
    path = QPainterPath()
    path.moveTo(5, 15)
    path.cubicTo(10, 5, 20, 25, 25, 15)
    painter.drawPath(path)
    
    path2 = QPainterPath()
    path2.moveTo(5, 20)
    path2.cubicTo(10, 10, 20, 30, 25, 20)
    painter.drawPath(path2)
    
    painter.end()
    return QIcon(pixmap)


def create_place_icon():
    """创建地名图标"""
    pixmap = QPixmap(30, 30)
    pixmap.fill(QColor(0, 0, 0, 0))  # transparent
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)

    # 绘制一个类似图钉的形状
    pen = QPen(QColor(0, 120, 215))
    pen.setWidth(2)
    painter.setPen(pen)
    painter.setBrush(QColor(0, 120, 215))

    path = QPainterPath()
    path.moveTo(15, 5)
    path.quadTo(25, 15, 15, 22)
    path.quadTo(5, 15, 15, 5)
    painter.drawPath(path)

    # 绘制圆点
    painter.setBrush(QColor(255, 255, 255))
    painter.drawEllipse(13, 13, 4, 4)

    # 绘制文字标签
    painter.setPen(QColor(0, 120, 215))
    font = QFont("Arial", 8)
    painter.setFont(font)
    painter.drawText(8, 26, "地名")

    painter.end()
    return QIcon(pixmap)


def create_device_icon():
    """创建设备位置图标"""
    pixmap = QPixmap(30, 30)
    pixmap.fill(QColor(0, 0, 0, 0))  # transparent
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)

    # 绘制设备图标（类似定位图标）
    pen = QPen(QColor(0, 120, 215))
    pen.setWidth(2)
    painter.setPen(pen)
    painter.setBrush(QColor(0, 120, 215))

    # 绘制外圈
    painter.drawEllipse(10, 10, 10, 10)

    # 绘制内圈
    painter.setBrush(QColor(255, 255, 255))
    painter.drawEllipse(13, 13, 4, 4)

    # 绘制文字标签
    painter.setPen(QColor(0, 120, 215))
    font = QFont("Arial", 8)
    painter.setFont(font)
    painter.drawText(8, 26, "设备")

    painter.end()
    return QIcon(pixmap)


def create_center_icon():
    """创建中心点图标"""
    pixmap = QPixmap(30, 30)
    pixmap.fill(QColor(0, 0, 0, 0))  # transparent
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    
    pen = QPen(QColor(0, 120, 215))
    pen.setWidth(2)
    painter.setPen(pen)
    
    painter.drawLine(5, 15, 25, 15)
    painter.drawLine(15, 5, 15, 25)
    
    painter.setBrush(QColor(0, 120, 215))
    painter.drawEllipse(13, 13, 4, 4)
    
    painter.end()
    return QIcon(pixmap)


def create_changescene_icon():
    """创建中心点图标"""
    pixmap = QPixmap(30, 30)
    pixmap.fill(QColor(0, 0, 0, 0))  # transparent
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)

    pen = QPen(QColor(0, 120, 215))
    pen.setWidth(2)
    painter.setPen(pen)

    painter.drawLine(5, 15, 25, 15)
    painter.drawLine(15, 5, 15, 25)

    painter.setBrush(QColor(0, 120, 215))
    painter.drawEllipse(13, 13, 4, 4)

    painter.end()
    return QIcon(pixmap)


class Map2DContainer(QWidget):
    """2D地图容器，阻止鼠标事件传播到父部件"""
    def __init__(self, parent=None):
        super().__init__(parent)
        
    def mousePressEvent(self, event):
        """捕获鼠标按下事件，不传播到父部件"""
        event.accept()
        super().mousePressEvent(event)
    
    def mouseMoveEvent(self, event):
        """捕获鼠标移动事件，不传播到父部件"""
        event.accept()
        super().mouseMoveEvent(event)
    
    def mouseReleaseEvent(self, event):
        """捕获鼠标释放事件，不传播到父部件"""
        event.accept()
        super().mouseReleaseEvent(event)
    
    def wheelEvent(self, event):
        """捕获滚轮事件，不传播到父部件"""
        event.accept()
        super().wheelEvent(event)
    
    def mouseDoubleClickEvent(self, event):
        """双击事件，触发位置互换"""
        if event.button() == Qt.MouseButton.LeftButton:
            # 获取主窗口并调用互换方法
            parent_window = self.window()
            if hasattr(parent_window, 'toggle_map_positions'):
                parent_window.toggle_map_positions()
        event.accept()


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.client = None
        settings.current_mode = "small_2d"  # 跟踪当前模式："small_2d" 或 "large_2d"
        self.setWindowTitle("高德地图应用")
        self.setGeometry(0, 0, 1900, 1000)
        # self.setGeometry(200, 50, 1700, 750)

        # 初始化OTS客端户
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
        
        # 保存3D场景的引用，用于位置互换
        self.scene3d_widget = self.googleMapScene3D
        
        # ==================== 右上角 - 2D地图小窗口 (250x250) ====================
        # 创建2D地图容器（绝对定位，作为left_widget的子部件）
        map2d_container = Map2DContainer(left_widget)
        map2d_container.setFixedSize(250, 250)
        map2d_container.setObjectName("map2dContainer")
        map2d_container.setStyleSheet("""
            QWidget#map2dContainer {
                background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 rgba(26, 26, 46, 0.95),
                    stop:1 rgba(44, 62, 80, 0.95));
                border: 3px solid;
                border-image: linear-gradient(45deg, #3498db, #9b59b6, #e74c3c, #f39c12);
                border-radius: 10px;
            }
        """)
        
        # 添加阴影效果
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(20)
        shadow.setXOffset(3)
        shadow.setYOffset(3)
        shadow.setColor(QColor(0, 0, 0, 150))
        map2d_container.setGraphicsEffect(shadow)
        
        # 设置属性以阻止事件传播到父部件
        map2d_container.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, False)
        map2d_container.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        
        map2d_layout = QVBoxLayout(map2d_container)
        map2d_layout.setContentsMargins(2, 2, 2, 2)
        map2d_layout.setSpacing(0)
        
        # 创建内部容器用于圆角
        inner_container = QWidget()
        inner_container.setObjectName("innerContainer")
        inner_container.setStyleSheet("""
            QWidget#innerContainer {
                background-color: #1a1a2e;
                border-radius: 7px;
            }
        """)
        inner_layout = QVBoxLayout(inner_container)
        inner_layout.setContentsMargins(0, 0, 0, 0)
        inner_layout.setSpacing(0)
        
        # 创建GoogleMap2DWidget实例（传入client）
        self.googleMap2D = GoogleMap2DWidget(None, self.client)
        
        # 绑定3D和2D地图的交互事件
        self.googleMap2D.receive_map_move_gps = self.googleMapScene3D.receive_gps_coordinates
        self.googleMapScene3D.change_map_gps = self.googleMap2D.change_map_gps
        
        self.googleMap2D.receive_load = self.googleMapScene3D.receive_load_to_scene
        self.googleMap2D.clear_load = self.googleMapScene3D.clear_all_load_line
        
        self.googleMap2D.receive_device = self.googleMapScene3D.receive_device_to_scene3d
        self.googleMap2D.clear_device = self.googleMapScene3D.clear_all_device
        
        self.googleMap2D.receive_place = self.googleMapScene3D.receive_place_to_scene3d
        self.googleMap2D.clear_place = self.googleMapScene3D.clear_all_place
        
        inner_layout.addWidget(self.googleMap2D)
        map2d_layout.addWidget(inner_container)
        
        # 保存引用，稍后设置位置
        self.map2d_container = map2d_container
        self.inner_container = inner_container  # 保存内部容器引用
        
        # 创建地图控制按钮容器（左侧区域右下角）
        self._create_map_control_buttons(left_widget)
        
        # 将左侧部件添加到主布局，设置拉伸因子为5
        main_layout.addWidget(left_widget, stretch=5)
        
        # ==================== 右侧区域 - 按钮面板（独立组件）====================
        right_widget = RightPanelContainer(ots_client=self.client, toggle_callback=self.toggle_map_positions)
        
        # 将右侧部件添加到主布局，设置拉伸因子为3
        main_layout.addWidget(right_widget, stretch=3)
        
        print("✅ 窗口初始化完成，3D场景和2D地图已添加")
    
    # ==================== 地图控制按钮事件处理方法 ====================
    def _add_route_to_map(self):
        """切换显示/隐藏道路"""
        # 检查路径显示状态，如果已有路径则清除
        if self.googleMap2D._gps_routes:
            self.googleMap2D._gps_routes.clear()
            self.googleMap2D._update_display()
            self.googleMap2D.clear_load()
            print("已清除所有路径")
            return

        # 1. 定义需要查询的数据列
        columns_to_get = ['route_id', 'roadinfo', 'roadname']

        # 2. 定义主键范围：覆盖全表
        inclusive_start_primary_key = [('route_id', INF_MAX)]
        exclusive_end_primary_key = [('route_id', INF_MIN)]

        try:
            # 3. 执行范围查询，direction=BACKWARD 为倒序读取
            consumed, next_start_primary_key, route_list, next_token = self.client.get_range(
                table_name=settings.ROUTETABLE_NAME,
                direction=Direction.BACKWARD,
                inclusive_start_primary_key=inclusive_start_primary_key,
                exclusive_end_primary_key=exclusive_end_primary_key,
                columns_to_get=columns_to_get,
                limit=20
            )

            # 4. 处理查询结果并显示在表格中
            print(f"成功读取 {len(route_list)} 条最新记录。")
            for row_idx, row in enumerate(route_list):
                # 解析属性列
                attr_dict = {attr[0]: attr[1] for attr in row.attribute_columns}
                roadinfo = attr_dict.get('roadinfo', '')

                # 将 roadinfo 字符串解析为 arr 数组
                arr = [float(x.strip()) for x in roadinfo.split(',')]

                # 生成路线坐标
                route_cords = []
                for i in range(int(len(arr) / 2)):
                    route_cords.append((arr[i * 2 + 0], arr[i * 2 + 1]))

                # 添加路线到地图
                self.googleMap2D.add_gps_route(route_cords)
                self.googleMap2D.receive_load(route_cords)

        except Exception as e:
            print(f"查询失败: {e}")
    
    def _add_place_to_map(self):
        """切换显示/隐藏地名"""
        # 检查地名显示状态，如果已有地名则清除
        if self.googleMap2D._gps_markers:
            self.googleMap2D._gps_markers.clear()
            self.googleMap2D.clear_place()
            self.googleMap2D._update_display()
            print("已隐藏所有地名")
            return

        # 查询地名数据
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
                limit=20
            )

            print(f"成功读取 {len(place_list)} 条最新记录。")
            for row_idx, row in enumerate(place_list):
                attr_dict = {attr[0]: attr[1] for attr in row.attribute_columns}
                gps = attr_dict.get('gps', '')
                name = attr_dict.get('name', '')
                lat_str, lon_str = gps.split(',')
                self.googleMap2D.add_gps_marker((float(lat_str.strip()), float(lon_str.strip())), name)
                self.googleMap2D.receive_place((float(lat_str.strip()), float(lon_str.strip())), name)

        except Exception as e:
            print(f"查询失败: {e}")
    
    def _toggle_device_location(self):
        """切换显示/隐藏设备位置"""
        # 如果有设备标记，清除所有标记并停止闪烁计时器
        if self.googleMap2D._device_markers:
            self.googleMap2D._device_markers.clear()
            self.googleMap2D._blink_timer.stop()
            self.googleMap2D._update_display()
            self.googleMap2D.clear_device()
            print("已隐藏所有设备位置")
            return

        # 查询设备位置数据
        columns_to_get = ['deviceId', 'gps', 'time']
        inclusive_start_primary_key = [('deviceId', INF_MAX)]
        exclusive_end_primary_key = [('deviceId', INF_MIN)]

        try:
            consumed, next_start_primary_key, device_list, next_token = self.client.get_range(
                table_name=settings.DEVICETTABLE_NAME,
                direction=Direction.BACKWARD,
                inclusive_start_primary_key=inclusive_start_primary_key,
                exclusive_end_primary_key=exclusive_end_primary_key,
                columns_to_get=columns_to_get,
                limit=20
            )

            print(f"成功读取 {len(device_list)} 条最新记录。")
            for row_idx, row in enumerate(device_list):
                # 解析主键
                primary_key_dict = {key[0]: key[1] for key in row.primary_key}
                deviceId = primary_key_dict.get('deviceId', '')
                attr_dict = {attr[0]: attr[1] for attr in row.attribute_columns}
                gps_str = attr_dict.get('gps', '')
                time_str = attr_dict.get('time', '')

                # 解析GPS坐标
                if gps_str:
                    lat_str, lon_str = gps_str.split(',')
                    gps = (float(lat_str.strip()), float(lon_str.strip()))

                    # 添加设备标记（默认不闪烁，不灰色）
                    self.googleMap2D.add_device_marker(deviceId, gps, time_str, is_gray=False, loop=False)
                    self.googleMap2D.receive_device(deviceId, gps, time_str)

        except Exception as e:
            print(f"查询失败: {e}")
    
    def _create_map_control_buttons(self, parent_container):
        """创建地图控制按钮（右下角4个小图标）"""
        # 创建按钮容器
        button_container = QWidget(parent_container)
        button_container.setFixedSize(180, 40)
        button_container.setObjectName("mapControlButtons")
        button_container.setStyleSheet("""
            QWidget#mapControlButtons {
                background-color: transparent;
            }
        """)
        button_container.raise_()
        
        # 创建水平布局
        button_layout = QHBoxLayout(button_container)
        button_layout.setContentsMargins(8, 5, 8, 5)
        button_layout.setSpacing(10)
        
        # 创建四个按钮
        btn_route = QPushButton()
        btn_route.setFixedSize(30, 30)
        btn_route.setStyleSheet("""
            QPushButton { 
                border: none; 
                background-color: rgba(255, 255, 255, 200); 
                border-radius: 4px; 
            } 
            QPushButton:hover { 
                background-color: rgba(255, 255, 255, 255); 
            }
        """)
        btn_route.setIcon(create_route_icon())
        btn_route.setIconSize(QSize(24, 24))
        btn_route.setToolTip("显示/隐藏道路")
        btn_route.clicked.connect(self._add_route_to_map)
        
        btn_place = QPushButton()
        btn_place.setFixedSize(30, 30)
        btn_place.setStyleSheet("""
            QPushButton { 
                border: none; 
                background-color: rgba(255, 255, 255, 200); 
                border-radius: 4px; 
            } 
            QPushButton:hover { 
                background-color: rgba(255, 255, 255, 255); 
            }
        """)
        btn_place.setIcon(create_place_icon())
        btn_place.setIconSize(QSize(24, 24))
        btn_place.setToolTip("显示/隐藏地名")
        btn_place.clicked.connect(self._add_place_to_map)
        
        btn_device = QPushButton()
        btn_device.setFixedSize(30, 30)
        btn_device.setStyleSheet("""
            QPushButton { 
                border: none; 
                background-color: rgba(255, 255, 255, 200); 
                border-radius: 4px; 
            } 
            QPushButton:hover { 
                background-color: rgba(255, 255, 255, 255); 
            }
        """)
        btn_device.setIcon(create_device_icon())
        btn_device.setIconSize(QSize(24, 24))
        btn_device.setToolTip("显示/隐藏设备位置")
        btn_device.clicked.connect(self._toggle_device_location)

        btn_center = QPushButton()
        btn_center.setFixedSize(30, 30)
        btn_center.setStyleSheet("""
                   QPushButton { 
                       border: none; 
                       background-color: rgba(255, 255, 255, 200); 
                       border-radius: 4px; 
                   } 
                   QPushButton:hover { 
                       background-color: rgba(255, 255, 255, 255); 
                   }
               """)
        btn_center.setIcon(create_center_icon())
        btn_center.setIconSize(QSize(24, 24))
        btn_center.setToolTip("回到原始位置")
        btn_center.clicked.connect(lambda: self.googleMap2D.center_on_gps(settings.centenGps))

        # 添加按钮到布局
        button_layout.addWidget(btn_route)
        button_layout.addWidget(btn_place)
        button_layout.addWidget(btn_device)
        button_layout.addWidget(btn_center)

        btn_changeScene = QPushButton()
        btn_changeScene.setFixedSize(30, 30)
        btn_changeScene.setStyleSheet("""
                   QPushButton { 
                       border: none; 
                       background-color: rgba(255, 255, 255, 200); 
                       border-radius: 4px; 
                   } 
                   QPushButton:hover { 
                       background-color: rgba(255, 255, 255, 255); 
                   }
               """)
        btn_changeScene.setIcon(create_changescene_icon())
        btn_changeScene.setIconSize(QSize(24, 24))
        btn_changeScene.setToolTip("换场景")
        btn_changeScene.clicked.connect( self.toggle_map_positions)

        # 添加按钮到布局
        button_layout.addWidget(btn_route)
        button_layout.addWidget(btn_place)
        button_layout.addWidget(btn_device)
        button_layout.addWidget(btn_center)
        button_layout.addWidget(btn_changeScene)
        
        # 保存按钮容器引用
        self.map_control_buttons = button_container
        button_container.show()
    
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
    
    def _update_map2d_position(self):
        """更新2D地图位置到右上角"""
        if hasattr(self, 'map2d_container'):
            # 获取left_widget
            central = self.centralWidget()
            if central and central.layout():
                left_widget = central.layout().itemAt(0).widget()
                if left_widget:
                    x_pos = left_widget.width() - 260  # 右边距10px
                    y_pos = 10  # 上边距10px
                    self.map2d_container.move(x_pos, y_pos)
                    self.map2d_container.raise_()  # 确保在最上层
                    print(f"✅ 2D地图已定位: x={x_pos}, y={y_pos}")
                    
                    # 同时更新控制按钮位置（右下角）
                    self._update_map_control_buttons_position(left_widget)
    
    def _position_scene3d_small(self):
        """设置小窗口3D场景位置到右上角"""
        if hasattr(self, 'scene3d_small_container'):
            left_widget = self.centralWidget().layout().itemAt(0).widget()
            if left_widget:
                x_pos = left_widget.width() - 260
                y_pos = 10
                self.scene3d_small_container.move(x_pos, y_pos)
                self.scene3d_small_container.raise_()
                print(f"✅ 3D小窗已定位: x={x_pos}, y={y_pos}")

    def rerishUi(self):
        # 有待优化

        QTimer.singleShot(1, self.googleMap2D.resizePostioniUi)
        QTimer.singleShot(50, self.googleMap2D.resizePostioniUi)
        QTimer.singleShot(100, self.googleMap2D.resizePostioniUi)
        QTimer.singleShot(200, self.googleMap2D.resizePostioniUi)
        QTimer.singleShot(500, self.googleMap2D.resizePostioniUi)
        QTimer.singleShot(1000, self.googleMap2D.resizePostioniUi)
        QTimer.singleShot(1500, self.googleMap2D.resizePostioniUi)

    def toggle_map_positions(self):
        """切换2D地图和3D场景的位置和大小"""
        print(f"🔄 切换地图位置... (当前模式: {settings.current_mode})")
        
        if not hasattr(self, 'map2d_container') or not hasattr(self, 'scene3d_widget'):
            print("❌ 错误：缺少必要的组件")
            return



        
        left_widget = self.centralWidget().layout().itemAt(0).widget()
        left_layout = left_widget.layout()


        
        if settings.current_mode == "small_2d":
            # 当前是小窗口模式，切换到大屏模式
            print("📍 切换到：2D地图大屏 + 3D场景小窗")
            
            # 1. 隐藏小窗口模式的2D地图容器
            self.map2d_container.hide()
            
            # 2. 从left_layout中移除3D场景
            left_layout.removeWidget(self.scene3d_widget)
            self.scene3d_widget.setParent(None)
            
            # 3. 创建新的3D场景小窗口容器（绝对定位）
            self.scene3d_small_container = Map2DContainer(left_widget)
            self.scene3d_small_container.setFixedSize(250, 250)
            self.scene3d_small_container.setObjectName("scene3dSmallContainer")
            self.scene3d_small_container.setStyleSheet("""
                QWidget#scene3dSmallContainer {
                    background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                        stop:0 rgba(44, 62, 80, 0.95),
                        stop:1 rgba(26, 26, 46, 0.95));
                    border: 3px solid;
                    border-image: linear-gradient(45deg, #e74c3c, #f39c12, #3498db, #9b59b6);
                    border-radius: 10px;
                }
            """)
            
            # 添加阴影效果
            shadow = QGraphicsDropShadowEffect()
            shadow.setBlurRadius(20)
            shadow.setXOffset(3)
            shadow.setYOffset(3)
            shadow.setColor(QColor(0, 0, 0, 150))
            self.scene3d_small_container.setGraphicsEffect(shadow)
            
            # 将3D场景放入小窗口容器
            scene3d_small_layout = QVBoxLayout(self.scene3d_small_container)
            scene3d_small_layout.setContentsMargins(0, 0, 0, 0)
            scene3d_small_layout.addWidget(self.scene3d_widget)
            
            # 显示小窗口容器
            self.scene3d_small_container.show()
            
            # 设置初始位置到右上角
            QTimer.singleShot(150, self._position_scene3d_small)
            
            # 4. 显示2D地图作为全屏背景
            self.googleMap2D.show()
            left_layout.addWidget(self.googleMap2D)

            self._update_map_control_buttons_position(left_widget)

            settings .current_mode = "large_2d"
            print("✅ 切换完成：2D地图全屏，3D场景小窗")
            self.rerishUi()



        else:
            # 当前是大屏模式，切换到小窗口模式
            print("📍 切换到：3D场景大屏 + 2D地图小窗")
            
            # 1. 从left_layout中移除2D地图
            if self.googleMap2D.parent() == left_widget:
                left_layout.removeWidget(self.googleMap2D)
                print("  ✅ 已从left_layout移除googleMap2D")
            
            # 2. 隐藏并清理3D场景小窗口容器
            if hasattr(self, 'scene3d_small_container'):
                # 先从容器中取出3D场景
                container_layout = self.scene3d_small_container.layout()
                if container_layout and container_layout.count() > 0:
                    item = container_layout.takeAt(0)
                    if item and item.widget():
                        item.widget().setParent(None)
                
                # 隐藏并销毁小窗口容器
                self.scene3d_small_container.hide()
                self.scene3d_small_container.setParent(None)
                del self.scene3d_small_container
                print("  ✅ 已清理scene3d_small_container")
            
            # 3. 恢复3D场景到全屏
            left_layout.addWidget(self.scene3d_widget)
            print("  ✅ 已恢复3D场景到全屏")
            
            # 4. 重新将2D地图添加到内部容器（关键：先设置父容器）
            if hasattr(self, 'inner_container'):
                # 先设置父容器，再添加到布局
                self.googleMap2D.setParent(self.inner_container)
                inner_layout = self.inner_container.layout()
                if inner_layout:
                    # 检查是否已经在布局中
                    already_added = False
                    for i in range(inner_layout.count()):
                        if inner_layout.itemAt(i).widget() == self.googleMap2D:
                            already_added = True
                            break
                    
                    if not already_added:
                        inner_layout.addWidget(self.googleMap2D)
                        print("  ✅ 已将googleMap2D添加到inner_container")
                    else:
                        print("  ⚠️ googleMap2D已在inner_container中")
                
                # 强制设置widget的大小策略，让它适应父容器
                from PyQt6.QtWidgets import QSizePolicy
                self.googleMap2D.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
                # 强制更新布局
                inner_layout.update()
                self.inner_container.updateGeometry()
            
            # 5. 显示所有组件
            self.googleMap2D.show()
            self.inner_container.show()
            self.map2d_container.show()
            self.map2d_container.raise_()  # 确保在最上层
            print("  ✅ 已显示所有组件")
            
            # 6. 强制更新2D地图widget的尺寸（触发resizeEvent）
            QTimer.singleShot(50, lambda: self.googleMap2D.resize(self.inner_container.size()))
            
            # 7. 更新2D地图容器位置
            QTimer.singleShot(150, self._update_map2d_position)
            
            # 8. 重新定位到中心GPS坐标（会自动触发resizeEvent更新红点）
            if hasattr(self.googleMap2D, '_center_gps_coord') and self.googleMap2D._center_gps_coord:
                QTimer.singleShot(200, lambda: self.googleMap2D.center_on_gps(self.googleMap2D._center_gps_coord))
            
            settings.current_mode = "small_2d"
            print("✅ 切换完成：3D场景全屏，2D地图小窗")
            self.rerishUi()


    
    def _update_map_control_buttons_position(self, left_widget):
        """更新地图控制按钮位置（左侧区域右下角）"""
        if hasattr(self, 'map_control_buttons'):
            # 使用left_widget的尺寸而不是map2d_container
            container_width = left_widget.width()
            container_height = left_widget.height()
            button_width = self.map_control_buttons.width()
            button_height = self.map_control_buttons.height()
            
            # 计算右下角位置（相对于left_widget）
            x_pos = container_width - button_width - 10  # 右边距10px
            y_pos = container_height - button_height - 10  # 下边距10px
            
            self.map_control_buttons.move(x_pos, y_pos)
            self.map_control_buttons.raise_()
    
    def resizeEvent(self, event):
        """窗口大小改变事件"""
        super().resizeEvent(event)
        # 延迟更新位置，确保布局已经完成
        QTimer.singleShot(100, self._update_map2d_position)
    
    def showEvent(self, event):
        """窗口显示事件，设置2D地图位置"""
        super().showEvent(event)
        # 延迟设置位置，确保布局已经完成
        QTimer.singleShot(200, self._update_map2d_position)
        # 同时更新按钮位置
        left_widget = self.centralWidget().layout().itemAt(0).widget() if self.centralWidget() and self.centralWidget().layout() else None
        self._update_map_control_buttons_position(left_widget)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())
