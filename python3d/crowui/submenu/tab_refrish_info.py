
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QPushButton,
                              QRadioButton, QCheckBox, QDialogButtonBox, QLineEdit,
                              QTableWidget, QTableWidgetItem, QHeaderView)
from PyQt6.QtGui import QFont
from PyQt6.QtCore import Qt

import config
from config import settings
from tablestore import INF_MAX, INF_MIN, Direction


class TobRefrishInfo(QWidget):
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

        self.button2 = QPushButton("获取最新数据")
        self.button2.setFont(QFont("Microsoft YaHei", 10))
        self.button2.clicked.connect(self.loadData)  # 连接按钮点击事件
        self.top_layout.addWidget(self.button2)

        # 创建表格
        self.table = QTableWidget()
        self.table.setColumnCount(4)
        self.table.setHorizontalHeaderLabels(['deviceId', 'lorastr', 'time', 'upDateDevice'])

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

        # 设置列宽比例为 1:2:2:1
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
        """重写 resizeEvent 以保持列宽比例 1:2:2:1"""
        super().resizeEvent(event)
        total_width = self.table.width()
        # 按比例 1:2:2:1 分配宽度
        col_width = total_width / 6  # 总共6份
        self.table.setColumnWidth(0, int(col_width * 1))
        self.table.setColumnWidth(1, int(col_width * 2))
        self.table.setColumnWidth(2, int(col_width * 2))
        self.table.setColumnWidth(3, int(col_width * 1))


    def loadData(self):



        # 1. 定义需要查询的数据列
        columns_to_get = ['lorastr', 'time', 'upDateDevice']  # 指定要获取的列名列表

        # 2. 定义主键范围：覆盖全表
        # 起始主键 (inclusive_start_primary_key) 设为最大值，表示从主键最大的行开始
        inclusive_start_primary_key = [('deviceId', INF_MAX), ('auto_id', INF_MAX)]
        # 结束主键 (exclusive_end_primary_key) 设为最小值，表示扫描到主键最小的行为止
        exclusive_end_primary_key = [('deviceId', INF_MIN), ('auto_id', INF_MIN)]

        try:
            # 3. 执行范围查询，direction=BACKWARD 为倒序读取
            consumed, next_start_primary_key, row_list, next_token = self.client.get_range(
                table_name=settings.LOG_TABLE_NAME,  # 表名
                direction=Direction.BACKWARD,  # 关键参数：反向查询，获取最新数据
                inclusive_start_primary_key=inclusive_start_primary_key,  # 起始主键（包含）
                exclusive_end_primary_key=exclusive_end_primary_key,  # 结束主键（不包含）
                columns_to_get=columns_to_get,  # 可选，指定要获取的列
                limit=20  # 限制返回的行数，即获取最新10条记录
            )

            # 4. 处理查询结果并显示在表格中
            print(f"成功读取 {len(row_list)} 条最新记录。")  # 打印读取的记录数量

            # 设置表格行数
            self.table.setRowCount(len(row_list))  # 根据查询结果设置表格的行数

            # 遍历每条记录
            for row_idx, row in enumerate(row_list):
                # 解析主键
                primary_key_dict = {key[0]: key[1] for key in row.primary_key}  # 将主键转换为字典
                deviceId = primary_key_dict.get('deviceId', '')  # 获取crow_idx值
                auto_id = primary_key_dict.get('auto_id', '')  # 获取auto_id值

                # 解析属性列
                attr_dict = {attr[0]: attr[1] for attr in row.attribute_columns}  # 将属性列转换为字典
                lorastr = attr_dict.get('lorastr', '')  # 获取gps值
                time = attr_dict.get('time', '')  # 获取time值
                upDateDevice = attr_dict.get('upDateDevice', '')  # 获取upDateDevice值

                # 创建表格项并设置值
                item_deviceId = QTableWidgetItem(str(deviceId))  # 创建crow_idx的表格项
                item_auto_id = QTableWidgetItem(str(auto_id))  # 创建auto_id的表格项
                item_lorastr = QTableWidgetItem(str(lorastr))  # 创建gps的表格项
                item_time = QTableWidgetItem(str(time))  # 创建time的表格项
                item_upDateDevice = QTableWidgetItem(str(upDateDevice))

                # 设置表格项居中对齐
                for item in [item_deviceId, item_lorastr, item_time, item_upDateDevice]:
                    item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)

                # 将表格项添加到表格中
                self.table.setItem(row_idx, 0, item_deviceId)  # 设置第0列
                self.table.setItem(row_idx, 1, item_lorastr)
                self.table.setItem(row_idx, 2, item_time)
                self.table.setItem(row_idx, 3, item_upDateDevice)

                # print(f"主键: {row.primary_key}, 属性列: {row.attribute_columns}")

        except Exception as e:
            print(f"查询失败: {e}")