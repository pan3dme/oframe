from ....display.particle.ParticleData import ParticleData
from ....display.particle.bone.Display3DBonePartilce import Display3DBonePartilce
from ....display.particle.bone.Display3DBonePartilce import Display3DBoneShader
from ....core.Pan3dByteArray import Pan3dByteArray
from ....base.MeshData import MeshData
from ....vo.AnimData import AnimData
from ....base.ResGC import ResGC

from ....vo.DualQuatFloat32Array import DualQuatFloat32Array

from OpenGL.GL import *
import numpy as np


class ParticleBoneData(ParticleData):

    def __init__(self, scene):
        super().__init__(scene)
        self.meshData: MeshData = None
        self.animData: AnimData = None
        self.objScale: float = 1.0;

    def getParticle(self):
        return Display3DBonePartilce(self.scene3D);

    def readFrameQua(self, byte: Pan3dByteArray):
        tempNum: float = byte.readFloat()
        RGB32767: float = 32767.0

        frameNum: int = byte.readInt();
        frameDualQuat: list = []

        for i in range(frameNum):
            lenNum: int = byte.readInt();
            qualQuatFloat32Array: DualQuatFloat32Array = DualQuatFloat32Array()
            qualQuatFloat32Array.quat = [0 for _ in range(lenNum * 4)]
            qualQuatFloat32Array.pos = [0 for _ in range(lenNum * 3)]

            for j in range(lenNum):
                qualQuatFloat32Array.quat[j * 4 + 0] = byte.readShort() / RGB32767;
                qualQuatFloat32Array.quat[j * 4 + 1] = byte.readShort() / RGB32767;
                qualQuatFloat32Array.quat[j * 4 + 2] = byte.readShort() / RGB32767;
                qualQuatFloat32Array.quat[j * 4 + 3] = byte.readShort() / RGB32767;

                qualQuatFloat32Array.pos[j * 3 + 0] = byte.readShort() / RGB32767* tempNum;
                qualQuatFloat32Array.pos[j * 3 + 1] = byte.readShort() / RGB32767* tempNum;
                qualQuatFloat32Array.pos[j * 3 + 2] = byte.readShort() / RGB32767* tempNum;
                pass

            frameDualQuat.append(qualQuatFloat32Array);
            pass

        self.animData.boneQPAry = [];
        self.animData.boneQPAry.append(frameDualQuat);
        pass

    def setAllByteInfo(self, byte: Pan3dByteArray):
        self.meshData = MeshData(self.scene3D)
        self.animData = AnimData()
        self.objScale = byte.readFloat();

        dataWidth = 13;
        buflen = byte.getInt()
        buflen *= dataWidth * 4;
        buffArr = np.zeros(buflen, dtype=np.float32)

        resGC: ResGC = ResGC(self.scene3D)

        vertices = resGC.readByArrayBuffer(byte, 0, buffArr, dataWidth, 3)
        uvs = resGC.readByArrayBuffer(byte, 3, buffArr, dataWidth, 2)

        indexs = resGC.readIntForTwoByte(byte);

        boneIDAry = resGC.readByArrayBuffer(byte, 5, buffArr, dataWidth, 4, 2);
        boneWeightAry = resGC.readByArrayBuffer(byte, 9, buffArr, dataWidth, 4, 1);

        self.meshData.indexs = indexs;

        self.meshData.stride = dataWidth * 4;

        self.readFrameQua(byte);

        super().setAllByteInfo(byte)

        self.upMeshToGpu(buffArr, np.array(indexs, dtype=np.int32))

    def upMeshToGpu(self, verBuffArr, indiceData):

        glBindVertexArray(glGenVertexArrays(1));
        self.meshData.vbo = glGenBuffers(1);
        glBindBuffer(GL_ARRAY_BUFFER, self.meshData.vbo);
        glBufferData(GL_ARRAY_BUFFER, verBuffArr.nbytes, verBuffArr, GL_STATIC_DRAW)

        glBindVertexArray(glGenVertexArrays(1));
        self.meshData.eboID = glGenBuffers(1)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self.meshData.eboID)
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, 4 * len(indiceData), indiceData, GL_STATIC_DRAW)

    def regShader(self):

        self.materialParam.shader = self.scene3D.progrmaManager.getMaterialProgram(
            Display3DBoneShader.Display3DBoneShader,
            Display3DBoneShader, self.materialParam.material);
        pass
