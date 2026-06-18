from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QLabel, 
                              QScrollArea, QFrame)
from PyQt6.QtGui import QFont, QPixmap, QPainter, QPainterPath
from PyQt6.QtCore import Qt, pyqtSignal, QUrl
import os


class CowSheepDetailPage(QWidget):
    """牛羊详情页面"""
    
    # 定义返回信号
    back_signal = pyqtSignal()
    
    def __init__(self, cowsheep_data, parent=None):
        super().__init__(parent)
        self.cowsheep_data = cowsheep_data
        self.setup_ui()
        self.load_cowsheep_info()
    
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
                background-color: #2ecc71;
                color: white;
                border: none;
                border-radius: 5px;
                padding: 5px 15px;
            }
            QPushButton:hover {
                background-color: #27ae60;
            }
            QPushButton:pressed {
                background-color: #1e8449;
            }
        """)
        self.back_btn.clicked.connect(self.on_back_clicked)
        top_layout.addWidget(self.back_btn)
        
        # 标题
        title_label = QLabel("牛羊详情")
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
        
        # 牛羊图片
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
        
        # 牛羊信息卡片容器
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
        
        # 牛羊ID
        self.cowsheep_id_label = QLabel()
        self.cowsheep_id_label.setFont(QFont("Microsoft YaHei", 14, QFont.Weight.Bold))
        self.cowsheep_id_label.setStyleSheet("color: #2ecc71; padding: 5px 0;")
        info_layout.addWidget(self.cowsheep_id_label)
        
        # 分隔线
        line1 = QFrame()
        line1.setFrameShape(QFrame.Shape.HLine)
        line1.setStyleSheet("background-color: #e0e0e0;")
        info_layout.addWidget(line1)
        
        # 牛羊名称
        self.rename_label = QLabel()
        self.rename_label.setFont(QFont("Microsoft YaHei", 11))
        self.rename_label.setStyleSheet("color: #5f6368; padding: 5px 0;")
        info_layout.addWidget(self.rename_label)
        
        # 分隔线
        line2 = QFrame()
        line2.setFrameShape(QFrame.Shape.HLine)
        line2.setStyleSheet("background-color: #e0e0e0;")
        info_layout.addWidget(line2)
        
        # 性别
        self.gender_label = QLabel()
        self.gender_label.setFont(QFont("Microsoft YaHei", 11))
        self.gender_label.setStyleSheet("color: #5f6368; padding: 5px 0;")
        info_layout.addWidget(self.gender_label)
        
        # 分隔线
        line3 = QFrame()
        line3.setFrameShape(QFrame.Shape.HLine)
        line3.setStyleSheet("background-color: #e0e0e0;")
        info_layout.addWidget(line3)
        
        # 生日
        self.birthday_label = QLabel()
        self.birthday_label.setFont(QFont("Microsoft YaHei", 11))
        self.birthday_label.setStyleSheet("color: #5f6368; padding: 5px 0;")
        info_layout.addWidget(self.birthday_label)
        
        # 分隔线
        line4 = QFrame()
        line4.setFrameShape(QFrame.Shape.HLine)
        line4.setStyleSheet("background-color: #e0e0e0;")
        info_layout.addWidget(line4)
        
        # 绑定设备
        self.devices_label = QLabel()
        self.devices_label.setFont(QFont("Microsoft YaHei", 11))
        self.devices_label.setStyleSheet("color: #5f6368; padding: 5px 0;")
        info_layout.addWidget(self.devices_label)
        
        content_layout.addWidget(info_card)
        content_layout.addStretch()
        
        scroll_area.setWidget(content_widget)
        main_layout.addWidget(scroll_area, stretch=1)
        
        print("✅ 牛羊详情页面初始化完成")
    
    def load_cowsheep_info(self):
        """加载牛羊信息"""
        pk_value = self.cowsheep_data.get('cowsheep_id', '')
        rename = self.cowsheep_data.get('rename', '')
        gender = self.cowsheep_data.get('gender', '')
        birthday = self.cowsheep_data.get('birthday', '')
        avatar = self.cowsheep_data.get('avatar', '')
        bound_devices = self.cowsheep_data.get('bound_devices', [])
        
        # 设置牛羊ID
        if rename and rename.strip():
            self.cowsheep_id_label.setText(f"牛羊ID: {pk_value} ({rename})")
        else:
            self.cowsheep_id_label.setText(f"牛羊ID: {pk_value}")
        
        # 设置牛羊名称
        if rename and rename.strip():
            self.rename_label.setText(f"牛羊名称: {rename}")
        else:
            self.rename_label.setText("牛羊名称: 未设置")
        
        # 设置性别
        gender_text = "公" if str(gender).strip() == "1" else "母" if str(gender).strip() == "2" else str(gender)
        self.gender_label.setText(f"性别: {gender_text}")
        
        # 设置生日
        if birthday and str(birthday).strip():
            self.birthday_label.setText(f"生日: {birthday}")
        else:
            self.birthday_label.setText("生日: 未知")
        
        # 设置绑定设备
        if bound_devices:
            devices_str = ", ".join([str(d) for d in bound_devices])
            self.devices_label.setText(f"绑定设备: {devices_str}")
        else:
            self.devices_label.setText("绑定设备: 未绑定")
        
        # 加载图片
        if avatar and str(avatar).strip():
            pixmap = QPixmap()
            avatar_url_str = str(avatar).strip()
            
            if avatar_url_str.startswith(('http://', 'https://')):
                # 网络图片 - 异步加载
                parent_widget = self.parent()
                while parent_widget and not hasattr(parent_widget, 'loadImageAsync'):
                    parent_widget = parent_widget.parent()
                if parent_widget and hasattr(parent_widget, 'loadImageAsync'):
                    parent_widget.loadImageAsync(avatar_url_str, self.pic_label)
            else:
                # 本地图片
                if not os.path.isabs(avatar_url_str):
                    avatar_url_str = os.path.join(os.getcwd(), avatar_url_str)
                
                if os.path.exists(avatar_url_str):
                    pixmap.load(avatar_url_str)
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
        print("🔙 返回牛羊列表")
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
