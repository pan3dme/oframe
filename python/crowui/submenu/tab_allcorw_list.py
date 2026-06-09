
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QPushButton,
                              QRadioButton, QCheckBox, QDialogButtonBox, QLineEdit,
                              QTableWidget, QTableWidgetItem, QHeaderView, QLabel)
from PyQt6.QtGui import QFont, QPixmap, QImage
from PyQt6.QtCore import Qt
import requests
import os
from PyQt6.QtWidgets import QApplication, QMainWindow
from PyQt6.QtWebEngineWidgets import QWebEngineView
from PyQt6.QtCore import QUrl
from tablestore import INF_MAX, INF_MIN, Direction

from config import settings
class TobAllcorwList(QWidget):
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


        self.button2 = QPushButton("获取所有牛羊数据")

        self.button2.setFont(QFont("Microsoft YaHei", 10))
        self.button2.clicked.connect(self.loadData)  # 连接按钮点击事件
        self.top_layout.addWidget(self.button2)

        # 创建表格
        self.table = QTableWidget()

        self.table.setColumnCount(6)  # 设置6列
        self.table.setHorizontalHeaderLabels(['cowsheep_id','avatar', 'gender', 'rename', 'birthday', '绑定设备'])


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

        # 设置列宽比例为 1:2:2:2:1
        header = self.table.horizontalHeader()
        header.setSectionResizeMode(QHeaderView.ResizeMode.Fixed)
        header.setStretchLastSection(False)

        # 设置表格行高
        self.table.verticalHeader().setDefaultSectionSize(30)
        self.table.verticalHeader().setVisible(False)  # 隐藏行号

        self.top_layout.addWidget(self.table)
        layout.addWidget(self.top_widget, stretch=1)
        self.loadData()

    def resizeEvent(self, event):
        """重写 resizeEvent 以保持列宽比例"""
        super().resizeEvent(event)
        total_width = self.table.width()
        # 按比例分配宽度，总共10份
        col_width = total_width / 10
        self.table.setColumnWidth(0, int(col_width * 1))
        self.table.setColumnWidth(1, int(col_width * 2))
        self.table.setColumnWidth(2, int(col_width * 1))
        self.table.setColumnWidth(3, int(col_width * 2))
        self.table.setColumnWidth(4, int(col_width * 2))
        self.table.setColumnWidth(5, int(col_width * 2))



    def _build_device_map(self):
        """查询设备表，构建 cowsheep_id -> deviceId 列表的映射"""
        device_map = {}  # key: cowsheep_id, value: [deviceId1, deviceId2, ...]
        try:
            columns_to_get = ['link_cowsheep_id']
            inclusive_start_primary_key = [('deviceId', INF_MAX)]
            exclusive_end_primary_key = [('deviceId', INF_MIN)]
            consumed, next_start_primary_key, row_list, next_token = self.client.get_range(
                table_name=settings.DEVICETTABLE_NAME,
                direction=Direction.BACKWARD,
                inclusive_start_primary_key=inclusive_start_primary_key,
                exclusive_end_primary_key=exclusive_end_primary_key,
                columns_to_get=columns_to_get,
                limit=500
            )
            for row in row_list:
                pk_dict = {key[0]: key[1] for key in row.primary_key}
                device_id = pk_dict.get('deviceId', '')
                attr_dict = {attr[0]: attr[1] for attr in row.attribute_columns}
                link_cowsheep_id = str(attr_dict.get('link_cowsheep_id', '')).strip()
                if link_cowsheep_id:
                    if link_cowsheep_id not in device_map:
                        device_map[link_cowsheep_id] = []
                    device_map[link_cowsheep_id].append(str(device_id))
            print(f"设备映射表构建完成，共 {len(device_map)} 个牛羊绑定设备")
        except Exception as e:
            print(f"构建设备映射表失败: {e}")
        return device_map

    def loadData(self):

        columns_to_get = ['avatar', 'rename','gender','birthday' ]

        inclusive_start_primary_key = [('cowsheep_id', INF_MAX)]
        exclusive_end_primary_key = [('cowsheep_id', INF_MIN)]
        table_name = settings.COWSHEEP_TABLE_NAME
        pk_name="cowsheep_id"

        # 先构建设备映射表
        device_map = self._build_device_map()

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
                avatar = attr_dict.get('avatar', '')
                rename = attr_dict.get('rename', '')
                gender = attr_dict.get('gender', '')
                birthday = attr_dict.get('birthday', '')

                # 创建表格项并设置值
                item_pk = QTableWidgetItem(str(pk_value))
                item_avatar = QTableWidgetItem('')  # avatar列用图片显示
                item_rename = QTableWidgetItem(str(rename))
                item_gender = QTableWidgetItem(str(gender))
                item_birthday = QTableWidgetItem(str(birthday))

                # 查找绑定设备
                pk_str = str(pk_value).strip()
                bound_devices = device_map.get(pk_str, [])
                device_text = ', '.join(bound_devices) if bound_devices else '未绑定'
                item_device = QTableWidgetItem(device_text)

                # 设置表格项居中对齐
                for item in [item_pk, item_avatar, item_gender, item_rename, item_birthday, item_device]:
                    item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)

                # 将表格项添加到表格中
                self.table.setItem(row_idx, 0, item_pk)
                self.table.setItem(row_idx, 1, item_avatar)
                self.table.setItem(row_idx, 2, item_gender)
                self.table.setItem(row_idx, 3, item_rename)
                self.table.setItem(row_idx, 4, item_birthday)
                self.table.setItem(row_idx, 5, item_device)

                # avatar列加载网络图片
                if avatar:
                    self._load_avatar_image(row_idx, str(avatar))

        except Exception as e:
            print(f"查询失败: {e}")

    def _load_avatar_image(self, row_idx, url):
        """从网络URL加载图片并显示在avatar列"""
        try:
            response = requests.get(url, timeout=5)
            if response.status_code == 200:
                image = QImage()
                image.loadFromData(response.content)
                if not image.isNull():
                    pixmap = QPixmap.fromImage(image).scaled(
                        128, 128, Qt.AspectRatioMode.KeepAspectRatio,
                        Qt.TransformationMode.SmoothTransformation
                    )
                    label = QLabel()
                    label.setPixmap(pixmap)
                    label.setAlignment(Qt.AlignmentFlag.AlignCenter)
                    self.table.setCellWidget(row_idx, 1, label)
                    self.table.setRowHeight(row_idx, 135)
        except Exception as e:
            print(f"加载头像图片失败 (row {row_idx}): {e}")
