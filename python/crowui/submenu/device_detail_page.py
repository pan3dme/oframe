from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QLabel, 
                              QScrollArea, QFrame)
from PyQt6.QtGui import QFont, QPixmap, QPainter, QPainterPath
from PyQt6.QtCore import Qt, pyqtSignal, QUrl
import os


class DeviceDetailPage(QWidget):
    """设备详情页面"""
    
    # 定义返回信号
    back_signal = pyqtSignal()
    
    def __init__(self, device_data, parent=None):
        super().__init__(parent)
        self.device_data = device_data
        self.setup_ui()
        self.load_device_info()
    
    def setup_ui(self):
        """设置UI布局"""
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(20, 20, 20, 20)
        main_layout.setSpacing(15)
        
        # === 顶部返回按钮区域 ===
        top_layout = QHBoxLayout()
        top_layout.setSpacing(10)
        
        # 返回按钮
        self.back_btn = QPushButton("← 返回")
        self.back_btn.setFont(QFont("Microsoft YaHei", 11, QFont.Weight.Bold))
        self.back_btn.setFixedHeight(40)
        self.back_btn.setStyleSheet("""
            QPushButton {
                background-color: #3498db;
                color: white;
                border: none;
                border-radius: 5px;
                padding: 5px 15px;
            }
            QPushButton:hover {
                background-color: #2980b9;
            }
            QPushButton:pressed {
                background-color: #21618c;
            }
        """)
        self.back_btn.clicked.connect(self.on_back_clicked)
        top_layout.addWidget(self.back_btn)
        
        # 标题
        title_label = QLabel("设备详情")
        title_label.setFont(QFont("Microsoft YaHei", 16, QFont.Weight.Bold))
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title_label.setStyleSheet("""
            QLabel {
                color: #2c3e50;
                padding: 5px;
            }
        """)
        top_layout.addWidget(title_label, stretch=1)
        
        main_layout.addLayout(top_layout)
        
        # === 内容区域（滚动）===
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
        
        content_widget = QWidget()
        content_layout = QVBoxLayout(content_widget)
        content_layout.setContentsMargins(10, 10, 10, 10)
        content_layout.setSpacing(15)
        
        # 设备图片
        self.pic_label = QLabel()
        self.pic_label.setFixedSize(150, 150)
        self.pic_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.pic_label.setStyleSheet("""
            QLabel {
                border: 2px solid #ddd;
                border-radius: 10px;
                background-color: #f9f9f9;
            }
        """)
        content_layout.addWidget(self.pic_label, alignment=Qt.AlignmentFlag.AlignHCenter)
        
        # 设备信息卡片容器
        info_card = QFrame()
        info_card.setFrameShape(QFrame.Shape.StyledPanel)
        info_card.setStyleSheet("""
            QFrame {
                background-color: white;
                border: 1px solid #e0e0e0;
                border-radius: 8px;
            }
        """)
        info_layout = QVBoxLayout(info_card)
        info_layout.setContentsMargins(20, 20, 20, 20)
        info_layout.setSpacing(12)
        
        # 设备ID
        self.device_id_label = QLabel()
        self.device_id_label.setFont(QFont("Microsoft YaHei", 14, QFont.Weight.Bold))
        self.device_id_label.setStyleSheet("color: #1a73e8; padding: 5px 0;")
        info_layout.addWidget(self.device_id_label)
        
        # 分隔线
        line1 = QFrame()
        line1.setFrameShape(QFrame.Shape.HLine)
        line1.setStyleSheet("background-color: #e0e0e0;")
        info_layout.addWidget(line1)
        
        # 设备编号
        self.device_key_label = QLabel()
        self.device_key_label.setFont(QFont("Microsoft YaHei", 11))
        self.device_key_label.setStyleSheet("color: #5f6368; padding: 5px 0;")
        info_layout.addWidget(self.device_key_label)
        
        # 分隔线
        line2 = QFrame()
        line2.setFrameShape(QFrame.Shape.HLine)
        line2.setStyleSheet("background-color: #e0e0e0;")
        info_layout.addWidget(line2)
        
        # 设备名称
        self.rename_label = QLabel()
        self.rename_label.setFont(QFont("Microsoft YaHei", 11))
        self.rename_label.setStyleSheet("color: #5f6368; padding: 5px 0;")
        info_layout.addWidget(self.rename_label)
        
        # 分隔线
        line3 = QFrame()
        line3.setFrameShape(QFrame.Shape.HLine)
        line3.setStyleSheet("background-color: #e0e0e0;")
        info_layout.addWidget(line3)
        
        # 链接牛羊
        self.link_cowsheep_label = QLabel()
        self.link_cowsheep_label.setFont(QFont("Microsoft YaHei", 11))
        self.link_cowsheep_label.setStyleSheet("color: #5f6368; padding: 5px 0;")
        info_layout.addWidget(self.link_cowsheep_label)
        
        # 分隔线
        line4 = QFrame()
        line4.setFrameShape(QFrame.Shape.HLine)
        line4.setStyleSheet("background-color: #e0e0e0;")
        info_layout.addWidget(line4)
        
        # GPS位置
        self.gps_label = QLabel()
        self.gps_label.setFont(QFont("Microsoft YaHei", 11))
        self.gps_label.setStyleSheet("color: #5f6368; padding: 5px 0;")
        info_layout.addWidget(self.gps_label)
        
        # 分隔线
        line5 = QFrame()
        line5.setFrameShape(QFrame.Shape.HLine)
        line5.setStyleSheet("background-color: #e0e0e0;")
        info_layout.addWidget(line5)
        
        # 时间
        self.time_label = QLabel()
        self.time_label.setFont(QFont("Microsoft YaHei", 11))
        self.time_label.setStyleSheet("color: #5f6368; padding: 5px 0;")
        info_layout.addWidget(self.time_label)
        
        # 分隔线
        line6 = QFrame()
        line6.setFrameShape(QFrame.Shape.HLine)
        line6.setStyleSheet("background-color: #e0e0e0;")
        info_layout.addWidget(line6)
        
        # 更新时间
        self.update_label = QLabel()
        self.update_label.setFont(QFont("Microsoft YaHei", 11))
        self.update_label.setStyleSheet("color: #5f6368; padding: 5px 0;")
        info_layout.addWidget(self.update_label)
        
        content_layout.addWidget(info_card)
        content_layout.addStretch()
        
        scroll_area.setWidget(content_widget)
        main_layout.addWidget(scroll_area, stretch=1)
        
        print("✅ 设备详情页面初始化完成")
    
    def load_device_info(self):
        """加载设备信息"""
        pk_value = self.device_data.get('deviceId', '')
        rename = self.device_data.get('rename', '')
        device_key = self.device_data.get('device_key', '')
        link_cowsheep_id = self.device_data.get('link_cowsheep_id', '')
        picurl = self.device_data.get('picurl', '')
        gps = self.device_data.get('gps', '')
        time = self.device_data.get('time', '')
        upDateDevice = self.device_data.get('upDateDevice', '')
        
        # 设置设备ID
        if rename and rename.strip():
            self.device_id_label.setText(f"设备ID: {pk_value} ({rename})")
        else:
            self.device_id_label.setText(f"设备ID: {pk_value}")
        
        # 设置设备编号
        if device_key and str(device_key).strip():
            self.device_key_label.setText(f"设备编号: {device_key}")
        else:
            self.device_key_label.setText("设备编号: 未设置")
        
        # 设置设备名称
        if rename and rename.strip():
            self.rename_label.setText(f"设备名称: {rename}")
        else:
            self.rename_label.setText("设备名称: 未设置")
        
        # 设置链接牛羊
        if link_cowsheep_id and link_cowsheep_id.strip():
            self.link_cowsheep_label.setText(f"链接牛羊: {link_cowsheep_id}")
        else:
            self.link_cowsheep_label.setText("链接牛羊: 未链接")
        
        # 设置GPS位置
        if gps and str(gps).strip():
            self.gps_label.setText(f"GPS位置: {gps}")
        else:
            self.gps_label.setText("GPS位置: 未获取")
        
        # 设置时间
        if time and str(time).strip():
            self.time_label.setText(f"上报时间: {time}")
        else:
            self.time_label.setText("上报时间: 未知")
        
        # 设置更新时间
        if upDateDevice and str(upDateDevice).strip():
            self.update_label.setText(f"更新时间: {upDateDevice}")
        else:
            self.update_label.setText("更新时间: 未知")
        
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
                        scaled_pixmap = pixmap.scaled(150, 150, Qt.AspectRatioMode.KeepAspectRatio, 
                                                     Qt.TransformationMode.SmoothTransformation)
                        
                        # 创建带圆角的图片
                        rounded_pixmap = QPixmap(150, 150)
                        rounded_pixmap.fill(Qt.GlobalColor.transparent)
                        painter = QPainter(rounded_pixmap)
                        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
                        path = QPainterPath()
                        path.addRoundedRect(0, 0, 150, 150, 10, 10)
                        painter.setClipPath(path)
                        painter.drawPixmap((150 - scaled_pixmap.width()) // 2, 
                                         (150 - scaled_pixmap.height()) // 2, 
                                         scaled_pixmap)
                        painter.end()
                        
                        self.pic_label.setPixmap(rounded_pixmap)
                    else:
                        self.pic_label.setText("加载失败")
                else:
                    self.pic_label.setText("图片不存在")
        else:
            self.pic_label.setText("无图片")
    
    def on_back_clicked(self):
        """返回按钮点击事件"""
        print("🔙 返回设备列表")
        self.back_signal.emit()
    
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
                    # 按比例缩放
                    scaled_pixmap = pixmap.scaled(150, 150, Qt.AspectRatioMode.KeepAspectRatio, 
                                                 Qt.TransformationMode.SmoothTransformation)
                    
                    # 创建带圆角的图片
                    rounded_pixmap = QPixmap(150, 150)
                    rounded_pixmap.fill(Qt.GlobalColor.transparent)
                    painter = QPainter(rounded_pixmap)
                    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
                    path = QPainterPath()
                    path.addRoundedRect(0, 0, 150, 150, 10, 10)
                    painter.setClipPath(path)
                    painter.drawPixmap((150 - scaled_pixmap.width()) // 2, 
                                     (150 - scaled_pixmap.height()) // 2, 
                                     scaled_pixmap)
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
