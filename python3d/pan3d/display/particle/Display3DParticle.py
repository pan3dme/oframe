from ...display.Display3D import Display3D
from ...core.Vector3D import Vector3D
from ...core.Matrix3D import Matrix3D
from ...program.Shader3D import Shader3D
from ...scene3D.Context3D import Context3D
from ...scene3D.Scene_data import Scene_data
from ...display.particle.ctrl.TimeLine import TimeLine
from ...mateial.DynamicTexItem import DynamicTexItem;
import numpy as np
from OpenGL.GL import *
import glm


class Display3DParticle(Display3D):
    def __init__(self, scene3D):
        super().__init__(scene3D);
        self.visible = True;
        self.time: int = 0;
        self.bindScale: Vector3D;
        self.timeline: TimeLine;
        self.rotationMatrix: Matrix3D = Matrix3D();
        self.modelMatrix: Matrix3D = Matrix3D();
        self.bindVecter3d: Vector3D;
        self.bindMatrix: Matrix3D = Matrix3D();
        self.invertBindMatrix: Matrix3D = Matrix3D();
        self.groupMatrix: Matrix3D = Matrix3D();
        self.shader: Shader3D = None;
        self.data: any;
        pass

    def setTimeLine(self, tl: TimeLine):
        self.timeline = tl;
        self.beginTime = tl.beginTime;

        pass

    def onCreated(self):
        pass

    def setBind(self, pos: Vector3D, rotation: Matrix3D, scale: Vector3D, invertRotation: Matrix3D,
                groupMatrix: Matrix3D):
        self.bindVecter3d = pos;
        self.bindMatrix = rotation;
        self.bindScale = scale;
        self.invertBindMatrix = invertRotation;
        self.groupMatrix = groupMatrix;

    def updateTime(self, t: int):
        self.time = t - self.beginTime;
        self.time += self.data.delayedTime;
        self.timeline.updateTime(t);
        self.visible = self.timeline.visible;
        self.posMatrix.identity();
        self.posMatrix.prependScale(self.scaleX * 0.1 * self.bindScale.x * self.data.overAllScale,
                                    self.scaleY * 0.1 * self.bindScale.y * self.data.overAllScale,
                                    self.scaleZ * 0.1 * self.bindScale.z * self.data.overAllScale);

        self.timeline.updateMatrix(self.posMatrix, self);

        pass

    def setMaterialVc(self):
        if not self.data.materialParam:
            return;
        dynamicConstList: list = self.data.materialParam.dynamicConstList;
        t: int = self.time % (Scene_data.frameTime * self.data.life);
        for i in range(len(dynamicConstList)):
            dynamicConstList[i].update(t);

        if self.data.materialParam.material.fcNum <= 0:
            return;

        t = t * self.data.materialParam.material.timeSpeed;
        self.data.materialParam.material.update(t);
        ctx: Context3D = self.scene3D.context3D;

        ctx.setVc4fv(self.data.materialParam.shader, "fc", self.data.materialParam.material.fcData,
                     self.data.materialParam.material.fcNum);

        pass

    def setVc(self):

        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'modelMatrix', self.modelMatrix)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'vpMatrix3D', self.scene3D.camera3D.vpMatrix3D)

        pass

    def setVa(self):
        pass

    def setMaterialTexture(self):
        if not self.data.materialParam:
            return

        ctx: Context3D = self.scene3D.context3D
        texVec: list = self.data.materialParam.material.texList;
        for i in range(len(texVec)):
            if texVec[i].isDynamic:
                continue;
            pass

        texDynamicVec: list = self.data.materialParam.dynamicTexList;
        for j in range(len(texDynamicVec)):
            dynamicTexItem: DynamicTexItem = texDynamicVec[j]




            ctx.setRenderTexture(self.data.materialParam.shader, dynamicTexItem.target.getName(),
                                 dynamicTexItem.target.id, dynamicTexItem.getUsetextureRes());
            pass

        pass

    def update(self):
        if self.visible and (self.data is not None) and (self.scene3D is not None):
            if (self.data.materialParam is not None) and (self.data.materialParam.shader is not None):
                self.shader = self.data.materialParam.shader;
                ctx: Context3D = self.scene3D.context3D
                ctx.setProgram(self.shader);
                ctx.setBlendParticleFactors(self.data.alphaMode);
                self.updateMatrix();
                self.setMaterialVc();
                self.setMaterialTexture();
                self.setVc();
                self.setVa();
                pass

        pass

    def reset(self):
        self.timeline.reset();
        self.updateTime(0);

    def updateMatrix(self):

        self.modelMatrix.identity()
        self.modelMatrix.append(self.posMatrix);

        self.modelMatrix.appendTranslation(self.bindVecter3d.x, self.bindVecter3d.y, self.bindVecter3d.z);
        pass
