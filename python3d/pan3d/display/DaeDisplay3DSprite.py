from ..display.DispBase3dSprite import DispBase3dSprite
from ..base.ObjData import ObjData
from ..core.Matrix3D import Matrix3D
from ..core.Vector3D import Vector3D
from ..mateial.daematerial.DaeMaterial import DaeMaterial
import numpy as np


class DaeDisplay3DSprite(DispBase3dSprite):
    def __init__(self, scene):

        super().__init__(scene)
        self.name: str = None;
        self.daeMaterial: DaeMaterial = None

    def encodeBaseShader(self):
        pass

    def upData(self):
        if not self.daeMaterial:
            return

        self.shader = self.daeMaterial.shader

        self.scene3D.context3D.setProgram(self.shader);
        self.setVc()

        sunDirect:Vector3D=Vector3D(1.3,3,1)

        # sunDirect=Vector3D(self.scene3D.camera3D.x,self.scene3D.camera3D.y,self.scene3D.camera3D.z)
        sunDirect.normalize()

        self.scene3D.context3D.setVc3fv(self.shader, 'sunDirect', [sunDirect.x,sunDirect.y,sunDirect.z],1)

        self.scene3D.context3D.setVaOld(self.objData.vbo, 0, 3, 32, 0)
        self.scene3D.context3D.setVaOld(self.objData.vbo, 1, 3, 32, 12)
        self.scene3D.context3D.setVaOld(self.objData.vbo, 2, 2, 32, 24)

        self.scene3D.context3D.setRenderTexture(self.shader, 'fs0',
                                                0, self.daeMaterial.mainTextureRes);

        self.scene3D.context3D.drawCallOld(self.objData.eboID, len(self.objData.indexs))

        pass

    def makeDaeToObjData(self, prim):
        if not prim.texcoordset:
            return
        self.objData = ObjData(self.scene3D)

        # index = prim.index;
        # vertex = prim.vertex;
        # texcoordset = prim.texcoordset;

        buffArr = []
        indexNum = 0;
        scaleNum = 10.0;

        for idx in prim.index:
            buffArr.extend(prim.vertex[idx[0][0]] * scaleNum)
            buffArr.extend(prim.normal[idx[0][1]])
            buffArr.extend(prim.texcoordset[0][idx[0][2]])

            buffArr.extend(prim.vertex[idx[1][0]] * scaleNum)
            buffArr.extend(prim.normal[idx[1][1]])
            buffArr.extend(prim.texcoordset[0][idx[1][2]])

            buffArr.extend(prim.vertex[idx[2][0]] * scaleNum)
            buffArr.extend(prim.normal[idx[2][1]])
            buffArr.extend(prim.texcoordset[0][idx[2][2]])

            self.objData.indexs.extend([indexNum, indexNum + 1, indexNum + 2])

            indexNum = indexNum + 3

            pass
        sideNum = 8;
        for i in range(int(len(buffArr) / sideNum)):
            buffArr[i * sideNum] = buffArr[i * sideNum] * -1.0;

        self.objData.buffArr = np.array(buffArr, dtype=np.float32);
        self.objData.upToGPU()
