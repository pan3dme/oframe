from ..base.ResGC import ResGC
from ..base.ObjData import ObjData
from ..core.Pan3dByteArray import Pan3dByteArray
import numpy as np


class ObjDataManager(ResGC):
    def __init__(self, scene):
        super().__init__(scene);
        self.loadDic = {}

    def getObjData(self, url, bfun):
        if url in self.dic:
            bfun(self.dic[url]);
            return

    def loadObjCom(self, byte: Pan3dByteArray, url: str):

        if url in self.dic:
            return self.dic[url]

        objData = ObjData(self.scene3D);
        version = byte.readInt()
        str = byte.readUTF();
        self.readObj2OneBuffer(byte, objData)

        self.dic[url] = objData;
        if url in self.loadDic:
            ary = self.loadDic[url];
            for i in range(len(ary)):
                ary[i](self.dic[url])

        return objData

    def readObj2OneBuffer(self, byte: Pan3dByteArray, objData: ObjData):

        dataWidth: int = 0
        typeItem = [];
        for i in range(6):
            tf = byte.readBoolean();
            typeItem.append(tf)
            if i == 0:
                dataWidth += 2;
            elif i == 1:
                dataWidth += 2;
            else:
                dataWidth += 3;

        buflen = int(byte.readFloat());
        buflen *= dataWidth * 4;

        uvsOffsets: int = 3;

        lightuvsOffsets: int = uvsOffsets + 2;
        if typeItem[2]:
            normalsOffsets: int = lightuvsOffsets + 2;
        else:
            normalsOffsets: int = uvsOffsets + 2;

        tangentsOffsets: int = normalsOffsets + 3;
        bitangentsOffsets: int = tangentsOffsets + 3;

        buffArr = np.zeros(buflen, dtype=np.float32)

        objData.vertices = self.readByArrayBuffer(byte,0, buffArr,dataWidth,3)
        objData.uvs = self.readByArrayBuffer(byte, uvsOffsets,buffArr,dataWidth,2)
        objData.lightuv = self.readByArrayBuffer(byte, lightuvsOffsets,buffArr,dataWidth,2, 1)
        objData.nors = self.readByArrayBuffer(byte,normalsOffsets,buffArr, dataWidth,3, )
        objData.tangents = self.readByArrayBuffer(byte,tangentsOffsets, buffArr,dataWidth,3, )
        objData.bitangents = self.readByArrayBuffer(byte, bitangentsOffsets,buffArr,dataWidth,3, )
        objData.indexs = self.readIntForTwoByte(byte)

        objData.buffArr=buffArr;
        objData.stride = dataWidth * 4;
        objData.uvsOffsets = uvsOffsets * 4;
        objData.lightuvsOffsets = lightuvsOffsets * 4;



        pass

