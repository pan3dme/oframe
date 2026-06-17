
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QPushButton,
                              QRadioButton, QCheckBox, QDialogButtonBox, QLineEdit,
                              QLabel, QScrollArea, QHBoxLayout, QFrame)
from PyQt6.QtGui import QFont, QPixmap
from PyQt6.QtCore import Qt, QUrl, pyqtSignal
import os
from PyQt6.QtWidgets import QApplication, QMainWindow
from PyQt6.QtWebEngineWidgets import QWebEngineView
from PyQt6.QtCore import QUrl
from tablestore import INF_MAX, INF_MIN, Direction

from config import settings


class DeviceCard(QFrame):
    """设备卡片组件"""
    # 定义信号
    edit_clicked = pyqtSignal(dict)  # 编辑按钮点击信号
    link_cowsheep_clicked = pyqtSignal(dict)  # 链接牛羊按钮点击信号
    
    def __init__(self, device_data, parent=None):
        super().__init__(parent)
        self.device_data = device_data
        self.setup_ui()
        self.load_data()
    
    def setup_ui(self):
        """设置UI布局 - 三列结构"""
        self.setFrameShape(QFrame.Shape.StyledPanel)
        self.setStyleSheet("""
            QFrame {
                background-color: white;
                border: 1px solid #e0e0e0;
                border-radius: 8px;
            }
            QFrame:hover {
                background-color: #f5f9ff;
                border: 1px solid #0078d7;
            }
        """)
        
        # 主布局 - 水平三列
        main_layout = QHBoxLayout(self)
        main_layout.setContentsMargins(0, 0, 0, 0)  # 移除内边距
        main_layout.setSpacing(12)
        main_layout.setAlignment(Qt.AlignmentFlag.AlignVCenter)  # 垂直居中对齐
        
        # 设置卡片固定高度为120
        self.setFixedHeight(120)
        
        # === 第一列：左侧图片区域 ===
        # 创建一个容器来让图片垂直居中
        pic_container = QWidget()
        pic_container.setFixedSize(120, 100)  # 宽度120，容纳左边距20+图片80+右边距20
        pic_container_layout = QVBoxLayout(pic_container)
        pic_container_layout.setContentsMargins(20, 0, 20, 0)  # 左右边距各20px
        pic_container_layout.setSpacing(0)
        pic_container_layout.setAlignment(Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignVCenter)  # 水平和垂直居中
        
        self.pic_label = QLabel()
        self.pic_label.setFixedSize(80, 80)  # 图片缩小到80x80
        self.pic_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.pic_label.setStyleSheet("""
            QLabel {
                border: none;
                background-color: transparent;
            }
        """)
        pic_container_layout.addWidget(self.pic_label)
        main_layout.addWidget(pic_container)
        
        # === 第二列：中间信息区域 ===
        info_widget = QWidget()
        info_layout = QVBoxLayout(info_widget)
        info_layout.setContentsMargins(0, 5, 10, 10)
        info_layout.setSpacing(5)
        
        # 设备名字（deviceId + rename）- 最大最醒目
        self.device_id_label = QLabel()
        self.device_id_label.setFont(QFont("Microsoft YaHei", 12, QFont.Weight.Bold))
        self.device_id_label.setAlignment(Qt.AlignmentFlag.AlignLeft)
        self.device_id_label.setStyleSheet("""
            QLabel {
                color: #1a73e8;
                padding: 2px 0px;
                background-color: transparent;
                border: none;
            }
            QLabel:hover {
                background-color: transparent;
                border: none;
            }
        """)
        self.device_id_label.setTextInteractionFlags(Qt.TextInteractionFlag.NoTextInteraction)
        self.device_id_label.setAttribute(Qt.WidgetAttribute.WA_Hover, False)
        info_layout.addWidget(self.device_id_label)
        
        # 设备编号 - 中等大小
        self.device_key_label = QLabel()
        self.device_key_label.setFont(QFont("Microsoft YaHei", 10))
        self.device_key_label.setAlignment(Qt.AlignmentFlag.AlignLeft)
        self.device_key_label.setStyleSheet("""
            QLabel {
                color: #5f6368;
                padding: 1px 0px;
                background-color: transparent;
                border: none;
            }
            QLabel:hover {
                background-color: transparent;
                border: none;
            }
        """)
        self.device_key_label.setTextInteractionFlags(Qt.TextInteractionFlag.NoTextInteraction)
        self.device_key_label.setAttribute(Qt.WidgetAttribute.WA_Hover, False)
        info_layout.addWidget(self.device_key_label)
        
        # 链接牛羊 - 最小
        self.link_cowsheep_label = QLabel()
        self.link_cowsheep_label.setFont(QFont("Microsoft YaHei", 9))
        self.link_cowsheep_label.setAlignment(Qt.AlignmentFlag.AlignLeft)
        self.link_cowsheep_label.setStyleSheet("""
            QLabel {
                color: #80868b;
                padding: 1px 0px;
                background-color: transparent;
                border: none;
            }
            QLabel:hover {
                background-color: transparent;
                border: none;
            }
        """)
        self.link_cowsheep_label.setTextInteractionFlags(Qt.TextInteractionFlag.NoTextInteraction)
        self.link_cowsheep_label.setAttribute(Qt.WidgetAttribute.WA_Hover, False)
        info_layout.addWidget(self.link_cowsheep_label)
        
        main_layout.addWidget(info_widget, stretch=1)
        
        # === 第三列：右侧按钮区域 ===
        button_widget = QWidget()
        button_layout = QVBoxLayout(button_widget)
        button_layout.setContentsMargins(15, 0, 15, 0)
        button_layout.setSpacing(0)
        
        # 编辑按钮
        self.edit_btn = QPushButton("编辑")
        self.edit_btn.setFont(QFont("Microsoft YaHei", 9))
        self.edit_btn.setFixedWidth(100)
        self.edit_btn.setStyleSheet("""
            QPushButton {
                background-color: #1a73e8;
                color: white;
                border: none;
                border-radius: 4px;
                padding: 5px;
            }
            QPushButton:hover {
                background-color: #1557b0;
            }
            QPushButton:pressed {
                background-color: #0d47a1;
            }
        """)
        self.edit_btn.clicked.connect(self.on_edit_clicked)
        button_layout.addWidget(self.edit_btn)
        
        main_layout.addWidget(button_widget)
    
    def on_edit_clicked(self):
        """编辑按钮点击事件"""
        self.edit_clicked.emit(self.device_data)
    
    def load_data(self):
        """加载设备数据"""
        pk_value = self.device_data.get('deviceId', '')
        rename = self.device_data.get('rename', '')
        device_key = self.device_data.get('device_key', '')
        link_cowsheep_id = self.device_data.get('link_cowsheep_id', '')
        picurl = self.device_data.get('picurl', '')
        
        # 调试输出
        print(f"卡片加载 - deviceId: {pk_value}, device_key: '{device_key}', rename: '{rename}'")
        
        # 设置文本信息
        # 设备名字：deviceId，如果有rename则在后面括号显示
        if rename and rename.strip():
            self.device_id_label.setText(f"{pk_value} ({rename})")
        else:
            self.device_id_label.setText(f"{pk_value}")
        
        # 设备编号：device_key，如果没有则提示输入
        print(f"检查 device_key: value='{device_key}', bool={bool(device_key)}, strip='{device_key.strip() if device_key else ''}'")
        if device_key and str(device_key).strip():
            self.device_key_label.setText(f"设备编号: {device_key}")
        else:
            self.device_key_label.setText("设备编号: 请输入唯一编号")
        
        if link_cowsheep_id and link_cowsheep_id.strip():
            self.link_cowsheep_label.setText(f"链接牛羊: {link_cowsheep_id}")
        else:
            self.link_cowsheep_label.setText("链接牛羊: 未链接")
        
        # 加载图片
        if picurl and str(picurl).strip():
            pixmap = QPixmap()
            pic_url_str = str(picurl).strip()
            
            if pic_url_str.startswith(('http://', 'https://')):
                # 网络图片 - 异步加载
                parent_widget = self.parent()
                while parent_widget and not hasattr(parent_widget, 'loadImageAsync'):
                    parent_widget = parent_widget.parent()
                if parent_widget and hasattr(parent_widget, 'loadImageAsync'):
                    parent_widget.loadImageAsync(pic_url_str, self.pic_label)
            else:
                # 本地图片
                if not os.path.isabs(pic_url_str):
                    pic_url_str = os.path.join(os.getcwd(), pic_url_str)
                
                if os.path.exists(pic_url_str):
                    pixmap.load(pic_url_str)
                    if not pixmap.isNull():
                        # 按比例缩放并居中裁剪以填满80x80区域
                        scaled_pixmap = pixmap.scaled(80, 80, Qt.AspectRatioMode.KeepAspectRatioByExpanding, Qt.TransformationMode.SmoothTransformation)
                        # 计算居中裁剪的位置
                        x = (scaled_pixmap.width() - 80) // 2
                        y = (scaled_pixmap.height() - 80) // 2
                        cropped_pixmap = scaled_pixmap.copy(x, y, 80, 80)
                        
                        # 创建带8px圆角的图片
                        rounded_pixmap = QPixmap(80, 80)
                        rounded_pixmap.fill(Qt.GlobalColor.transparent)
                        from PyQt6.QtGui import QPainter, QPainterPath
                        painter = QPainter(rounded_pixmap)
                        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
                        path = QPainterPath()
                        path.addRoundedRect(0, 0, 80, 80, 8, 8)
                        painter.setClipPath(path)
                        painter.drawPixmap(0, 0, cropped_pixmap)
                        painter.end()
                        
                        self.pic_label.setPixmap(rounded_pixmap)
                    else:
                        self.pic_label.setText("加载失败")
                else:
                    self.pic_label.setText("图片不存在")
        else:
            self.pic_label.setText("无图片")


