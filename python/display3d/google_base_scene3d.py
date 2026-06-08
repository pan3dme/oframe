
import math

from PyQt6.QtWidgets import QWidget, QVBoxLayout

from PyQt6.QtOpenGLWidgets import QOpenGLWidget
from PyQt6.QtCore import Qt


from pan3d.event.GameMouseManager import GameMouseManagerGetInstance

from pan3d.event.InteractiveEvent import InteractiveEvent
from pan3d.core.Vector2D import Vector2D
from pan3d.core.Vector3D import Vector3D
from pan3d.core.TimeUtil import TimeUtilInter
from pan3d.scene3D.Scene3D import Scene3D


class GoogleBaseScene3D(QWidget):
    """3D三角形渲染组件"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)
        self.layout.setSpacing(0)

        # 创建 OpenGL 渲染区域，跟随父组件拉伸
        self.gl_widget = QOpenGLWidget()
        self.gl_widget.setFocusPolicy(Qt.FocusPolicy.StrongFocus)  # 设置焦点策略，确保能接收键盘事件

        # 添加到布局，自动拉伸填充高度
        self.layout.addWidget(self.gl_widget)
        # 设置 OpenGL 渲染方法
        self.gl_widget.initializeGL = self.initializeGL
        self.gl_widget.resizeGL = self.resizeGL
        self.gl_widget.paintGL = self.paintGL


        self.scene3D = None
        self.lastDownTm = None



    def initData(self):
        """绑定鼠标交互事件（按下、抬起、移动、滚轮）到空白舞台"""
        uiBlankStage = GameMouseManagerGetInstance.uiBlankStage  # 获取空白舞台实例
        uiBlankStage.addEventListener(InteractiveEvent.Down, self.uiStageDown, self)  # 鼠标按下
        uiBlankStage.addEventListener(InteractiveEvent.Up, self.uiStageUp, self)  # 鼠标抬起
        uiBlankStage.addEventListener(InteractiveEvent.Move, self.uiStageMove, self)  # 鼠标移动
        uiBlankStage.addEventListener(InteractiveEvent.WheelEvent, self.uiStageWheel, self)  # 鼠标滚轮
    def uiStageWheel(self, evt=InteractiveEvent()):
        if self.scene3D is None:
            return
        """鼠标滚轮事件：调整摄像机距离"""
        # 判断鼠标是否在当前widget范围内
        global_pos = self.mapToGlobal(self.pos())
        local_x = evt.x - global_pos.x()
        local_y = evt.y - global_pos.y()

        if not (0 <= local_x <= self.width() and 0 <= local_y <= self.height()):
            return
        camera3D = self.scene3D.camera3D
        camera3D.camDis = camera3D.camDis + evt.wheelNum * 10  # 根据滚轮方向调整摄像机距离

    def uiStageDown(self, evt=InteractiveEvent()):
        if self.scene3D is None:
            return
        """鼠标按下事件：记录按下状态、旋转角度和位置，并检测双击"""
        # 判断鼠标是否在当前widget范围内
        global_pos = self.mapToGlobal(self.pos())
        local_x = evt.x - global_pos.x()
        local_y = evt.y - global_pos.y()
        if not (0 <= local_x <= self.width() and 0 <= local_y <= self.height()):
            return

        camera3D = self.scene3D.camera3D

        # 中键按下：记录旋转状态
        if evt.button and hasattr(evt.button, 'name') and evt.button.name == 'middle':
            camera3D.isMiddleDown = True
            camera3D.downCamRoV2.x = camera3D.rotationX  # 记录按下时的水平旋转角度
            camera3D.downCamRoV2.y = camera3D.rotationY  # 记录按下时的垂直旋转角度
            camera3D.downPos = Vector2D(evt.x, evt.y)  # 记录按下时的鼠标坐标
            return

        # 左键按下：记录拖动状态
        camera3D.isLastDonw = True
        camera3D.middleDownPos = Vector2D(evt.x, evt.y)
        camera3D.middleDownLocaAtPos = Vector3D(
            camera3D.locaAtPos.x if camera3D.locaAtPos else 0,
            camera3D.locaAtPos.y if camera3D.locaAtPos else 0,
            camera3D.locaAtPos.z if camera3D.locaAtPos else 0
        )

        # 双击检测：两次按下间隔小于300ms则触发双击事件
        if (TimeUtilInter.getTimer() - self.lastDownTm) < 300:
            self.scene3D.doubelClikEvent()

        self.lastDownTm = TimeUtilInter.getTimer()  # 更新上次按下时间

    def uiStageUp(self, evt=InteractiveEvent()):
        """鼠标抬起事件：取消按下状态"""
        if self.scene3D is None:
            return
        camera3D = self.scene3D.camera3D
        camera3D.isLastDonw = False  # 取消鼠标按下状态
        camera3D.isMiddleDown = False  # 取消中键按下状态

    def uiStageMove(self, evt=InteractiveEvent()):
        if self.scene3D is None:
            return
        """鼠标移动事件：左键拖拽平移场景，中键拖拽旋转摄像机"""
        camera3D = self.scene3D.camera3D

        # 中键拖动：旋转摄像机
        if camera3D.isMiddleDown:
            tx = evt.x - camera3D.downPos.x  # 水平偏移量
            ty = evt.y - camera3D.downPos.y  # 垂直偏移量

            camera3D.rotationX = max(-50, min(-35, camera3D.downCamRoV2.x - ty*0.2))
            camera3D.rotationY = camera3D.downCamRoV2.y - tx  # 水平拖拽控制偏航角
            return

        # 左键拖动：平移场景（通过射线投射到Y=0平面计算偏移）
        if camera3D.isLastDonw:
            # 将鼠标当前位置和按下位置都投射到Y=0平面
            global_pos = self.mapToGlobal(self.pos())
            local_x = evt.x - global_pos.x()
            local_y = evt.y - global_pos.y()
            down_local_x = camera3D.middleDownPos.x - global_pos.x()
            down_local_y = camera3D.middleDownPos.y - global_pos.y()

            hit_current = camera3D.screenToWorldOnPlane(local_x, local_y, 0.0)
            hit_down = camera3D.screenToWorldOnPlane(down_local_x, down_local_y, 0.0)

            if hit_current is not None and hit_down is not None:
                # 拖动场景 = 世界坐标差取反（鼠标向右拖，场景向左移）
                offset_x = hit_down.x - hit_current.x
                offset_z = hit_down.z - hit_current.z

                if camera3D.locaAtPos is None:
                    camera3D.locaAtPos = Vector3D(0, 0, 0)

                camera3D.locaAtPos.x = camera3D.middleDownLocaAtPos.x - offset_x*50
                camera3D.locaAtPos.y = 0
                camera3D.locaAtPos.z = camera3D.middleDownLocaAtPos.z - offset_z*50

    def initializeGL(self):
        """初始化OpenGL设置"""
        self.initData()
        self.scene3D = Scene3D()
        self.scene3D.camera3D.camDis = -200
        self.lastDownTm = TimeUtilInter.getTimer()  # 记录上次鼠标按下时间，用于双击检测



    def resizeGL(self, width, height):
        self.scene3D.camera3D.widWidth = width
        self.scene3D.camera3D.widHeight = height


    def paintGL(self):
        self.scene3D.update()






