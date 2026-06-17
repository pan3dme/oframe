from display3d.google_base_scene3d import GoogleBaseScene3D

from PyQt6.QtCore import QTimer


from display3d.area_map_display_sprite import AreaMapDisplay3DSprite
from display3d.sprite.bace_scale_ball_sprite import BaseScaleBallSprite3D
from display3d.sprite.base_ball_sprite import BaseBallSprite3D

from display3d.sprite.base_color_sprite import BaseColorSprite3D
from display3d.sprite.base_device_sprite import BaseDeviceSprite3D
from display3d.sprite.base_flag_sprite import BaseFlagSprite

from display3d.sprite.base_space_sprite import BaseSpaceSprite3D

from pan3d.core.Vector3D import Vector3D


from pan3d.core.TimeUtil import TimeUtilInter

from display3d.google_map_dae_model import GoogleMapDaeModel
from config import settings
from typing import Optional

from pan3d.event.InteractiveEvent import InteractiveEvent


class GoogleScene3D(GoogleBaseScene3D):
    """3D三角形渲染组件"""

    def __init__(self, parent=None):
        super().__init__(parent)


        # 创建布局
        self.willdelearr = []
        self.fouseCentenSprite: Optional[BaseColorSprite3D] = None


        self.google_map_model = None


        # 设置初始旋转状态
        self.is_rotating = True

        # 设置定时器用于更新旋转
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update_rotation)
        self.timer.start(16)  # 约60 FPS

        # 缓动动画相关变量
        self.animation_start_rotationY = 0
        self.animation_target_rotationY = 0
        self.animation_start_rotationX = 0
        self.animation_target_rotationX = 0
        self.animation_start_time = 0
        self.animation_duration = 300  # 300毫秒
        self.is_animating = False


    def initializeGL(self):
        super().initializeGL()
        self.fouseCentenSprite = BaseBallSprite3D(self.scene3D, (1, 1, 1))
        self.scene3D.addDisplay(self.fouseCentenSprite)
        self.fouseCentenSprite.visible = False
        self.loadDaeGoogleMap()



    def loadDaeGoogleMap(self):
        """加载Google Earth DAE模型"""
        self.google_map_model = GoogleMapDaeModel(self.scene3D)
        self.google_map_model.load_google_earth_model()

        baseFlagSprite = BaseFlagSprite(self.scene3D, text="你好世界", color=(1.0, 0.0, 0.0))
        baseFlagSprite.x = 101
        baseFlagSprite.y = 60
        baseFlagSprite.z = 28
        baseFlagSprite.scaleX = 2
        baseFlagSprite.scaleY = 2
        baseFlagSprite.scaleZ = 2
        baseFlagSprite.rotationY = 90
        self.scene3D.addDisplay(baseFlagSprite)

    def clear_all_place(self):
        result = [obj for obj in self.scene3D.displayList if isinstance(obj, BaseSpaceSprite3D)]
        for dis in result:
            self.scene3D.removeDisplay(dis)

        pass

    def clear_all_device(self):
        result = [obj for obj in self.scene3D.displayList if isinstance(obj, BaseDeviceSprite3D)]
        for dis in result:
            self.scene3D.removeDisplay(dis)
        pass

    def receive_device_to_scene3d(self, device_id, gps, time_str):
        vPos = settings.gps_to_world_pos(gps)

        self.gl_widget.makeCurrent()
        temp = BaseDeviceSprite3D(self.scene3D, device_id, (1, 1, 0))
        temp.x = vPos.x
        temp.y = self.getPosinMapHeightByVec4(vPos.x, vPos.z) + 5
        temp.z = vPos.z
        self.scene3D.addDisplay(temp)

        self.gl_widget.doneCurrent()
        self.gl_widget.update()
        pass

    def receive_place_to_scene3d(self, gps_coord, text=""):

        vPos = settings.gps_to_world_pos(gps_coord)
        self.gl_widget.makeCurrent()
        temp = BaseSpaceSprite3D(self.scene3D, text, (1, 0, 0))
        # temp = BaseScaleBallSprite3D(self.scene3D,  (1, 0, 0))
        temp.x = vPos.x
        temp.y = self.getPosinMapHeightByVec4(vPos.x, vPos.z)+3
        temp.z = vPos.z
        self.scene3D.addDisplay(temp)
        self.gl_widget.doneCurrent()
        self.gl_widget.update()

        pass

    def receive_load_to_scene(self, value):
        result = [obj for obj in self.scene3D.displayList if isinstance(obj, AreaMapDisplay3DSprite)]
        for dis in result:
            dis.receive_load_to_scene(value)

    def clear_all_load_line(self):
        result = [obj for obj in self.scene3D.displayList if isinstance(obj, AreaMapDisplay3DSprite)]
        for dis in result:
            dis.clear_all_load_line()

    def uiStageMove(self, evt=InteractiveEvent()):
        super().uiStageMove(evt)
        if self.scene3D is None:
            return
        camera3D = self.scene3D.camera3D
        if camera3D.isLastDonw:
            latitude, longitude=settings.world_pos_to_gps(Vector3D(-camera3D.locaAtPos.x,0,-camera3D.locaAtPos.z))
            self.change_map_gps( latitude, longitude)
            pass


        pass

    def change_map_gps(self, latitude, longitude):
        pass
    def receive_gps_coordinates(self, latitude, longitude):

        self.drawGpsPointTomap(latitude, longitude)
        topos = settings.gps_to_world_pos((latitude, longitude))

        self.scene3D.camera3D.locaAtPos = Vector3D(-topos.x, 0, -topos.z)

        # 启动缓动动画，将 rotationY 从当前值变为 -90
        self.animation_start_rotationY = self.scene3D.camera3D.rotationY
        self.animation_target_rotationY = -90
        # 同时启动 rotationX 的缓动动画，从当前值变为 -60
        self.animation_start_rotationX = self.scene3D.camera3D.rotationX
        # self.animation_target_rotationX = -45
        self.animation_target_rotationX = self.scene3D.camera3D.rotationX
        self.animation_start_time = TimeUtilInter.getTimer()
        self.is_animating = True

        self.fouseCentenSprite.x = topos.x
        self.fouseCentenSprite.z = topos.z
        ty = self.getPosinMapHeightByVec4(topos.x, topos.z)
        if ty is not None:
            self.fouseCentenSprite.y = ty

    def drawGpsPointTomap(self, latitude, longitude):

        result = [obj for obj in self.scene3D.displayList if isinstance(obj, AreaMapDisplay3DSprite)]
        for dis in result:
            dis.drawGpsPointTomap(latitude, longitude)

        pass

    def update_rotation(self):
        """更新旋转角度"""
        if self.is_animating:
            current_time = TimeUtilInter.getTimer()
            elapsed = current_time - self.animation_start_time
            progress = min(elapsed / self.animation_duration, 1.0)

            # 使用缓动函数 easeOutQuad
            eased_progress = progress * (2 - progress)

            # 计算当前 rotationY 值
            self.scene3D.camera3D.rotationY = self.animation_start_rotationY + (
                    self.animation_target_rotationY - self.animation_start_rotationY) * eased_progress

            # 计算当前 rotationX 值
            self.scene3D.camera3D.rotationX = self.animation_start_rotationX + (
                    self.animation_target_rotationX - self.animation_start_rotationX) * eased_progress

            # 动画结束
            if progress >= 1.0:
                self.is_animating = False
                self.scene3D.camera3D.rotationY = self.animation_target_rotationY

        if self.is_rotating:
            self.gl_widget.update()  # 触发重绘

    def getPosinMapHeightByVec4(self, world_x: float, world_z: float) -> Optional[float]:

        result = [obj for obj in self.scene3D.displayList if isinstance(obj, AreaMapDisplay3DSprite)]
        _endTy: float | None = None
        for dis in result:
            ty = dis.get_terrain_height(world_x, world_z)
            if ty is not None:
                if _endTy is None:
                    _endTy = ty
                else:
                    _endTy = max(_endTy, ty)

        return _endTy

    def keyPressEventDownSpace(self):

        # print(self.baseCylinderSprite.x,self.baseCylinderSprite.y,self.baseCylinderSprite.z)

        latitude, longitude = settings.world_pos_to_gps(self.fouseCentenSprite)
        self.willdelearr.append((latitude, longitude))
        print("打印路径显示--------------------")
        lastpos = None
        outStr = ""
        for temp in self.willdelearr:
            if lastpos is not None:
                if temp[0] != lastpos[0] and temp[1] != lastpos[1]:
                    if len(outStr):
                        outStr = outStr + ","
                    outStr = outStr + str(temp[0]) + "," + str(temp[1])
                    pass
            lastpos = temp
        print(outStr)

