from ..base.ObjData import ObjData
from ..core.Matrix3D import Matrix3D
from ..core.Quaternion import QuaternionPan3d
from OpenGL.GL import *
import numpy as np

class MeshData(ObjData):
    def __init__(self, scene):
        super().__init__(scene)

        self.boneIDAry : array = None
        self.boneWeightAry : array =None
        self.boneNewIDAry:array = []
        self.bindPosAry: array = []
        self.materialParamData: array = []
        self.materialUrl: str = ''
        self.bindPosMatrixAry= []
        self.compressBuffer:bool = True;
        self.uvsOffsets:int = 0
        self.normalsOffsets:int = 0
        self.tangentsOffsets :int= 0
        self.bitangentsOffsets:int = 0
        self.boneIDOffsets:int = 0
        self.boneWeightOffsets:int = 0
        self.stride:int = 0
        self.treNum:int=0;

    def getBindPosMatrix(self):
        ary  = [];
        invertAry= [];
        for  i in range(len(self.bindPosAry)):
            objbone  = self.bindPosAry[i];

            OldQ: QuaternionPan3d =   QuaternionPan3d(objbone[0], objbone[1], objbone[2]);
            OldQ.setMd5W();
            newM: Matrix3D = OldQ.toMatrix3D();

            newM.appendTranslation(objbone[3], objbone[4], objbone[5]);
            invertAry.append(newM.clone());
            newM.invert();
            ary.append(newM);

        self.bindPosMatrixAry = ary;
        self.bindPosInvertMatrixAry = invertAry;

    def upToGPU(self):
        arr=self.getVectUvBuffArr()

        self.vertex_count = int(len(arr) / 5)
        self.vertices = np.array(arr, dtype=np.float32);

        self.vao = glGenVertexArrays(1);
        glBindVertexArray(self.vao);

        self.vbo = glGenBuffers(1);
        glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
        glBufferData(GL_ARRAY_BUFFER, self.vertices.nbytes, self.vertices, GL_STATIC_DRAW)

        glEnableVertexAttribArray(0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 52, ctypes.c_void_p(0));
        glEnableVertexAttribArray(1)
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 52, ctypes.c_void_p(12));
        glEnableVertexAttribArray(2)
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 52, ctypes.c_void_p(20));
        glEnableVertexAttribArray(3)
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 52, ctypes.c_void_p(36));

    def getVectUvBuffArr(self):
        arr = []
        for i in range(len(self.indexs)):
            idx: int = self.indexs[i]


            arr.append(self.vertices[idx * 3 + 0])
            arr.append(self.vertices[idx * 3 + 1])
            arr.append(self.vertices[idx * 3 + 2])

            arr.append(self.uvs[idx * 2 + 0])
            arr.append(self.uvs[idx * 2 + 1])

            arr.append(self.boneIDAry[idx * 4 + 0])
            arr.append(self.boneIDAry[idx * 4 + 1])
            arr.append(self.boneIDAry[idx * 4 + 2])
            arr.append(self.boneIDAry[idx * 4 + 3])

            arr.append(self.boneWeightAry[idx * 4 + 0])
            arr.append(self.boneWeightAry[idx * 4 + 1])
            arr.append(self.boneWeightAry[idx * 4 + 2])
            arr.append(self.boneWeightAry[idx * 4 + 3])

        return arr;





