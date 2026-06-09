from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QTabWidget, QLabel)
from PyQt6.QtGui import QFont, QPalette, QColor, QResizeEvent
from PyQt6.QtCore import Qt, pyqtSignal, QSize

from tablestore import Direction, INF_MAX, INF_MIN

from crowui.submenu.button_panel import ButtonPanel
from crowui.submenu.tab_allcorw_list import TobAllcorwList
from crowui.submenu.tab_alldivice_list import TabAllDiviceList
from crowui.submenu.tab_refrish_info import TobRefrishInfo
from config import settings


class RightTabMenu(QWidget):
    # 定义信号，用于通知刷新数据
    refresh_data_signal = pyqtSignal()

    # 全局变量，用于存储设备表和牛羊表数据
    device_table_data = None
    cowsheep_table_data = None

    def __init__(self, parent=None, ots_client=None):
        # 如果传入的是 OTSClient，则不将其作为 parent
        if parent is not None and not isinstance(parent, QWidget):
            ots_client = parent
            parent = None
        super().__init__(parent)
        self.ots_client = ots_client

        # 初始化后加载数据表
        self.load_table_data()

        # 设置背景颜色
        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(QPalette.ColorRole.Window, QColor(245, 245, 250))
        self.setPalette(palette)
        # 主布局
        layout = QVBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)
        layout.setSpacing(5)

        # 创建选项卡控件
        self.tab_widget = QTabWidget()
        self.tab_widget.setStyleSheet("""
                        QTabWidget::pane {
                            border: 1px solid #cccccc;
                            background: white;
                            border-radius: 5px;
                        }
                        QTabBar::tab {
                            background: #e0e0e0;
                            padding: 10px 20px;
                            margin-right: 3px;
                            border-top-left-radius: 5px;
                            border-top-right-radius: 5px;
                            font-size: 12px;
                            font-weight: bold;
                        }
                        QTabBar::tab:selected {
                            background: white;
                            border-bottom: 3px solid #0078d7;
                            color: #0078d7;
                        }
                        QTabBar::tab:hover:!selected {
                            background: #f0f0f0;
                        }
                    """)
        layout.addWidget(self.tab_widget)
        # 初始化TableStore客户端


        self.create_cow_status_tab()  # 设备列表
        self.create_cowsheep_status_tab()  # 牛羊列表
        self.create_latest_data_tab()  # 最新数据
        self.create_cruise_view_tab()  # 巡航画面

        # 连接页签切换信号，切换时触发表格自适应列宽
        self.tab_widget.currentChanged.connect(self.on_tab_changed)




    def on_tab_changed(self, index):
        """页签切换时，对当前页签中包含表格的组件触发 resizeEvent 以自适应列宽"""
        widget = self.tab_widget.widget(index)
        if widget is None:
            return
        # 遍历页签内的子组件，找到带有 table 属性的面板
        for child in widget.children():
            if hasattr(child, 'table') and child.table is not None:
                # 发送一个 resizeEvent 触发表格列宽重新计算
                old_size = child.size()
                event = QResizeEvent(old_size, old_size)
                child.resizeEvent(event)

    def create_latest_data_tab(self):
        """创建最新数据选项卡"""
        tab_widget = QWidget()
        tab_layout = QVBoxLayout(tab_widget)
        tab_layout.setContentsMargins(10, 10, 10, 10)


        panel = TobRefrishInfo(client=self.ots_client)
        tab_layout.addWidget(panel)

        self.tab_widget.addTab(tab_widget, "最新数据")

    def create_cow_status_tab(self):
        """创建设备列表选项卡"""
        tab_widget = QWidget()
        tab_layout = QVBoxLayout(tab_widget)
        tab_layout.setContentsMargins(10, 10, 10, 10)



        panel = TabAllDiviceList(client=self.ots_client)
        tab_layout.addWidget(panel)

        self.tab_widget.addTab(tab_widget, "设备动态")
        
    def create_cowsheep_status_tab(self):
        """创建牛羊列表选项卡"""
        tab_widget = QWidget()
        tab_layout = QVBoxLayout(tab_widget)
        tab_layout.setContentsMargins(10, 10, 10, 10)

        # 创建牛羊列表组件
        panel = TobAllcorwList(client=self.ots_client)
        tab_layout.addWidget(panel)

        self.tab_widget.addTab(tab_widget, "牛羊列表")

    def create_cruise_view_tab(self):
        """创建巡航画面选项卡"""
        tab_widget = QWidget()
        tab_layout = QVBoxLayout(tab_widget)
        tab_layout.setContentsMargins(10, 10, 10, 10)

        # 添加标题
        title = QLabel("巡航画面")
        title.setFont(QFont("Microsoft YaHei", 14, QFont.Weight.Bold))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        tab_layout.addWidget(title)

        panel= ButtonPanel()
        tab_layout.addWidget(panel)

        self.tab_widget.addTab(tab_widget, "巡航画面")

    def load_table_data(self):
        """加载设备表和牛羊表数据到全局变量中"""
        try:
            # 加载设备表数据
            print(f"加载设备表数据: {settings.DEVICETTABLE_NAME}")
            columns_to_get = ['gps', 'lorastr', 'time', 'upDateDevice']
            # 2. 定义主键范围：覆盖全表
            # 起始主键 (inclusive_start_primary_key) 设为最大值，表示从主键最大的行开始
            inclusive_start_primary_key = [('deviceId', INF_MAX)]
            # 结束主键 (exclusive_end_primary_key) 设为最小值，表示扫描到主键最小的行为止
            exclusive_end_primary_key = [('deviceId', INF_MIN)]

            # 3. 执行范围查询，direction=BACKWARD 为倒序读取
            # 使用时间戳排序获取最新记录
            consumed, next_start_primary_key, self.device_table_data, next_token = self.ots_client.get_range(
                table_name=settings.DEVICETTABLE_NAME,
                direction=Direction.BACKWARD,  # 关键参数：反向查询
                inclusive_start_primary_key=inclusive_start_primary_key,
                exclusive_end_primary_key=exclusive_end_primary_key,
                columns_to_get=columns_to_get,  # 可选，指定要获取的列
                limit=100  # 限制返回的行数，获取最新100条记录
            )
            print(f"设备表数据加载完成，共 {len(self.device_table_data)} 条记录")

            # 加载牛羊表数据
            print(f"加载牛羊表数据: {settings.COWSHEEP_TABLE_NAME}")
            # 使用类似的方法加载牛羊表数据
            columns_to_get = ['avatar', 'birthday', 'gender', 'rename']
            inclusive_start_primary_key = [('cowsheep_id', INF_MAX)]
            exclusive_end_primary_key = [('cowsheep_id', INF_MIN)]

            consumed, next_start_primary_key, self.cowsheep_table_data, next_token = self.ots_client.get_range(
                table_name=settings.COWSHEEP_TABLE_NAME,
                direction=Direction.BACKWARD,
                inclusive_start_primary_key=inclusive_start_primary_key,
                exclusive_end_primary_key=exclusive_end_primary_key,
                columns_to_get=columns_to_get,
                limit=100
            )
            print(f"牛羊表数据加载完成，共 {len(self.cowsheep_table_data)} 条记录")

        except Exception as e:
            print(f"加载数据表失败: {str(e)}")

    def refresh_table_data(self):
        """刷新设备表和牛羊表数据"""
        print("刷新数据表...")
        self.load_table_data()
