from ..vo.SkinMesh import SkinMesh
from ..base.MeshData import MeshData
from ..scene3D.Scene_data import Scene_data
from ..vo.AnimData import AnimData
from ..skill.Skill import Skill
from ..display.Display3DSprite import Display3DSprite
from ..vo.DualQuatFloat32Array import DualQuatFloat32Array
from OpenGL.GL import *
import numpy as np


class Display3dMovie(Display3DSprite):
    def __init__(self, scene):
        super().__init__(scene)

        self.skinMesh: SkinMesh = None;
        self.fileScale: float = 1.0;
        self.actionTime: int = 0
        self.curentFrame: int = 0
        self.completeState: int = 0
        self.curentAction: str = 'stand'
        self.defaultAction: str = 'stand'
        # self.defaultAction: str = 'stand'

    def playSkill(self, skill: Skill):
        self.scene3D.skillManager.playSkill(skill);
        pass

    def getDisplayShader(self):
        return None
        # shader = Display3dMovieShader(self.scene3D);
        # shader.encode()
        # return shader;

    def setRoleUrl(self, url: str):
        def back(value: SkinMesh):
            self.skinMesh = value;
            self.fileScale = value.fileScale;
            self.updateMatrix();
            self.loadTextureByUrl('nberciyuan/jiaose/taidaoshou/zhujue_jianshi_nan.jpg')
            pass

        self.scene3D.meshDataManager.getMeshData(url, back)

    def loadTextureByUrl(self, url: str):
        pass

    def setMeshVa(self, meshData: MeshData):

        self.scene3D.context3D.setVa(meshData.vao)
        glDrawArrays(GL_TRIANGLES, 0, meshData.vertex_count);

    def setMeshVc(self, meshData: MeshData):

        animData: AnimData = self.getCurentAnimData();

        self.scene3D.animManager.makeAnimDataProcessAction(animData, self.skinMesh);

        dualQuatFrame: DualQuatFloat32Array = animData.getBoneQPAryByMesh(meshData)[meshData.uid][self.curentFrame];

        self.scene3D.context3D.setVcMatrix4fv(meshData.material.shader, 'posMatrix3D', self.posMatrix)
        self.scene3D.context3D.setVcMatrix4fv(meshData.material.shader, 'vpMatrix3D', self.scene3D.camera3D.vpMatrix3D)

        self.scene3D.context3D.setVc4fv(meshData.material.shader, "boneQ",
                                        np.array(dualQuatFrame.quat, dtype=np.float32), 54);
        self.scene3D.context3D.setVc3fv(meshData.material.shader, "boneD",
                                        np.array(dualQuatFrame.pos, dtype=np.float32), 54);

        pass

    def getCurentAnimData(self):
        animData = self.skinMesh.animDic[self.curentAction];
        self.scene3D.animManager.makeAnimDataProcessAction(animData, self.skinMesh);
        return animData;

    def updateFrame(self, t: int):
        self.actionTime += t;
        if self.skinMesh is None:
            return;
        animData: AnimData = self.getCurentAnimData();

        # self.curentFrame += 1;
        self.curentFrame = int(self.actionTime / (Scene_data.frameTime*1.50));

        if self.curentFrame >= len(animData.matrixAry):

            if self.completeState == 0:
                self.actionTime = 0;
                self.curentFrame = 0;
            elif self.completeState == 1:
                self.curentFrame = len(animData.matrixAry) - 1;
            elif self.completeState == 2:
                self.curentFrame = 0;
                self.completeState = 0;
                self.changeAction(self.defaultAction);

            else:
                print('没有动作')

    def changeAction(self, value: str):
        self.curentAction = value;
        pass

    def updateMaterialMesh(self, meshData: MeshData):

        self.scene3D.context3D.setProgram(meshData.material.shader)
        self.setMaterialTexture(meshData.material, meshData.materialParam);
        self.setMeshVc(meshData)
        self.setMeshVa(meshData)

    def upData(self):
        self.updateMatrix()
        if self.skinMesh:
            for i in range(len(self.skinMesh.meshAry)):
                meshData: MeshData = self.skinMesh.meshAry[i];
                self.updateMaterialMesh(meshData);

    def play(self, action: str, completeState: int = 0, needFollow: bool = True):

        if self.curentAction == action:
            return;
        self.curentAction = action;
        self.completeState = completeState;
        self.actionTime = 0;
        self.updateFrame(0);

        if action in self.skinMesh.animDic:
            return True;
        else:
            return False;

        pass
