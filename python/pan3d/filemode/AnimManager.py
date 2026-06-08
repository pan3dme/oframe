from ..base.ResGC import ResGC
from ..core.Quaternion import QuaternionPan3d
from ..core.Matrix3D import Matrix3D
from ..core.Vector3D import Vector3D
from ..vo.AnimData import AnimData
from ..vo.SkinMesh import SkinMesh
from ..vo.AnimDataProcessMesh import AnimDataProcessMesh
from ..vo.ObjectBaseBone import ObjectBaseBone
from ..vo.ObjectBone import ObjectBone
from ..core.Pan3dByteArray import Pan3dByteArray
import math


class AnimManager(ResGC):
    def __init__(self, scene):
        super().__init__(scene)

    def getAnimDataImmediate(self, url: str):
        if url in self.dic:
            return self.dic[url];
        else:
            print('没有的数据')
            return None

    def makeAnimDataProcessAction(self, animData: AnimData, skinMesh: SkinMesh):

        if animData.isProcessAction:
            return
        animData.isProcessAction = True;
        animData.matrixAry = self.processFrame(animData.frameAry, animData.hierarchyList);


        AnimDataProcessMesh(animData).processMesh(skinMesh);

    def processFrame(self, frameAry, hierarchyList):

        newFrameAry = []
        for i in range(len(frameAry)):
            newFrameAry.append(self.frameToBone(frameAry[i], hierarchyList));
        return self.setFrameToMatrix(newFrameAry);

    def setFrameToMatrix(self, frameAry):
        matrixAry = [];
        for j in range(len(frameAry)):
            boneAry = frameAry[j];

            Q0: QuaternionPan3d;
            newM: Matrix3D;
            frameMatrixAry = [];
            matrixAry.append(frameMatrixAry);

            for i in range(len(boneAry)):
                xyzfarme0: ObjectBaseBone = boneAry[i];

                Q0 = QuaternionPan3d(xyzfarme0.qx, xyzfarme0.qy, xyzfarme0.qz);
                Q0.w = self.getW(Q0.x, Q0.y, Q0.z);

                if xyzfarme0.father == -1:
                    newM = Q0.toMatrix3D();
                    newM.appendTranslation(xyzfarme0.tx, xyzfarme0.ty, xyzfarme0.tz);
                    newM.appendRotation(-90, Vector3D.X_AXIS);

                else:
                    newM = Q0.toMatrix3D();
                    newM.appendTranslation(xyzfarme0.tx, xyzfarme0.ty, xyzfarme0.tz);
                    newM.append(frameMatrixAry[xyzfarme0.father]);

                # newM.outStr()
                frameMatrixAry.append(newM);

            for i in range(len(frameMatrixAry)):
                frameMatrixAry[i].appendScale(-1, 1, 1)



        return matrixAry

    def getW(self, x, y, z):
        t = 1 - (x * x + y * y + z * z);
        if t < 0:
            t = 0
        else:
            t = -math.sqrt(t);
        return t;

    def frameToBone(self, frameData, hierarchyList):
        arr = []
        for i in range(len(hierarchyList)):
            _temp = ObjectBaseBone()
            _temp.father = hierarchyList[i].father;
            k: int = 0;
            if hierarchyList[i].changtype & 1:
                _temp.tx = frameData[hierarchyList[i].startIndex + k];
                k += 1;
            else:
                _temp.tx = hierarchyList[i].tx;

            if hierarchyList[i].changtype & 2:
                _temp.ty = frameData[hierarchyList[i].startIndex + k];
                k += 1;
            else:
                _temp.ty = hierarchyList[i].ty;

            if hierarchyList[i].changtype & 4:
                _temp.tz = frameData[hierarchyList[i].startIndex + k];
                k += 1;
            else:
                _temp.tz = hierarchyList[i].tz;

            if hierarchyList[i].changtype & 8:
                _temp.qx = frameData[hierarchyList[i].startIndex + k];
                k += 1;
            else:
                _temp.qx = hierarchyList[i].qx;

            if hierarchyList[i].changtype & 16:
                _temp.qy = frameData[hierarchyList[i].startIndex + k];
                k += 1;
            else:
                _temp.qy = hierarchyList[i].qy;

            if hierarchyList[i].changtype & 32:
                _temp.qz = frameData[hierarchyList[i].startIndex + k];
                k += 1;
            else:
                _temp.qz = hierarchyList[i].qz;

            arr.append(_temp);
        return arr;

    def readData(self, byte: Pan3dByteArray, url: str):
        animData = AnimData()
        # animData.init()
        animData.name = url;
        animData.hierarchyList = []
        animData.frameAry = []
        hierarchyList = animData.hierarchyList;
        frameAry = animData.frameAry;

        animData.inLoop = byte.readInt();
        interLen: int = byte.readInt();
        for i in range(interLen):
            animData.inter.append(byte.readInt());

        boundsLen: int = byte.readInt();
        for j in range(boundsLen):
            animData.bounds.append(byte.readInt());

        animData.nameHeight = byte.readInt();
        boneLen = byte.readInt();
        for k in range(boneLen):
            objBone: ObjectBone = ObjectBone();
            objBone.father = byte.readInt();
            objBone.changtype = byte.readInt();
            objBone.startIndex = byte.readInt();

            objBone.tx = byte.readFloat();
            objBone.ty = byte.readFloat();
            objBone.tz = byte.readFloat();

            objBone.qx = byte.readFloat();
            objBone.qy = byte.readFloat();
            objBone.qz = byte.readFloat();
            hierarchyList.append(objBone);

        self.readFrameData(byte, frameAry);

        posLen = byte.readInt();
        for o in range(posLen):
            animData.posAry.append(byte.readVector3D());

        self.dic[url] = animData;
        return animData;

    def readFrameTypeData(self, byte):
        arr = []
        numLength: int = byte.readInt();
        for i in range(numLength):
            arr.append(byte.readBoolean())

        return arr

    def readFrameData(self, byte, frameAry):
        frameTyeArr = self.readFrameTypeData(byte)
        isStand: bool = byte.readBoolean()
        scaleNum: float = byte.readFloat();
        numLength: int = byte.readInt();
        for i in range(numLength):
            frameItemAryLength: int = byte.readInt();
            frameItemAry = []
            frameAry.append(frameItemAry);
            for j in range(frameItemAryLength):
                if frameTyeArr[j]:
                    frameItemAry.append(byte.readFloatTwoByte(scaleNum))
                else:
                    if isStand:
                        frameItemAry.append(byte.readFloat())
                    else:
                        frameItemAry.append(byte.readShort() / 32767.0)
