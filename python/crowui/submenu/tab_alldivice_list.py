
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QPushButton,
                              QRadioButton, QCheckBox, QDialogButtonBox, QLineEdit,
                              QTableWidget, QTableWidgetItem, QHeaderView)
from PyQt6.QtGui import QFont
from PyQt6.QtCore import Qt
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

        self.table.setColumnCount(6)  # 设置5列：deviceId, gps, lorastr, time, upDateDevice
        self.table.setHorizontalHeaderLabels(['deviceId', 'link_cowsheep_id', 'gps', 'lorastr', 'time', 'upDateDevice'])

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
        """重写 resizeEvent 以保持列宽比例 1:2:2:2:1"""
        super().resizeEvent(event)
        total_width = self.table.width()
        # 按比例 1:2:2:2:1 分配宽度
        col_width = total_width / 9  # 总共8份
        self.table.setColumnWidth(0, int(col_width * 1))
        self.table.setColumnWidth(1, int(col_width * 1))
        self.table.setColumnWidth(2, int(col_width * 2))
        self.table.setColumnWidth(3, int(col_width * 2))
        self.table.setColumnWidth(4, int(col_width * 2))
        self.table.setColumnWidth(5, int(col_width * 1))



    def loadData(self):


        # 设备表查询
        columns_to_get = ['link_cowsheep_id', 'gps', 'lorastr', 'time', 'upDateDevice']
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
                lorastr = attr_dict.get('lorastr', '')
                time = attr_dict.get('time', '')
                upDateDevice = attr_dict.get('upDateDevice', '')

                # 创建表格项并设置值
                item_pk = QTableWidgetItem(str(pk_value))
                item_gps = QTableWidgetItem(str(gps))
                item_link_cowsheep_id = QTableWidgetItem(str(link_cowsheep_id))
                item_lorastr = QTableWidgetItem(str(lorastr))
                item_time = QTableWidgetItem(str(time))
                item_upDateDevice = QTableWidgetItem(str(upDateDevice))

                # 设置表格项居中对齐
                for item in [item_pk, item_gps,item_link_cowsheep_id, item_lorastr, item_time, item_upDateDevice]:
                    item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)

                # 将表格项添加到表格中
                self.table.setItem(row_idx, 0, item_pk)
                self.table.setItem(row_idx, 1, item_link_cowsheep_id)
                self.table.setItem(row_idx, 2, item_gps)
                self.table.setItem(row_idx, 3, item_lorastr)
                self.table.setItem(row_idx, 4, item_time)
                self.table.setItem(row_idx, 5, item_upDateDevice)

        except Exception as e:
            print(f"查询失败: {e}")
