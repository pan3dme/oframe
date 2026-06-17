import sys
import os
from PyQt6.QtCore import Qt, QUrl, QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
    QGraphicsDropShadowEffect
)
from PyQt6.QtWebEngineWidgets import QWebEngineView
from PyQt6.QtWebEngineCore import QWebEngineSettings
from tablestore import OTSClient

from display3d.google_scene3d import GoogleScene3D
from crowui.google_map2d_widget import GoogleMap2DWidget
from config import settings


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
        self.current_mode = "small_2d"  # 跟踪当前模式："small_2d" 或 "large_2d"
        self.setWindowTitle("高德地图应用")
        self.setGeometry(100, 100, 1440, 800)
        
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
        
        # 将左侧部件添加到主布局，设置拉伸因子为5
        main_layout.addWidget(left_widget, stretch=5)
        
        # ==================== 右侧区域 - 按钮面板 ====================
        right_widget = QWidget()
        right_layout = QVBoxLayout(right_widget)
        right_layout.setContentsMargins(20, 20, 20, 20)
        right_layout.setSpacing(15)
        
        # 设置右侧背景色
        right_widget.setAutoFillBackground(True)
        palette = right_widget.palette()
        palette.setColor(right_widget.backgroundRole(), QColor("#f5f5f5"))
        right_widget.setPalette(palette)
        
        # 创建4个按钮
        btn1 = QPushButton("按钮 1")
        btn1.setFixedHeight(50)
        btn1.setStyleSheet("""
            QPushButton {
                background-color: #3498db;
                color: white;
                border: none;
                border-radius: 5px;
                font-size: 14px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #2980b9;
            }
            QPushButton:pressed {
                background-color: #21618c;
            }
        """)
        
        btn2 = QPushButton("按钮 2")
        btn2.setFixedHeight(50)
        btn2.setStyleSheet("""
            QPushButton {
                background-color: #2ecc71;
                color: white;
                border: none;
                border-radius: 5px;
                font-size: 14px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #27ae60;
            }
            QPushButton:pressed {
                background-color: #1e8449;
            }
        """)
        
        btn3 = QPushButton("按钮 3")
        btn3.setFixedHeight(50)
        btn3.setStyleSheet("""
            QPushButton {
                background-color: #e74c3c;
                color: white;
                border: none;
                border-radius: 5px;
                font-size: 14px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #c0392b;
            }
            QPushButton:pressed {
                background-color: #922b21;
            }
        """)
        
        btn4 = QPushButton("按钮 4")
        btn4.setFixedHeight(50)
        btn4.setStyleSheet("""
            QPushButton {
                background-color: #f39c12;
                color: white;
                border: none;
                border-radius: 5px;
                font-size: 14px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #d68910;
            }
            QPushButton:pressed {
                background-color: #af601a;
            }
        """)
        
        # 创建切换位置按钮
        btn_toggle = QPushButton("🔄 切换地图位置")
        btn_toggle.setFixedHeight(50)
        btn_toggle.setStyleSheet("""
            QPushButton {
                background-color: #9b59b6;
                color: white;
                border: none;
                border-radius: 5px;
                font-size: 14px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #8e44ad;
            }
            QPushButton:pressed {
                background-color: #6c3483;
            }
        """)
        
        # 添加按钮到右侧布局
        right_layout.addWidget(btn1)
        right_layout.addWidget(btn2)
        right_layout.addWidget(btn3)
        right_layout.addWidget(btn4)
        right_layout.addWidget(btn_toggle)
        right_layout.addStretch()  # 添加弹性空间
        
        # 将右侧部件添加到主布局，设置拉伸因子为3
        main_layout.addWidget(right_widget, stretch=3)
        
        # 连接按钮信号（示例）
        btn1.clicked.connect(lambda: print("按钮 1 被点击"))
        btn2.clicked.connect(lambda: print("按钮 2 被点击"))
        btn3.clicked.connect(lambda: print("按钮 3 被点击"))
        btn4.clicked.connect(lambda: print("按钮 4 被点击"))
        btn_toggle.clicked.connect(self.toggle_map_positions)
        
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
    
    def toggle_map_positions(self):
        """切换2D地图和3D场景的位置和大小"""
        print(f"🔄 切换地图位置... (当前模式: {self.current_mode})")
        
        if not hasattr(self, 'map2d_container') or not hasattr(self, 'scene3d_widget'):
            print("❌ 错误：缺少必要的组件")
            return
        
        left_widget = self.centralWidget().layout().itemAt(0).widget()
        left_layout = left_widget.layout()
        
        if self.current_mode == "small_2d":
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
            
            self.current_mode = "large_2d"
            print("✅ 切换完成：2D地图全屏，3D场景小窗")
            
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
            
            # 5. 显示所有组件
            self.googleMap2D.show()
            self.inner_container.show()
            self.map2d_container.show()
            self.map2d_container.raise_()  # 确保在最上层
            print("  ✅ 已显示所有组件")
            
            QTimer.singleShot(150, self._update_map2d_position)
            
            self.current_mode = "small_2d"
            print("✅ 切换完成：3D场景全屏，2D地图小窗")
    
    def showEvent(self, event):
        """窗口显示事件，设置2D地图位置"""
        super().showEvent(event)
        # 延迟设置位置，确保布局已经完成
        QTimer.singleShot(200, self._update_map2d_position)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())