class TabAllDiviceList(QWidget):
    """按钮面板组件"""
    def __init__(self, parent=None, client=None):
        super().__init__(parent)
        self.client = client  # 保存client对象

        layout = QVBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)
        layout.setSpacing(0)


        # 上半部分：控件区域
        self.top_widget = QWidget()
        self.top_layout = QVBoxLayout(self.top_widget)
        self.top_layout.setContentsMargins(5, 5, 5, 5)


        self.button2 = QPushButton("获取所有设备数据")
        self.button2.setFont(QFont("Microsoft YaHei", 10))
        self.button2.clicked.connect(self.loadData)  # 连接按钮点击事件
        self.top_layout.addWidget(self.button2)

        # 创建滚动区域
        scroll_area = QScrollArea()
        scroll_area.setWidgetResizable(True)
        scroll_area.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        scroll_area.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        scroll_area.setStyleSheet("""
            QScrollArea {
                border: none;
                background-color: transparent;
            }
            QScrollBar:vertical {
                border: none;
                background: #f0f0f0;
                width: 10px;
                border-radius: 5px;
            }
            QScrollBar::handle:vertical {
                background: #c0c0c0;
                border-radius: 5px;
                min-height: 30px;
            }
            QScrollBar::handle:vertical:hover {
                background: #a0a0a0;
            }
        """)
        
        # 创建容器widget用于放置卡片
        self.cards_container = QWidget()
        self.cards_layout = QVBoxLayout(self.cards_container)
        self.cards_layout.setContentsMargins(5, 5, 5, 5)
        self.cards_layout.setSpacing(10)
        self.cards_layout.addStretch()
        
        scroll_area.setWidget(self.cards_container)
        
        self.top_layout.addWidget(scroll_area)
        layout.addWidget(self.top_widget, stretch=1)
        self.loadData()





    def loadData(self):


        # 设备表查询
        columns_to_get = ['link_cowsheep_id', 'gps', 'time', 'upDateDevice', 'picurl', 'rename', 'device_key']
        inclusive_start_primary_key = [('deviceId', INF_MAX)]
        exclusive_end_primary_key = [('deviceId', INF_MIN)]
        table_name = settings.DEVICETTABLE_NAME
        pk_name = 'deviceId'

        try:
            # 执行范围查询，direction=BACKWARD 为倒序读取
            consumed, next_start_primary_key, row_list, next_token = self.client.get_range(
                table_name=table_name,
                direction=Direction.BACKWARD,  # 关键参数：反向查询
                inclusive_start_primary_key=inclusive_start_primary_key,
                exclusive_end_primary_key=exclusive_end_primary_key,
                columns_to_get=columns_to_get,
                limit=100  # 限制返回的行数，获取最新100条记录
            )

            # 处理查询结果并显示在卡片中
            print(f"成功读取 {len(row_list)} 条记录。")

            # 清除旧的卡片
            while self.cards_layout.count():
                item = self.cards_layout.takeAt(0)
                if item.widget():
                    item.widget().deleteLater()
            
            # 添加stretch确保卡片从顶部开始
            self.cards_layout.addStretch()

            # 遍历每条记录，创建卡片
            for row in row_list:
                # 解析主键
                primary_key_dict = {key[0]: key[1] for key in row.primary_key}
                pk_value = primary_key_dict.get(pk_name, '')

                # 解析属性列
                attr_dict = {attr[0]: attr[1] for attr in row.attribute_columns}
                gps = attr_dict.get('gps', '')
                link_cowsheep_id = attr_dict.get('link_cowsheep_id', '')
                time = attr_dict.get('time', '')
                upDateDevice = attr_dict.get('upDateDevice', '')
                picurl = attr_dict.get('picurl', '')
                rename = attr_dict.get('rename', '')
                device_key = attr_dict.get('device_key', '')
                
                # 调试输出
                print(f"设备 {pk_value}: device_key={device_key}, rename={rename}")
                
                # 构建设备数据字典
                device_data = {
                    'deviceId': pk_value,
                    'gps': gps,
                    'link_cowsheep_id': link_cowsheep_id,
                    'time': time,
                    'upDateDevice': upDateDevice,
                    'picurl': picurl,
                    'rename': rename,
                    'device_key': device_key
                }
                
                # 创建设备卡片
                card = DeviceCard(device_data, self)
                
                # 将卡片插入到stretch之前
                self.cards_layout.insertWidget(self.cards_layout.count() - 1, card)

        except Exception as e:
            print(f"查询失败: {e}")
    
    def loadImageAsync(self, url, label):
        """异步加载网络图片"""
        try:
            from PyQt6.QtNetwork import QNetworkAccessManager, QNetworkRequest
            
            # 创建网络访问管理器（如果不存在）
            if not hasattr(self, 'network_manager'):
                self.network_manager = QNetworkAccessManager()
            
            # 创建请求
            request = QNetworkRequest(QUrl(url))
            request.setHeader(QNetworkRequest.KnownHeaders.UserAgentHeader, "Mozilla/5.0")
            
            # 发送请求
            reply = self.network_manager.get(request)
            reply.finished.connect(lambda: self.onImageLoaded(reply, label))
        except Exception as e:
            print(f"加载网络图片失败: {e}")
            label.setText("加载失败")
    
    def onImageLoaded(self, reply, label):
        """图片加载完成回调"""
        try:
            if reply.error() == reply.NetworkError.NoError:
                data = reply.readAll()
                pixmap = QPixmap()
                if pixmap.loadFromData(data):
                    # 按比例缩放并居中裁剪以填满80x80区域
                    scaled_pixmap = pixmap.scaled(80, 80, Qt.AspectRatioMode.KeepAspectRatioByExpanding, Qt.TransformationMode.SmoothTransformation)
                    # 计算居中裁剪的位置
                    x = (scaled_pixmap.width() - 80) // 2
                    y = (scaled_pixmap.height() - 80) // 2
                    cropped_pixmap = scaled_pixmap.copy(x, y, 80, 80)
                    
                    # 创建带8px圆角的图片
                    rounded_pixmap = QPixmap(80, 80)
                    rounded_pixmap.fill(Qt.GlobalColor.transparent)
                    from PyQt6.QtGui import QPainter, QPainterPath
                    painter = QPainter(rounded_pixmap)
                    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
                    path = QPainterPath()
                    path.addRoundedRect(0, 0, 80, 80, 8, 8)
                    painter.setClipPath(path)
                    painter.drawPixmap(0, 0, cropped_pixmap)
                    painter.end()
                    
                    label.setPixmap(rounded_pixmap)
                else:
                    label.setText("格式错误")
            else:
                label.setText("网络错误")
        except Exception as e:
            print(f"处理图片数据失败: {e}")
            label.setText("处理失败")
        finally:
            reply.deleteLater()
