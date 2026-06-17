from ....display.particle.ball.ParticleBallData import ParticleBallData
from ....display.particle.locusball.Display3DLocusBallPartilce import Display3DLocusBallPartilce
from ....core.Pan3dByteArray import Pan3dByteArray
from ....core.Vector3D import Vector3D
from ....core.Matrix3D import Matrix3D
from random import random
import math
from pandocfilters import Math
import json


class ParticleLocusballData(ParticleBallData):
    def __init__(self, scene):
        super().__init__(scene)

        self.posAry: list = None;
        self.angleAry: list = None;
        self.tangentAry: list = None;
        self.tangentSpeed: float = None;

    def initBasePos(self):
        basePos: list = [];

        for i in range(self.totalNum):
            v3d: Vector3D;
            index: int = i * 3;
            if self.isRandom:
                roundv3d: Vector3D = Vector3D(self.round.x * self.round.w, self.round.y * self.round.w,
                                              self.round.z * self.round.w);


                v3d = Vector3D(self.posAry[index] + random() * roundv3d.x,
                               self.posAry[index + 1] + random() * roundv3d.y,
                               self.posAry[index + 2] + random() * roundv3d.z);
            else:
                v3d = Vector3D(self.posAry[index], self.posAry[index + 1], self.posAry[index + 2]);

            v3d = v3d.add(self.basePositon);

            for j in range(4):
                basePos.extend([v3d.x, v3d.y, v3d.z, i * self.shootSpeed]);

        objBallData = self.objData;
        objBallData.basePos = basePos;
        pass

    def initSpeed(self):
        objBallData = self.objData;
        beMove: list = [];
        for i in range(self.totalNum):
            resultv3d: Vector3D = Vector3D();
            v3d: Vector3D = Vector3D();
            if self.tangentSpeed == 0:
                resultv3d.addByNum(self.angleAry[i * 3], self.angleAry[i * 3 + 1], self.angleAry[i * 3 + 2]);
            elif self.tangentSpeed == 2:
                resultv3d.setTo( random() * 2 - 1, random() * 2 - 1,  random() * 2 - 1);
            else:
                v3d = Vector3D(self.tangentAry[i * 3], self.tangentAry[i * 3 + 1], self.tangentAry[i * 3 + 2]);
                v3d.scaleBy(self.tangentSpeed);
                resultv3d = resultv3d.add(v3d);

            resultv3d.normalize();

            if self.isSendRandom:
                resultv3d.scaleBy(self.speed * random());
            else:
                resultv3d.scaleBy(self.speed);

            for j in range(4):
                beMove.extend([resultv3d.x, resultv3d.y, resultv3d.z]);

        objBallData.beMove = beMove;

        pass

    def getParticle(self):
        return Display3DLocusBallPartilce(self.scene3D);

    def setAllByteInfo(self, byte: Pan3dByteArray):
        self.tangentSpeed = byte.readFloat();
        self.posAry = json.loads(byte.readUTF());
        self.angleAry = json.loads(byte.readUTF());
        self.tangentAry = json.loads(byte.readUTF());

        super().setAllByteInfo(byte)
