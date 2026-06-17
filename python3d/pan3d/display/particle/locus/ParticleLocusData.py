from ....display.particle.ParticleData import ParticleData
from ....display.particle.locus.Display3DLocusPartilce import Display3DLocusPartilce
from ....display.particle.locus.Display3DLocusPartilce import Display3DLocusShader
from ....core.Pan3dByteArray import Pan3dByteArray
from ....base.ResGC import ResGC
from ....base.ObjData import ObjData
from ....core.Vector3D import Vector3D
import numpy as np


class ParticleLocusData(ParticleData):
    def __init__(self, scene):
        super().__init__(scene)
        self.speed: float = 1;
        self.isLoop: bool = False;
        self.density: float;
        self.isEnd: bool = False;
        self.resultUvVec: list = None;
        self.caramPosVec: list = None;
        self.changUv: bool = False;
        self.uvVec: list = None;
        pass

    def getParticle(self):
        return Display3DLocusPartilce(self.scene3D);

    def regShader(self):

        hasParticleColor: bool = self.materialParam.material.hasParticleColor;

        if self.watchEye:
            isWatchEye: int = 1
        else:
            isWatchEye: int = 0;

        if self.isU or self.isV or self.isUV:
            changeUv: int = 1;
            self.changUv = True;
        else:
            self.changUv = False;
            changeUv: int = 0

        if hasParticleColor:
            shaderParameAry = [isWatchEye, changeUv, 1];
        else:
            shaderParameAry = [isWatchEye, changeUv, 0];

        self.materialParam.shader = self.scene3D.progrmaManager.getMaterialProgram(
            Display3DLocusShader.Display3DLocusShader,
            Display3DLocusShader, self.materialParam.material, shaderParameAry);
        pass

    def setAllByteInfo(self, byte: Pan3dByteArray):
        self.objData = ObjData(self.scene3D);
        self.isLoop = byte.readBoolean()
        self.speed = byte.readFloat()
        self.density = byte.readFloat()
        self.isEnd = byte.readBoolean()

        if self.version >= 38:
            self.watchEye = byte.readBoolean();

        dataWidth = 9;
        buflen = byte.getInt()
        buflen *= dataWidth * 4;

        buffArr = np.zeros(buflen, dtype=np.float32)

        resGC: ResGC = ResGC(self.scene3D)

        vertices = resGC.readByArrayBuffer(byte, 0, buffArr, dataWidth, 3, 4)
        normal = resGC.readByArrayBuffer(byte, 3, buffArr, dataWidth, 4, 4)
        uvs = resGC.readByArrayBuffer(byte, 7, buffArr, dataWidth, 2, 4)
        self.objData.buffArr = buffArr;

        iLen: int = byte.readInt();
        for k in range(iLen):
            self.objData.indexs.append(byte.readInt())

        self.objData.stride = dataWidth * 4;

        self.objData.upToGPU()

        super().setAllByteInfo(byte)
        self.initUV();
        if self.watchEye:
            self.caramPosVec = [0, 0, 0];

        self.uvVec = [1, 1, -1];
        if self.isU:
            self.uvVec[0] = -1;
        if self.isV:
            self.uvVec[1] = -1;
        if self.isUV:
            self.uvVec[2] = 1;

        pass

    def initUV(self):
        self.resultUvVec = [0, 0, 0];
        nowTime: int = 0;
        lifeRoundNum: float = (self.life / 100);
        moveUv: float = self.speed * nowTime / self.density / 10;
        if self.isEnd:
            moveUv = min(1, moveUv);

        fcVector: Vector3D;
        if self.isLoop:
            if self.life:
                moveUv = moveUv % (lifeRoundNum + 1)
                fcVector = Vector3D(moveUv, lifeRoundNum, -lifeRoundNum);
            else:
                moveUv = moveUv % 1;
                fcVector = Vector3D(moveUv + 1, 99, -2);

        else:
            if self.life:
                fcVector = Vector3D(moveUv, lifeRoundNum, -1);
            else:
                fcVector = Vector3D(moveUv, 99, -1);

        self.resultUvVec[0] = fcVector.x;
        self.resultUvVec[1] = fcVector.y;
        self.resultUvVec[2] = fcVector.z;

        pass;
