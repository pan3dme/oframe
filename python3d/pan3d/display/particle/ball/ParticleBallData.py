import math
from random import random

from pandocfilters import Math

from ....display.particle.ParticleData import ParticleData
from ....display.particle.ball.Display3DBallPartilce import Display3DBallPartilce
from ....display.particle.ball.Display3DBallPartilce import Display3DBallShader
from ....display.particle.ball.ParticleBallGpuData import ParticleBallGpuData

from ....core.Pan3dByteArray import Pan3dByteArray

from ....core.Vector3D import Vector3D
from ....core.Matrix3D import Matrix3D
from ....core.Quaternion import QuaternionPan3d

from ....core.Vector2D import Vector2D

from ....vo.DualQuatFloat32Array import DualQuatFloat32Array

from OpenGL.GL import *
import numpy as np


class ParticleBallData(ParticleData):
    def __init__(self, scene):
        super().__init__(scene);
        self.totalNum: int = 0;
        self.acceleration: float = 0;
        self.toscale: float = 0;
        self.shootSpeed: float = 0;
        self.isRandom: bool = False
        self.isSendRandom: bool = False
        self.round: Vector3D = Vector3D();
        self.is3Dlizi: bool = False
        self.halfCircle: bool = False
        self.shootAngly: Vector3D = Vector3D();

        self.speed: float = 0;
        self.isLoop: bool = False

        self.isSendAngleRandom: bool = False;

        self.waveform: Vector3D = Vector3D()

        self.closeSurface: bool = False
        self.isEven: bool = False
        self.paticleMaxScale: float = 0;
        self.paticleMinScale: float = 0;
        self.basePositon: Vector3D = Vector3D()

        self.baseRandomAngle: float = 0;
        self.shapeType: float = 0;

        self.lockX: bool = False
        self.lockY: bool = False

        self.addforce: Vector3D = Vector3D()

        self.lixinForce: Vector3D = Vector3D()

        self.islixinAngly: bool = False

        self.particleRandomScale: Vector3D = Vector3D();

        self.playSpeed: float = 0;
        self.facez: bool = False
        self.beginScale: float = 0;

        self.widthFixed: bool = False
        self.heightFixed: bool = False

        self.needAddSpeed: bool = None;
        self.addSpeedVec: Vector3D = None;

        self.textureRandomColorInfo: any = None
        self.needSelfRotation: bool = None;
        self.needRandomColor: bool = None
        self.animCtrlVec: list = None;
        self.uvCtrlVec: list = None;

    def setAllByteInfo(self, byte: Pan3dByteArray):
        self.totalNum = int(byte.readFloat())
        self.acceleration = byte.readFloat()
        self.toscale = byte.readFloat()
        self.shootSpeed = byte.readFloat()
        self.isRandom = byte.readBoolean()
        self.isSendRandom = byte.readBoolean()
        self.round.x = byte.readFloat()
        self.round.y = byte.readFloat()
        self.round.z = byte.readFloat()
        self.round.w = byte.readFloat()

        self.is3Dlizi = byte.readBoolean()
        self.halfCircle = byte.readBoolean()
        self.shootAngly.x = byte.readFloat()
        self.shootAngly.y = byte.readFloat()
        self.shootAngly.z = byte.readFloat()
        self.shootAngly.w = byte.readFloat()

        # this._shootAngly.normalize();

        self.speed = byte.readFloat()
        self.isLoop = byte.readBoolean()

        self.isSendAngleRandom = byte.readBoolean()

        self.waveform.x = byte.readFloat()
        self.waveform.y = byte.readFloat()
        self.waveform.z = byte.readFloat()
        self.waveform.w = byte.readFloat()

        self.closeSurface = byte.readBoolean()
        self.isEven = byte.readBoolean()
        self.paticleMaxScale = byte.readFloat()
        self.paticleMinScale = byte.readFloat()
        self.basePositon.x = byte.readFloat()
        self.basePositon.y = byte.readFloat()
        self.basePositon.z = byte.readFloat()
        self.basePositon.w = byte.readFloat()

        self.baseRandomAngle = byte.readFloat()
        self.shapeType = byte.readFloat()

        self.lockX = byte.readBoolean()
        self.lockY = byte.readBoolean()

        self.addforce.x = byte.readFloat()
        self.addforce.y = byte.readFloat()
        self.addforce.z = byte.readFloat()
        self.addforce.w = byte.readFloat()
        self.addforce.scaleByW();

        self.lixinForce.x = byte.readFloat()
        self.lixinForce.y = byte.readFloat()
        self.lixinForce.z = byte.readFloat()
        self.lixinForce.w = byte.readFloat()

        self.islixinAngly = byte.readBoolean()

        self.particleRandomScale = Vector3D();
        self.particleRandomScale.x = byte.readFloat()
        self.particleRandomScale.y = byte.readFloat()
        self.particleRandomScale.z = byte.readFloat()
        self.particleRandomScale.w = byte.readFloat()

        self.playSpeed = byte.readFloat()
        self.facez = byte.readBoolean()
        self.beginScale = byte.readFloat()

        self.widthFixed = byte.readBoolean()
        self.heightFixed = byte.readBoolean()

        self.readRandomColor(byte);

        if self.acceleration != 0 or self.addforce.x != 0 or self.addforce.y != 0 or self.addforce.z != 0:
            self.needAddSpeed = True;
            self.addSpeedVec = Vector3D(self.addforce.x, self.addforce.y, self.addforce.z);
            pass
        else:
            self.needAddSpeed = False;
            self.addSpeedVec = Vector3D();
            pass

        if self.toscale != 0 or self.waveform.x != 0 or self.waveform.y != 0:
            self.needScale = True;
            self.scaleVec = Vector3D(self.toscale, self.waveform.x, self.waveform.y, self.beginScale);

            self.scaleCtrlVec = Vector3D(1, 1, 1, 1);
            if self.widthFixed:
                self.scaleCtrlVec.x = 0;
            else:
                self.scaleCtrlVec.x = 1;

            if self.heightFixed:
                self.scaleCtrlVec.y = 0;
            else:
                self.scaleCtrlVec.y = 1;

            self.scaleCtrlVec.z = self.paticleMaxScale - 1
            self.scaleCtrlVec.w = self.paticleMinScale - 1


        else:
            self.scaleVec = Vector3D(1, 1, 1, 1);
            self.scaleCtrlVec = Vector3D(1, 1, 1, 1);
            self.needScale = False;

        super().setAllByteInfo(byte)

        if self.isLoop:
            self.timeVec = Vector3D(0, self.acceleration, self.life, 1);
        else:
            self.timeVec = Vector3D(0, self.acceleration, self.life, -1);

        if self.is3Dlizi:
            self.wordPosVec = [0, 0, 0];
            self.caramPosVec = [0, 0, 0];
            self.allRotationMatrix = Matrix3D();

        pass

    def readRandomColor(self, byte):
        randomColorLen: int = byte.readInt();
        obj: any = {};
        obj['alpha'] = [];
        obj['color'] = [];
        obj['pos'] = [];
        for i in range(randomColorLen):
            obj['alpha'].push(byte.readFloat())
            obj['color'].push(byte.readFloat())
            obj['pos'].push(byte.readFloat())

        self.textureRandomColorInfo = obj;
        pass

    def initBaseData(self):
        verterList: list = [];
        uvAry: list = [];
        indexs: list = [];
        for i in range(self.totalNum):
            self.makeRectangleData(verterList, uvAry, self.width, self.height, self.originWidthScale,
                                   self.originHeightScale, self.isUV, self.isU, self.isV, self.animLine, self.animRow,
                                   i);
            indexs.extend([0 + i * 4, 1 + i * 4, 2 + i * 4, 0 + i * 4, 2 + i * 4, 3 + i * 4]);

        objBallData = self.objData;
        objBallData.vertices = verterList;
        objBallData.uvs = uvAry;
        objBallData.indexs = indexs;
        pass

    def makeRectangleData(self, verterList: list, uvAry: list, width: float, height: float, offsetX: float = 0.5,
                          offsetY: float = 0.5, isUV: bool = False, isU: bool = False, isV: bool = False,
                          animLine: float = 1, animRow: float = 1, indexID: float = 0):

        ranScale: float = random() * (
                self.particleRandomScale.x - self.particleRandomScale.y) + self.particleRandomScale.y;
        verterList.extend([(-offsetX * width) * ranScale, (height - offsetY * height) * ranScale, 0]);
        verterList.extend([(width - offsetX * width) * ranScale, (height - offsetY * height) * ranScale, 0]);
        verterList.extend([(width - offsetX * width) * ranScale, (-offsetY * height) * ranScale, 0]);
        verterList.extend([(-offsetX * width) * ranScale, (-offsetY * height) * ranScale, 0]);

        ary: list = [];
        ary.append(Vector2D(0, 0));
        ary.append(Vector2D(0, 1 / animRow));
        ary.append(Vector2D(1 / animLine, 1 / animRow));
        ary.append(Vector2D(1 / animLine, 0));

        if isU:
            for i in range(len(ary)):
                ary[i].x = - ary[i].x;

        if isV:
            for i in range(len(ary)):
                ary[i].y = - ary[i].y;

        if isUV:
            ary.append(ary.pop(0));

        for i in range(len(ary)):
            uvAry.extend([ary[i].x, ary[i].y, indexID]);

        pass

    def initBasePos(self):
        basePos: list = [];
        for i in range(self.totalNum):
            v3d: Vector3D;
            ma: Matrix3D;
            if self.isRandom:
                roundv3d: Vector3D = Vector3D(self.round.x * self.round.w, self.round.y * self.round.w,
                                              self.round.z * self.round.w);
                if self.isEven:
                    if self.closeSurface:
                        v3d = Vector3D(0, 0, roundv3d.z);
                        ma = Matrix3D();
                        ma.appendRotation(random() * 360, Vector3D.Y_AXIS);
                        v3d = ma.transformVector(v3d);
                        v3d.y = roundv3d.y * random() * 2 - roundv3d.y;
                    else:
                        v3d = Vector3D(0, 0, roundv3d.z * random() * 2 - roundv3d.z);
                        ma = Matrix3D();
                        ma.appendRotation(random() * 360, Vector3D.Y_AXIS);
                        v3d = ma.transformVector(v3d);
                        v3d.y = roundv3d.y * random() * 2 - roundv3d.y;
                    pass

                else:
                    if self.closeSurface:
                        v3d = Vector3D(0, 0, roundv3d.z);
                        ma = Matrix3D();

                        if self.halfCircle:
                            ma.appendRotation(- random() * 180, Vector3D.X_AXIS);
                        else:
                            ma.appendRotation(random() * 360, Vector3D.X_AXIS);

                        ma.appendRotation(random() * 360, Vector3D.Y_AXIS);
                        v3d = ma.transformVector(v3d);
                    else:
                        if self.halfCircle:
                            v3d = Vector3D(roundv3d.x * random() * 2 - roundv3d.x, roundv3d.y * random(),
                                           roundv3d.z * random() * 2 - roundv3d.z);
                        else:
                            v3d = Vector3D(roundv3d.x * random() * 2 - roundv3d.x,
                                           roundv3d.y * random() * 2 - roundv3d.y,
                                           roundv3d.z * random() * 2 - roundv3d.z);



            else:
                v3d = Vector3D();

            v3d = v3d.add(self.basePositon);

            for j in range(4):
                basePos.extend([v3d.x, v3d.y, v3d.z, i * self.shootSpeed]);

        objBallData = self.objData;
        objBallData.basePos = basePos;
        pass

    def ma_fromVtoV(self, ma, basePos: Vector3D, newPos: Vector3D):
        axis: Vector3D = basePos.cross(newPos);
        axis.normalize();
        bbb = basePos.dot(newPos)
        # 等待优化
        angle: float = math.acos(bbb%1.0);
        q: QuaternionPan3d = QuaternionPan3d();
        q.fromAxisAngle(axis, angle);
        q.toMatrix3D(ma);
        pass

    def initSpeed(self):
        objBallData = self.objData;
        beMove: list = [];
        for i in range(self.totalNum):
            resultv3d: Vector3D = Vector3D();
            v3d: Vector3D = Vector3D();

            if self.shootAngly.x != 0 or self.shootAngly.y != 0 or self.shootAngly.z != 0:
                r: float = math.tan(self.shootAngly.w * math.pi / 180 * random());
                a: float = 360 * math.pi / 180 * random();

                v3d = Vector3D(math.sin(a) * r, math.cos(a) * r, 1);
                ma: Matrix3D = Matrix3D();
                self.ma_fromVtoV(ma, Vector3D(0, 0.0101, 0.998),
                                 Vector3D(self.shootAngly.x, self.shootAngly.y, self.shootAngly.z));
                v3d = ma.transformVector(v3d);

                v3d.normalize();
                resultv3d = resultv3d.add(v3d);
                pass

            if self.lixinForce.x != 0 or self.lixinForce.y != 0 or self.lixinForce.z != 0:
                if random() > 0.5:
                    v3d.x = -self.lixinForce.x;
                else:
                    v3d.x = -self.lixinForce.x;
                if random() > 0.5:
                    v3d.y = -self.lixinForce.y;
                else:
                    v3d.y = -self.lixinForce.y;
                if random() > 0.5:
                    v3d.z = -self.lixinForce.z;
                else:
                    v3d.z = -self.lixinForce.z;
                v3d.normalize();
                resultv3d = resultv3d.add(v3d);

            if self.islixinAngly:
                if self.isEven:
                    v3d = Vector3D(objBallData.basePos[i * 16], 0, objBallData.basePos[i * 16 + 2]);
                else:
                    v3d = Vector3D(objBallData.basePos[i * 16], objBallData.basePos[i * 16 + 1],
                                   objBallData.basePos[i * 16 + 2]);

                v3d.normalize();
                resultv3d = resultv3d.add(v3d);

            resultv3d.normalize();

            if self.isSendRandom:
                resultv3d.scaleBy(self.speed * random());
            else:
                resultv3d.scaleBy(self.speed);

            ranAngle: float = self.baseRandomAngle * random() * math.pi / 180;

            for j in range(4):
                beMove.extend([resultv3d.x, resultv3d.y, resultv3d.z]);

        objBallData.beMove = beMove;

        pass

    def initSelfRotaion(self):

        baseRotationAngle: float = 0;
        baseRotationSpeed: float = 0;
        if self.ziZhuanAngly.x == 0 and self.ziZhuanAngly.y == 0 and self.ziZhuanAngly.z == 0 and self.ziZhuanAngly.w == 0:
            self.needSelfRotation = False;
            return;

        if self.is3Dlizi:
            self.needSelfRotation = False;
            return;

        self.needSelfRotation = True;
        vecs: list = [];
        flag: int = 0;

        while flag < self.totalNum:

            _baseRotationAngle = self.ziZhuanAngly.x;
            if self.ziZhuanAngly.y == 1:
                baseRotationAngle = baseRotationAngle * random();

            baseRotationSpeed = self.ziZhuanAngly.z;
            if self.ziZhuanAngly.w == 1:
                baseRotationSpeed = baseRotationSpeed * random();
            elif self.ziZhuanAngly.w == -1:
                baseRotationSpeed = baseRotationSpeed * (random() * 2 - 1);

            vecs.extend([baseRotationAngle, baseRotationSpeed]);
            vecs.extend([baseRotationAngle, baseRotationSpeed]);
            vecs.extend([baseRotationAngle, baseRotationSpeed]);
            vecs.extend([baseRotationAngle, baseRotationSpeed]);
            flag = flag + 1;
        objBallData = self.objData;
        objBallData.baseRotation = vecs;
        pass

    def initBaseColor(self):
        pass

    def uploadGpu(self):
        self.objData = ParticleBallGpuData(self.scene3D);
        self.initBaseData();
        self.initBasePos();
        self.initSpeed();
        self.initSelfRotaion();
        if self.needRandomColor:
            self.initBaseColor();

        self.pushToGpu();

        uvAry: list = [];
        verterList: list = [];
        ary: list = [];
        ary.append(Vector2D(0, 0));
        ary.append(Vector2D(0, 1));
        ary.append(Vector2D(1, 1));
        ary.append(Vector2D(1, 0));

        for i in range(len(ary)):
            uvAry.append(ary[i].x);
            uvAry.append(ary[i].y);

        verterList.append(-100);
        verterList.append(0);
        verterList.append(-100);

        # verterList.append(ary[0].x);
        # verterList.append(ary[0].y);

        verterList.append(100);
        verterList.append(0);
        verterList.append(-100);

        # verterList.append(ary[1].x);
        # verterList.append(ary[1].y);

        verterList.append(100);
        verterList.append(0);
        verterList.append(100);

        # verterList.append(ary[2].x);
        # verterList.append(ary[2].y);

        verterList.append(-100);
        verterList.append(0);
        verterList.append(100);

        # verterList.append(ary[3].x);
        # verterList.append(ary[3].y);

        indexs: list = [0, 1, 2, 0, 2, 3];

        # self.objData.buffArr = np.array(verterList, dtype=np.float32);
        # self.objData.indexs = indexs;
        # self.objData.upToGPU()
        pass;

    def pushToGpu(self):
        self.compressVertex()

    def compressVertex(self):
        objBallData = self.objData;
        sizeLenNum: int = int(len(objBallData.vertices) / 3);
        itemSize: int = 13;
        if self.needSelfRotation:
            itemSize += 2;

        if self.needRandomColor:
            self.objBallData.randomOffset = itemSize * 4;
            itemSize += 4;

        objBallData.stride = itemSize * 4;

        ary: list = [];

        for i in range(sizeLenNum):
            for j in range(3):
                ary.append(objBallData.vertices[i * 3 + j]);

            for j in range(3):
                ary.append(objBallData.uvs[i * 3 + j]);

            for j in range(4):
                ary.append(objBallData.basePos[i * 4 + j]);

            for j in range(3):
                ary.append(objBallData.beMove[i * 3 + j]);

            if self.needSelfRotation:
                for j in range(2):
                    ary.append(objBallData.baseRotation[i * 2 + j]);

            if self.needRandomColor:
                for j in range(4):
                    ary.append(objBallData.randomColor[i * 4 + j]);

        print(ary);

        self.objData.buffArr = np.array(ary, dtype=np.float32);
        self.objData.indexs = self.objData.indexs;
        self.objData.upToGPU()

        pass

    def getParticle(self):
        return Display3DBallPartilce(self.scene3D);

    def regShader(self):

        self.uploadGpu();

        shaderParameAry: list = self.getShaderParam();

        self.materialParam.shader = self.scene3D.progrmaManager.getMaterialProgram(
            Display3DBallShader.Display3DBallShader,
            Display3DBallShader, self.materialParam.material, shaderParameAry);
        pass

    def getShaderParam(self):
        if self.animRow != 1 or self.animLine != 1:
            self.uvType = 1;
            self.animCtrlVec = [self.animLine, self.animRow, self.animInterval];
        elif self.uSpeed != 0 or self.vSpeed != 0:
            self.uvType = 2;
            self.uvCtrlVec = [self.uSpeed, self.vSpeed];
        else:
            self.uvType = 0;

        hasParticleColor: bool = self.materialParam.material.hasParticleColor;
        self.needRandomColor = self.materialParam.material.hasVertexColor;

        shaderParameAry: list

        hasParticle: int;
        if hasParticleColor:
            hasParticle = 1;
        else:
            hasParticle = 0;

        hasRandomClolr: int = 0;
        if self.needRandomColor:
            hasRandomClolr = 1;

        isMul: int = 0;
        if self.is3Dlizi:
            isMul = 1;

        needRotation: int = 0;
        if self.needSelfRotation:
            needRotation = 1;

        needScale: int = 0;
        if self.needScale:
            needScale = 1;

        needAddSpeed: int = 0;
        if self.needAddSpeed:
            needAddSpeed = 1;

        shaderParameAry = [hasParticle, hasRandomClolr, isMul, needRotation, needScale, needAddSpeed, self.uvType];

        return shaderParameAry;
