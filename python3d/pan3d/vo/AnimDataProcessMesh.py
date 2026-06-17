from ..vo.SkinMesh import SkinMesh
from ..vo.AnimData import AnimData
from ..vo.DualQuatFloat32Array import DualQuatFloat32Array
from ..core.Vector3D import Vector3D
from ..core.Matrix3D import Matrix3D
from ..core.Quaternion import QuaternionPan3d


class AnimDataProcessMesh:
    def __init__(self, data: AnimData):

        self.data = data;
        # self.meshBoneQPAryDic = {};

        pass

    def processMesh(self, skinMesh: SkinMesh):
        if self.data.hasProcess:
            return;
        self.makeArrBoneQPAry(skinMesh);
        self.data.hasProcess = True;

    def makeArrBoneQPAry(self, skinMesh: SkinMesh):
        self.data.meshBoneQPAryDic = {}
        for k in range(len(skinMesh.meshAry)):
            conleM = self.conleMatrixArr();
            for i in range(len(conleM)):
                frameAry = conleM[i];

                for j in range(len(frameAry)):
                    frameAry[j].prepend(skinMesh.meshAry[k].bindPosMatrixAry[j]);

            temp = self.makeFrameDualQuatFloatArray(skinMesh, conleM);
            self.data.meshBoneQPAryDic[skinMesh.meshAry[k].uid] = temp;
            self.data.boneQPAry = temp;

    def makeFrameDualQuatFloatArray(self, skinMesh: SkinMesh, matrixAry):

        backArr = [];
        tempMatrix: Matrix3D = Matrix3D()

        for i in range(len(skinMesh.meshAry)):
            frameDualQuat = [];
            newIDBoneArr = skinMesh.meshAry[i].boneNewIDAry;
            for j in range(len(matrixAry)):

                baseBone = matrixAry[j];

                qualQuatFloat32Array: DualQuatFloat32Array = DualQuatFloat32Array()

                qualQuatFloat32Array.quat = [0 for _ in range(len(newIDBoneArr) * 4)]
                qualQuatFloat32Array.pos = [0 for _ in range(len(newIDBoneArr) * 3)]

                for k in range(len(newIDBoneArr)):
                    m: Matrix3D = baseBone[newIDBoneArr[k]].clone(tempMatrix);

                    m.appendScale(-1, 1, 1)

                    q: QuaternionPan3d = QuaternionPan3d()
                    q.fromMatrix(m)

                    p: Vector3D = m.position();
                    qualQuatFloat32Array.quat[k * 4 + 0] = q.x
                    qualQuatFloat32Array.quat[k * 4 + 1] = q.y
                    qualQuatFloat32Array.quat[k * 4 + 2] = q.z
                    qualQuatFloat32Array.quat[k * 4 + 3] = q.w

                    qualQuatFloat32Array.pos[k * 3 + 0] = p.x;
                    qualQuatFloat32Array.pos[k * 3 + 1] = p.y;
                    qualQuatFloat32Array.pos[k * 3 + 2] = p.z;

                frameDualQuat.append(qualQuatFloat32Array);
            backArr.append(frameDualQuat);

        return backArr

    def conleMatrixArr(self):
        arr = [];

        for i in range(len(self.data.matrixAry)):
            frameAry = self.data.matrixAry[i];

            temp = []
            for j in range(len(frameAry)):
                temp.append(frameAry[j].clone())
            arr.append(temp)

        return arr
