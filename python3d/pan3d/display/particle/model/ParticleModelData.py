from ....display.particle.ParticleData import ParticleData
from ....display.particle.model.Display3DModelPartilce import Display3DModelPartilce
from ....display.particle.model.Display3DModelPartilce import Display3DModelShader
from ....core.Pan3dByteArray import Pan3dByteArray
from ....base.ResGC import ResGC
from ....base.ObjData import ObjData
from ....core.Vector3D import Vector3D
import numpy as np


class ParticleModelData(ParticleData):
    def __init__(self, scene):
        super().__init__(scene)

    def setAllByteInfo(self, byte: Pan3dByteArray):

        self.objData = ObjData(self.scene3D);
        self.maxAnimTime = byte.readFloat()
        vLenNum: int = byte.getInt();
        dataWidth = 5;
        buflen: int = vLenNum * dataWidth * 4;

        buffArr = np.zeros(buflen, dtype=np.float32)

        resGC: ResGC = ResGC(self.scene3D)

        vertices = resGC.readByArrayBuffer(byte, 0, buffArr, dataWidth, 3, 4)
        uvs = resGC.readByArrayBuffer(byte, 3, buffArr, dataWidth, 2, 4)

        self.objData.buffArr = buffArr;
        iLen: int = byte.readInt();
        for k in range(iLen):
            self.objData.indexs.append(byte.readInt())

        self.objData.stride = dataWidth * 4;
        self.objData.upToGPU()
        if self.version >= 36:
            self.depthMode = byte.readInt();



        super().setAllByteInfo(byte)

        pass

    def getParticle(self):
        return Display3DModelPartilce(self.scene3D);

    def regShader(self):

        self.materialParam.shader = self.scene3D.progrmaManager.getMaterialProgram(
            Display3DModelShader.Display3DModelShader,
            Display3DModelShader, self.materialParam.material);
        pass
