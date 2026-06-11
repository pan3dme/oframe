
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QPushButton,
                              QRadioButton, QCheckBox, QDialogButtonBox, QLineEdit,
                              QTableWidget, QTableWidgetItem, QHeaderView, QLabel)
from PyQt6.QtGui import QFont, QPixmap
from PyQt6.QtCore import Qt, QUrl
import os
from PyQt6.QtWidgets import QApplication, QMainWindow
from PyQt6.QtWebEngineWidgets import QWebEngineView
from PyQt6.QtCore import QUrl
from tablestore import INF_MAX, INF_MIN, Direction

from config import settings
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

        # 创建表格
        self.table = QTableWidget()

        self.table.setColumnCount(6)  # 设置6列：deviceId, link_cowsheep_id, gps, time, upDateDevice, 图片
        self.table.setHorizontalHeaderLabels(['deviceId', 'link_cowsheep_id', 'gps', 'time', 'upDateDevice', '图片'])

        # 设置表格样式
        self.table.setStyleSheet("""
            QTableWidget {
                background-color: white;
                border: 1px solid #cccccc;
                gridline-color: #e0e0e0;
            }
            QTableWidget::item {
                padding: 5px;
            }
            QHeaderView::section {
                background-color: #f0f0f0;
                padding: 8px;
                border: 1px solid #cccccc;
                font-weight: bold;
            }
            QTableWidget::item:selected {
                background-color: #0078d7;
                color: white;
            }
        """)

        # 设置列宽比例为 1:1:2:2:1:2
        header = self.table.horizontalHeader()
        header.setSectionResizeMode(QHeaderView.ResizeMode.Fixed)
        header.setStretchLastSection(False)

        # 设置表格行高
        self.table.verticalHeader().setDefaultSectionSize(80)  # 增加行高以显示图片
        self.table.verticalHeader().setVisible(False)  # 隐藏行号

        self.top_layout.addWidget(self.table)
        layout.addWidget(self.top_widget, stretch=1)
        self.loadData()

    def resizeEvent(self, event):
        """重写 resizeEvent 以保持列宽比例 1:1:2:2:1:2"""
        super().resizeEvent(event)
        total_width = self.table.width()
        # 按比例 1:1:2:2:1:2 分配宽度
        col_width = total_width / 9  # 总共9份
        self.table.setColumnWidth(0, int(col_width * 1))
        self.table.setColumnWidth(1, int(col_width * 1))
        self.table.setColumnWidth(2, int(col_width * 2))
        self.table.setColumnWidth(3, int(col_width * 2))
        self.table.setColumnWidth(4, int(col_width * 1))
        self.table.setColumnWidth(5, int(col_width * 2))



    def loadData(self):


        # 设备表查询
        columns_to_get = ['link_cowsheep_id', 'gps', 'time', 'upDateDevice', 'picurl']
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

            # 处理查询结果并显示在表格中
            print(f"成功读取 {len(row_list)} 条记录。")

            # 设置表格行数
            self.table.setRowCount(len(row_list))

            # 遍历每条记录
            for row_idx, row in enumerate(row_list):
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

                # 创建表格项并设置值
                item_pk = QTableWidgetItem(str(pk_value))
                item_gps = QTableWidgetItem(str(gps))
                item_link_cowsheep_id = QTableWidgetItem(str(link_cowsheep_id))
                item_time = QTableWidgetItem(str(time))
                item_upDateDevice = QTableWidgetItem(str(upDateDevice))

                # 设置表格项居中对齐
                for item in [item_pk, item_gps, item_link_cowsheep_id, item_time, item_upDateDevice]:
                    item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)

                # 将表格项添加到表格中
                self.table.setItem(row_idx, 0, item_pk)
                self.table.setItem(row_idx, 1, item_link_cowsheep_id)
                self.table.setItem(row_idx, 2, item_gps)
                self.table.setItem(row_idx, 3, item_time)
                self.table.setItem(row_idx, 4, item_upDateDevice)
                
                # 显示图片
                pic_label = QLabel()
                pic_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
                pic_label.setStyleSheet("""
                    QLabel {
                        border: 1px solid #e0e0e0;
                        background-color: #f9f9f9;
                    }
                """)
                
                if picurl and str(picurl).strip():
                    # 如果有图片URL，尝试加载图片
                    pixmap = QPixmap()
                    pic_url_str = str(picurl).strip()
                    
                    # 判断是网络URL还是本地路径
                    if pic_url_str.startswith(('http://', 'https://')):
                        # 网络图片 - 使用异步加载
                        self.loadImageAsync(pic_url_str, pic_label)
                    else:
                        # 本地图片路径
                        if not os.path.isabs(pic_url_str):
                            # 如果是相对路径，转换为绝对路径
                            pic_url_str = os.path.join(os.getcwd(), pic_url_str)
                        
                        if os.path.exists(pic_url_str):
                            pixmap.load(pic_url_str)
                            if not pixmap.isNull():
                                # 缩放图片以适应单元格
                                scaled_pixmap = pixmap.scaled(60, 60, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
                                pic_label.setPixmap(scaled_pixmap)
                            else:
                                pic_label.setText("加载失败")
                        else:
                            pic_label.setText("图片不存在")
                else:
                    pic_label.setText("无图片")
                
                # 将QLabel设置为表格单元格部件
                self.table.setCellWidget(row_idx, 5, pic_label)

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
                    # 缩放图片以适应单元格
                    scaled_pixmap = pixmap.scaled(60, 60, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
                    label.setPixmap(scaled_pixmap)
                else:
                    label.setText("格式错误")
            else:
                label.setText("网络错误")
        except Exception as e:
            print(f"处理图片数据失败: {e}")
            label.setText("处理失败")
        finally:
            reply.deleteLater()
