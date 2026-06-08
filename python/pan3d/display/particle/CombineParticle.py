from ...event.EventDispatcher import EventDispatcher
from ...core.Matrix3D import Matrix3D
from ...core.Vector3D import Vector3D
from ...event.BaseEvent import BaseEvent
from ...display.particle.Display3DParticle import Display3DParticle


class CombineParticle(EventDispatcher):
    def __init__(self):
        super().__init__()
        self.url: str;
        self.sceneVisible: bool = True;
        self.visible: bool;
        self.displayAry: list = [];
        self.bindVecter3d: Vector3D = Vector3D();
        self.bindMatrix: Matrix3D = Matrix3D();
        self.bindScale: Vector3D = Vector3D(1, 1, 1);
        self.invertBindMatrix: Matrix3D = Matrix3D();
        self.groupMatrix: Matrix3D = Matrix3D();
        self.sourceData: any;
        self.time: int = 0;
        pass

    def setBindVecter3d(self, x, y, z):
        self.bindVecter3d.x = x;
        self.bindVecter3d.y = y;
        self.bindVecter3d.z = z;

    def addPrticleItem(self, dis: Display3DParticle):
        self.visible = True;
        dis.setBind(self.bindVecter3d, self.bindMatrix, self.bindScale, self.invertBindMatrix, self.groupMatrix);
        self.displayAry.append(dis);
        pass

    def updateTime(self, t: int):
        self.time += t;

        for i in range(len(self.displayAry)):
            self.displayAry[i].updateTime(self.time);

        self.updateBind();
        if self.time >= self.maxTime:
            self.dispatchEvent(BaseEvent(BaseEvent.COMPLETE));

        pass

    def reset(self):
        self.time = 0;
        for dis in self.displayAry:
            dis.reset();

    def updateBind(self):
        pass

    def update(self):
        if not self.sceneVisible:
            return;
        for i in range(len(self.displayAry)):
            self.displayAry[i].update();

    pass
