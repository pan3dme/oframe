from ....display.particle.ctrl.BaseAnim import BaseAnim
from ....scene3D.Scene_data import Scene_data


class SelfRotation(BaseAnim):
    def __init__(self):
        super().__init__();

    def dataByte(self,arr: list):
        self.beginTime = int(arr[0]);
        if int(arr[1]) == -1:
            self.lastTime = Scene_data.MAX_NUMBER;
        else:
            self.lastTime = int(arr[1]);

        self.speed = arr[2] * 0.1;
        self.aSpeed = arr[3] * 0.1;
        pass
