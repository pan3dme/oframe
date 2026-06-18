"""BasePicWidget - 加载切片图片拼接显示，支持滑动浏览和GPS标记"""
import os
from PyQt6.QtCore import Qt, QTimer, QSize
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QScrollArea, QLabel, QSizePolicy, QPushButton, QHBoxLayout
from PyQt6.QtGui import QPixmap, QPainter, QColor, QPen, QFont, QIcon, QPainterPath
from tablestore import INF_MIN, INF_MAX,Direction

from config import settings
class NoWheelScrollArea(QScrollArea):
    """自定义ScrollArea，将滚轮事件转发给父widget而非自己处理滚动"""
    def wheelEvent(self, event):
        # 不自己处理，转发给父widget
        parent = self.parent()
        if parent:
            parent.wheelEvent(event)
        event.ignore()


class GoogleMap2DWidget(QWidget):
    """加载res目录下的切片图片拼接成大图，在300x300区域内支持上下左右滑动和GPS标记"""

    TILE_ROWS = 10
    TILE_COLS = 10

    def __init__(self, parent=None, ots_client=None):
        # 如果传入的是 OTSClient，则不将其作为 parent
        if parent is not None and not isinstance(parent, QWidget):
            ots_client = parent
            parent = None
        super().__init__(parent)
        self.ots_client = ots_client
        self.setSizePolicy(
            QSizePolicy.Policy.Expanding,
            QSizePolicy.Policy.Expanding
        )

        # 滚动区域
        self.scroll_area = NoWheelScrollArea(self)
        self.scroll_area.setWidgetResizable(False)
        self.scroll_area.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.scroll_area.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.scroll_area.setStyleSheet("QScrollArea { border: none; background-color: #1a1a2e; }")

        # 图片标签
        self.image_label = QLabel()
        self.image_label.setAlignment(Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignLeft)
        self.image_label.setStyleSheet("background-color: #1a1a2e;")
        self.scroll_area.setWidget(self.image_label)

        # 布局
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        layout.addWidget(self.scroll_area)

        # 拖拽滑动相关
        self._dragging = False
        self._last_pos = None

        # 缩放相关
        self._scale = 1.0
        self._min_scale = 0.1
        self._max_scale = 5.0
        self._original_pixmap = None

        # GPS坐标边界（左上角和右下角）
        self._gps_bounds = settings.gps_bounds
        
        # GPS标记点（像素坐标和文本）
        self._gps_markers = []  # [(x, y, text), ...]
        
        # GPS路线（像素坐标点序列）
        self._gps_routes = []  # [[(x, y), ...], ...]
        
        # 设备位置标记（独立于GPS标记）
        self._device_markers = []  # [(deviceId, gps, time, isGray, loop, visible), ...]

        # 闪烁计时器
        self._blink_timer = QTimer(self)
        self._blink_timer.timeout.connect(self._update_blink_state)
        self._blink_timer.setInterval(300)  # 300毫秒间隔
        self._blink_state = False  # 闪烁状态

        # 加载切片图片
        self._load_tiles()

    def _load_tiles(self):
        """加载res目录下的切片图片并拼接"""
        res_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "res")

        # 先加载第一张获取单块尺寸
        first_path = os.path.join(res_dir, "tile_0_0.png")
        first_pixmap = QPixmap(first_path)
        if first_pixmap.isNull():
            return

        tile_w = first_pixmap.width()
        tile_h = first_pixmap.height()

        # 创建大图画布
        big_w = tile_w * self.TILE_COLS
        big_h = tile_h * self.TILE_ROWS
        big_pixmap = QPixmap(big_w, big_h)
        big_pixmap.fill(Qt.GlobalColor.transparent)

        painter = QPainter(big_pixmap)

        for r in range(self.TILE_ROWS):
            for c in range(self.TILE_COLS):
                path = os.path.join(res_dir, f"tile_{r}_{c}.png")
                tile = QPixmap(path)
                if not tile.isNull():
                    painter.drawPixmap(c * tile_w, r * tile_h, tile)

        painter.end()

        self._original_pixmap = big_pixmap
        
        # 延时100毫秒再显示并打印
        QTimer.singleShot(100, self._delayed_display)
        
    def _delayed_display(self):
        """延时显示图片并打印"""
        self._update_display()
        self.center_on_gps((26.525126, 109.394103))
        print("图片加载完成并已显示")


    def mousePressEvent(self, event):
        if  not self.isCanUseMouseEvent():
            return
        """鼠标按下，开始拖拽或检查是否点击了标记点"""
        if event.button() in (Qt.MouseButton.LeftButton, Qt.MouseButton.MiddleButton):
            # 获取鼠标在scroll_area中的位置
            mouse_pos = event.position().toPoint()
            
            # 获取滚动条的位置
            h_bar = self.scroll_area.horizontalScrollBar()
            v_bar = self.scroll_area.verticalScrollBar()
            
            # 检查是否点击了标记点（仅左键）
            clicked_marker = False
            if event.button() == Qt.MouseButton.LeftButton:
                for x, y, text in self._gps_markers:
                    # 计算标记点在当前缩放下的位置
                    scaled_x = int(x * self._scale)
                    scaled_y = int(y * self._scale)
                    
                    # 计算标记点在scroll_area中的位置
                    marker_x = scaled_x - h_bar.value()
                    marker_y = scaled_y - v_bar.value()
                    
                    # 计算鼠标与标记点的距离
                    distance = ((mouse_pos.x() - marker_x) ** 2 + (mouse_pos.y() - marker_y) ** 2) ** 0.5
                    
                    # 如果距离小于标记点半径，认为点击了标记点
                    if distance <= 20:  # 20像素的点击区域
                        # 将像素坐标转换为GPS坐标
                        gps_coord = self._pixel_to_gps((x, y))
                        if gps_coord:
                            print(f"标记点GPS坐标: 纬度={gps_coord[0]:.6f}, 经度={gps_coord[1]:.6f}, 文本={text}")
                        clicked_marker = True
                        break
            
            # 如果没有点击标记点，开始拖拽
            if not clicked_marker:
                self._dragging = True
                self._last_pos = mouse_pos
                self.setCursor(Qt.CursorShape.ClosedHandCursor)

    def mouseMoveEvent(self, event):
        if  not self.isCanUseMouseEvent():
            return
        """鼠标移动，拖拽滑动"""
        if self._dragging and self._last_pos:
            delta = event.position().toPoint() - self._last_pos
            self._last_pos = event.position().toPoint()
            h_bar = self.scroll_area.horizontalScrollBar()
            v_bar = self.scroll_area.verticalScrollBar()
            h_bar.setValue(h_bar.value() - delta.x())
            v_bar.setValue(v_bar.value() - delta.y())
            
            # 打印当前地图中心的GPS坐标
            self._print_center_gps()

    def mouseReleaseEvent(self, event):
        """鼠标释放，停止拖拽"""
        if event.button() in (Qt.MouseButton.LeftButton, Qt.MouseButton.MiddleButton):
            self._dragging = False
            self._last_pos = None
            self.setCursor(Qt.CursorShape.ArrowCursor)

    def isCanUseMouseEvent(self):
        if settings.current_mode=='small_2d':
            return False
        else:
            return True

    def wheelEvent(self, event):
        if  not self.isCanUseMouseEvent():
            return
        """鼠标滚轮缩放，以鼠标位置为中心"""
        if self._original_pixmap is None:
            return

        # 计算缩放因子
        zoom_factor = 1.15
        if event.angleDelta().y() > 0:
            new_scale = self._scale * zoom_factor
        else:
            new_scale = self._scale / zoom_factor

        # 计算最小缩放比例：图片至少填满容器
        viewport_w = self.scroll_area.viewport().width()
        viewport_h = self.scroll_area.viewport().height()
        if self._original_pixmap:
            fit_scale_w = viewport_w / self._original_pixmap.width()
            fit_scale_h = viewport_h / self._original_pixmap.height()
            min_fit_scale = max(fit_scale_w, fit_scale_h)
        else:
            min_fit_scale = self._min_scale

        # 限制缩放范围
        new_scale = max(min_fit_scale, min(self._max_scale, new_scale))
        if new_scale == self._scale:
            return

        # 获取鼠标在 scroll_area 中的位置
        mouse_pos = event.position().toPoint()
        h_bar = self.scroll_area.horizontalScrollBar()
        v_bar = self.scroll_area.verticalScrollBar()

        # 鼠标相对于图片内容的位置比例
        old_x_ratio = (h_bar.value() + mouse_pos.x()) / self.image_label.width()
        old_y_ratio = (v_bar.value() + mouse_pos.y()) / self.image_label.height()

        # 更新缩放
        self._scale = new_scale
        self._update_display()

        # 以鼠标位置为中心调整滚动条
        new_w = int(self._original_pixmap.width() * self._scale)
        new_h = int(self._original_pixmap.height() * self._scale)
        h_bar.setValue(int(old_x_ratio * new_w - mouse_pos.x()))
        v_bar.setValue(int(old_y_ratio * new_h - mouse_pos.y()))
        
        # 打印当前地图中心的GPS坐标
        self._print_center_gps()



    def add_gps_marker(self, gps_coord, text=""):
        """在指定GPS坐标处添加标记点
        
        Args:
            gps_coord: GPS坐标 (latitude, longitude)
            text: 标记点文本说明（不超过10个字节）
        """
        if self._gps_bounds['top_left'] is None or self._gps_bounds['bottom_right'] is None:
            print("警告：请先设置GPS坐标边界")
            return
        
        if self._original_pixmap is None:
            print("警告：图片未加载")
            return
        
        # 限制文本长度不超过10个字节
        if len(text.encode('utf-8')) > 10:
            text = text.encode('utf-8')[:10].decode('utf-8', errors='ignore')

        # 将GPS坐标转换为像素坐标
        pixel_coord = self._gps_to_pixel(gps_coord)
        if pixel_coord:
            x, y = pixel_coord
            self._gps_markers.append((x, y, text))
            self._update_display()

    def add_gps_route(self, gps_coords):
        """添加GPS路线

        Args:
            gps_coords: GPS坐标列表 [(latitude, longitude), ...]
        """
        if self._gps_bounds['top_left'] is None or self._gps_bounds['bottom_right'] is None:
            # print("警告：请先设置GPS坐标边界")
            return

        if self._original_pixmap is None:
            # print("警告：图片未加载")
            return

        # 将所有GPS坐标转换为像素坐标
        pixel_coords = []
        for coord in gps_coords:
            pixel_coord = self._gps_to_pixel(coord)
            if pixel_coord:
                pixel_coords.append(pixel_coord)

        # 如果有有效的像素坐标，添加到路线列表
        if pixel_coords:
            self._gps_routes.append(pixel_coords)
            self._update_display()

    def center_on_gps(self, gps_coord):
        """将地图移动到以指定GPS坐标为中心

        Args:
            gps_coord: GPS坐标 (latitude, longitude)
        """
        if self._gps_bounds['top_left'] is None or self._gps_bounds['bottom_right'] is None:
            # print("警告：请先设置GPS坐标边界")
            return

        if self._original_pixmap is None:
            # print("警告：图片未加载")
            return

        # 将GPS坐标转换为像素坐标
        pixel_coord = self._gps_to_pixel(gps_coord)
        if pixel_coord is None:
            # print("警告：GPS坐标转换失败")
            return

        x, y = pixel_coord

        # 获取滚动条
        h_bar = self.scroll_area.horizontalScrollBar()
        v_bar = self.scroll_area.verticalScrollBar()

        # 获取可视区域尺寸
        viewport_width = self.scroll_area.viewport().width()
        viewport_height = self.scroll_area.viewport().height()

        # 计算缩放后的图片尺寸
        scaled_width = int(self._original_pixmap.width() * self._scale)
        scaled_height = int(self._original_pixmap.height() * self._scale)

        # 计算目标滚动条位置（使GPS坐标位于可视区域中心）
        target_h_value = int(x * self._scale - viewport_width / 2)
        target_v_value = int(y * self._scale - viewport_height / 2)

        # 确保滚动条位置在有效范围内（不溢出）
        max_h_value = max(0, scaled_width - viewport_width)
        max_v_value = max(0, scaled_height - viewport_height)

        target_h_value = max(0, min(target_h_value, max_h_value))
        target_v_value = max(0, min(target_v_value, max_v_value))

        # 设置滚动条位置
        h_bar.setValue(target_h_value)
        v_bar.setValue(target_v_value)

    def _gps_to_pixel(self, gps_coord):
        """将GPS坐标转换为图片上的像素坐标
        
        Args:
            gps_coord: GPS坐标 (latitude, longitude)
            
        Returns:
            像素坐标 (x, y) 或 None（如果转换失败）
        """
        if self._gps_bounds['top_left'] is None or self._gps_bounds['bottom_right'] is None:
            return None
        
        lat, lon = gps_coord
        top_left_lat, top_left_lon = self._gps_bounds['top_left']
        bottom_right_lat, bottom_right_lon = self._gps_bounds['bottom_right']
        
        # 计算GPS坐标在边界中的比例
        lat_ratio = (top_left_lat - lat) / (top_left_lat - bottom_right_lat)
        lon_ratio = (lon - top_left_lon) / (bottom_right_lon - top_left_lon)
        
        # 确保比例在0-1范围内
        lat_ratio = max(0, min(1, lat_ratio))
        lon_ratio = max(0, min(1, lon_ratio))
        
        # 转换为像素坐标
        x = int(lon_ratio * self._original_pixmap.width())
        y = int(lat_ratio * self._original_pixmap.height())
        
        return x, y

    def _pixel_to_gps(self, pixel_coord):
        """将图片上的像素坐标转换为GPS坐标

        Args:
            pixel_coord: 像素坐标 (x, y)

        Returns:
            GPS坐标 (latitude, longitude) 或 None（如果转换失败）
        """
        if self._gps_bounds['top_left'] is None or self._gps_bounds['bottom_right'] is None:
            return None

        x, y = pixel_coord
        top_left_lat, top_left_lon = self._gps_bounds['top_left']
        bottom_right_lat, bottom_right_lon = self._gps_bounds['bottom_right']

        # 计算像素坐标在图片中的比例
        x_ratio = x / self._original_pixmap.width()
        y_ratio = y / self._original_pixmap.height()

        # 确保比例在0-1范围内
        x_ratio = max(0, min(1, x_ratio))
        y_ratio = max(0, min(1, y_ratio))

        # 转换为GPS坐标
        lat = top_left_lat - y_ratio * (top_left_lat - bottom_right_lat)
        lon = top_left_lon + x_ratio * (bottom_right_lon - top_left_lon)

        return lat, lon

    def _print_center_gps(self):
        """打印当前地图中心的GPS坐标"""
        if self._original_pixmap is None:
            return

        # 获取滚动条位置
        h_bar = self.scroll_area.horizontalScrollBar()
        v_bar = self.scroll_area.verticalScrollBar()

        # 获取可视区域尺寸
        viewport_width = self.scroll_area.viewport().width()
        viewport_height = self.scroll_area.viewport().height()

        # 计算可视区域中心的像素坐标（相对于图片左上角）
        center_x = h_bar.value() + viewport_width / 2
        center_y = v_bar.value() + viewport_height / 2

        # 将像素坐标转换为原始图片的坐标（考虑缩放）
        original_x = center_x / self._scale
        original_y = center_y / self._scale

        # 将像素坐标转换为GPS坐标
        gps_coord = self._pixel_to_gps((original_x, original_y))
        if gps_coord:
            lat, lon = gps_coord
            # print(f"地图中心GPS坐标: 纬度={lat:.6f}, 经度={lon:.6f}")
            self.receive_map_move_gps(lat,lon)


    def _update_display(self):
        """更新显示，包括图片和标记点"""
        if self._original_pixmap is None:
            return
        
        # 计算缩放后的尺寸
        new_w = int(self._original_pixmap.width() * self._scale)
        new_h = int(self._original_pixmap.height() * self._scale)
        
        # 缩放图片
        scaled_pixmap = self._original_pixmap.scaled(
            new_w, new_h,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation
        )
        
        # 如果有标记点、路线或设备位置，绘制在图片上
        if self._gps_markers or self._gps_routes or self._device_markers:
            # 创建可绘制的pixmap副本
            display_pixmap = QPixmap(scaled_pixmap)
            painter = QPainter(display_pixmap)
            
            # 设置红色标记
            marker_color = QColor(255, 0, 0)
            pen = QPen(marker_color)
            pen.setWidth(1)  # 边框宽度设为1
            painter.setPen(pen)
            painter.setBrush(marker_color)  # 设置填充颜色为红色
            
            # 标记点半径（固定大小，不随缩放变化）
            marker_radius = 5

            # 绘制所有路线
            if self._gps_routes:
                # 设置绿色画笔
                route_color = QColor(0, 255, 0)
                route_pen = QPen(route_color)
                route_pen.setWidth(2)  # 路线宽度
                painter.setPen(route_pen)

                # 绘制每条路线
                for route in self._gps_routes:
                    if len(route) >= 2:
                        # 将像素坐标转换为缩放后的坐标
                        scaled_route = [(int(x * self._scale), int(y * self._scale)) for x, y in route]
                        # 绘制路线
                        for i in range(len(scaled_route) - 1):
                            painter.drawLine(
                                scaled_route[i][0], scaled_route[i][1],
                                scaled_route[i+1][0], scaled_route[i+1][1]
                            )
            
            # 绘制所有标记点
            for x, y, text in self._gps_markers:
                # 根据当前缩放比例计算标记点位置
                scaled_x = int(x * self._scale)
                scaled_y = int(y * self._scale)
                
                # 先绘制红色描边圆点（稍大一些）
                red_pen = QPen(QColor(255, 0, 0))
                red_pen.setWidth(2)
                painter.setPen(red_pen)
                painter.setBrush(QColor(255, 0, 0))
                painter.drawEllipse(
                    scaled_x - marker_radius - 1,
                    scaled_y - marker_radius - 1,
                    marker_radius * 2 + 2,
                    marker_radius * 2 + 2
                )

                # 绘制红色圆点
                painter.drawEllipse(
                    scaled_x - marker_radius, 
                    scaled_y - marker_radius, 
                    marker_radius * 2, 
                    marker_radius * 2
                )
                
                # 如果有文本，在红点下方绘制
                if text:
                    # 设置字体（固定大小，不随缩放变化）
                    font = QFont("Arial", 10)
                    painter.setFont(font)
                    
                    # 设置文本颜色为白色
                    painter.setPen(QColor(255, 255, 255))
                    
                    # 计算文本位置（红点下方，居中对齐）
                    text_rect = painter.fontMetrics().boundingRect(text)
                    text_x = scaled_x - text_rect.width() // 2
                    text_y = scaled_y + marker_radius * 2 + 12
                    
                    # 绘制文本
                    painter.drawText(text_x, text_y, text)
                    
                    # 恢复红色画笔
                    painter.setPen(marker_color)
            
            # 绘制所有设备位置标记
            for device_id, gps, time_str, is_gray, loop, visible in self._device_markers:
                # 如果不可见，跳过
                if not visible:
                    continue

                # 如果是闪烁模式且当前闪烁状态为False，跳过
                if loop and not self._blink_state:
                    continue

                # 将GPS坐标转换为像素坐标
                pixel_coord = self._gps_to_pixel(gps)
                if not pixel_coord:
                    continue

                x, y = pixel_coord
                scaled_x = int(x * self._scale)
                scaled_y = int(y * self._scale)

                # 根据isGray决定颜色（黄色或灰色）
                if is_gray:
                    marker_color = QColor(128, 128, 128)  # 灰色
                else:
                    marker_color = QColor(255, 255, 0)  # 黄色

                # 绘制黄色/灰色圆点
                painter.setPen(QPen(marker_color))
                painter.setBrush(marker_color)
                painter.drawEllipse(
                    scaled_x - marker_radius,
                    scaled_y - marker_radius,
                    marker_radius * 2,
                    marker_radius * 2
                )

                # 绘制设备ID和时间文本
                font = QFont("Arial", 9)
                painter.setFont(font)
                painter.setPen(QColor(255, 255, 255))

                # 计算文本位置
                text1 = device_id

                # 处理时间字符串，只保留月日和时间（去掉年份）
                try:
                    # 假设时间格式为 "YYYY/M/D HH:MM:SS"
                    parts = time_str.split()
                    if len(parts) >= 2:
                        date_part = parts[0]
                        time_part = parts[1]
                        # 去掉年份，只保留月日
                        date_parts = date_part.split('/')
                        if len(date_parts) >= 3:
                            text2 = f"{date_parts[1]}-{date_parts[2]} {time_part}"
                        else:
                            text2 = time_str
                    else:
                        text2 = time_str
                except:
                    text2 = time_str

                # 绘制第一行文本（设备ID）
                text1_rect = painter.fontMetrics().boundingRect(text1)
                text1_x = scaled_x - text1_rect.width() // 2
                text1_y = scaled_y + marker_radius * 2 + 12
                painter.drawText(text1_x, text1_y, text1)

                # 绘制第二行文本（时间）
                text2_rect = painter.fontMetrics().boundingRect(text2)
                text2_x = scaled_x - text2_rect.width() // 2
                text2_y = text1_y + 14
                painter.drawText(text2_x, text2_y, text2)

            painter.end()
            self.image_label.setPixmap(display_pixmap)
            self.image_label.setFixedSize(new_w, new_h)
        else:
            self.image_label.setPixmap(scaled_pixmap)
            self.image_label.setFixedSize(new_w, new_h)
        
        self.image_label.setFixedSize(new_w, new_h)




    def receive_device(self, device_id, gps, time_str):
        pass
    def clear_device(self ):
        pass
    def receive_map_move_gps(self, lat, lon):
        pass
    def receive_load(self, value):
        pass
    def receive_place(self, gps_coord, text=""):
        pass
    def clear_place(self):
        pass

    def change_map_gps(self,latitude, longitude):
        self.center_on_gps((latitude, longitude))
        pass

    def _update_blink_state(self):
        """更新闪烁状态"""
        self._blink_state = not self._blink_state
        self._update_display()

    def add_device_marker(self, device_id, gps, time_str, is_gray=False, loop=False):
        """添加设备位置标记

        Args:
            device_id: 设备ID
            gps: GPS坐标 (latitude, longitude)
            time_str: 时间字符串
            is_gray: 是否离线（灰色）
            loop: 是否闪烁
        """
        # 添加设备标记
        self._device_markers.append((device_id, gps, time_str, is_gray, loop, True))

        # 如果有闪烁标记，启动计时器
        if loop and not self._blink_timer.isActive():
            self._blink_timer.start()

        self._update_display()
