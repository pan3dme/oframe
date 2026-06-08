"""mainTri.py - 基于 Pygame + OpenGL 的3D场景交互应用入口"""

import pygame
 
from OpenGL import GL

from pan3d.event.GameMouseManager import GameMouseManagerGetInstance
from pan3d.event.InteractiveEvent import InteractiveEvent

from pan3d.core.Vector2D import Vector2D
from pan3d.scene3D.Scene3D import Scene3D
from pan3d.core.TimeUtil import TimeUtilInter



class App:
    """3D场景应用主类，负责窗口初始化、事件绑定和主循环"""

    def __init__(self):
        """初始化 Pygame 窗口、OpenGL 上下文、3D场景及事件监听，并进入主循环"""
        pygame.init()
        pygame.display.set_mode((1024, 768), pygame.OPENGL | pygame.DOUBLEBUF)

        self.check_opengl_version()  # 检测OpenGL版本信息
        self.clock = pygame.time.Clock()  # 帧率控制时钟
        self.lastDownTm = TimeUtilInter.getTimer()  # 记录上次鼠标按下时间，用于双击检测
        self.scene3d = Scene3D()  # 创建3D场景
        self.initData()  # 初始化事件绑定
        self.mainLoop()  # 进入主循环

    def check_opengl_version(self):
        """检测并打印当前系统支持的 OpenGL 版本信息"""
        pygame.display.set_mode((800, 600), pygame.OPENGL | pygame.DOUBLEBUF)

        print(f"OpenGL version: {GL.glGetString(GL.GL_VERSION).decode()}")
        print(f"GLSL version: {GL.glGetString(GL.GL_SHADING_LANGUAGE_VERSION).decode()}")
        print(f"Renderer: {GL.glGetString(GL.GL_RENDERER).decode()}")
        print(f"Vendor: {GL.glGetString(GL.GL_VENDOR).decode()}")

    def initData(self):
        """绑定鼠标交互事件（按下、抬起、移动、滚轮）到空白舞台"""
        uiBlankStage = GameMouseManagerGetInstance.uiBlankStage  # 获取空白舞台实例
        uiBlankStage.addEventListener(InteractiveEvent.Down, self.uiStageDown, self)  # 鼠标按下
        uiBlankStage.addEventListener(InteractiveEvent.Up, self.uiStageUp, self)      # 鼠标抬起
        uiBlankStage.addEventListener(InteractiveEvent.Move, self.uiStageMove, self)  # 鼠标移动
        uiBlankStage.addEventListener(InteractiveEvent.WheelEvent, self.uiStageWheel, self)  # 鼠标滚轮

    def uiStageWheel(self, evt=InteractiveEvent()):
        """鼠标滚轮事件：调整摄像机距离"""
        camera3D = self.scene3d.camera3D
        camera3D.camDis = camera3D.camDis + evt.wheelNum * 10  # 根据滚轮方向调整摄像机距离

    def uiStageDown(self, evt=InteractiveEvent()):
        """鼠标按下事件：记录按下状态、旋转角度和位置，并检测双击"""
        camera3D = self.scene3d.camera3D
        camera3D.isLastDonw = True  # 标记鼠标处于按下状态
        camera3D.downCamRoV2.x = camera3D.rotationX  # 记录按下时的水平旋转角度
        camera3D.downCamRoV2.y = camera3D.rotationY  # 记录按下时的垂直旋转角度
        camera3D.downPos = Vector2D(evt.x, evt.y)     # 记录按下时的鼠标坐标

        # 双击检测：两次按下间隔小于300ms则触发双击事件
        if (TimeUtilInter.getTimer() - self.lastDownTm) < 300:
            self.scene3d.doubelClikEvent()

        self.lastDownTm = TimeUtilInter.getTimer()  # 更新上次按下时间

    def uiStageUp(self, evt=InteractiveEvent()):
        """鼠标抬起事件：取消按下状态"""
        camera3D = self.scene3d.camera3D
        camera3D.isLastDonw = False  # 取消鼠标按下状态

    def uiStageMove(self, evt=InteractiveEvent()):
        """鼠标移动事件：按下状态下拖拽旋转摄像机"""
        camera3D = self.scene3d.camera3D
        if camera3D.isLastDonw:
            tx = evt.x - camera3D.downPos.x  # 水平偏移量
            ty = evt.y - camera3D.downPos.y  # 垂直偏移量
            camera3D.rotationX = camera3D.downCamRoV2.x - ty  # 垂直拖拽控制俯仰角
            camera3D.rotationY = camera3D.downCamRoV2.y - tx  # 水平拖拽控制偏航角

    def mainLoop(self):
        """主循环：处理事件、更新场景、刷新画面"""
        running = True
        while running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False

            self.scene3d.update()  # 更新3D场景
            pygame.display.flip()  # 刷新画面
            self.clock.tick(60)  # 限制帧率为60FPS

        self.quit()

    def quit(self):
        """退出应用，释放 Pygame 资源"""
        pygame.quit()


if __name__ == '__main__':
    myApp = App()
