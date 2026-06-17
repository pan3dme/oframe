from ..core.Pan3dByteArray import Pan3dByteArray
from ..core.Vector3D import Vector3D


class ParamDataVo(Vector3D):
    def __init__(self):
        super().__init__()
        self.name: str = ''
        self.type: int = 0;


class ResGC:
    def __init__(self, scene):
        self.scene3D = scene
        self.dic = {}
        pass

    def readMaterialParamData(self, byte: Pan3dByteArray):
        mpNum: int = byte.readInt();
        if mpNum > 0:
            mpAry = [];
            for j in range(mpNum):
                obj = ParamDataVo()
                obj.name = byte.readUTF();
                obj.type = byte.readByte();
                if obj.type == 0:
                    obj.url = byte.readUTF();
                elif obj.type == 1:
                    obj.x = byte.readFloat();
                elif obj.type == 2:
                    obj.x = byte.readFloat();
                    obj.y = byte.readFloat();
                elif obj.type == 3:
                    obj.x = byte.readFloat();
                    obj.y = byte.readFloat();
                    obj.z = byte.readFloat();
                mpAry.append(obj);
            return mpAry

        return None

    def readIntForTwoByte(self, byte: Pan3dByteArray):
        indexs = []
        iLen: int = byte.readInt();
        for j in range(iLen):
            indexs.append(byte.readShort());

        return indexs;

    def readByArrayBuffer(self, byte: Pan3dByteArray, offset: int, dataArr: list, dataWidth: int, dw: int,
                          readType: int = 0):
        backArr = []
        verLength: int = byte.readInt();
        if verLength <= 0:
            return backArr

        scaleNum = 1.0;
        if readType == 0:
            scaleNum = byte.readFloat();

        readNum: int = int(verLength / dw);
        for i in range(readNum):

            pos: int = dataWidth * i + offset;
            for j in range(dw):
                if readType == 0:
                    num = byte.readFloatTwoByte(scaleNum);
                elif readType == 1:
                    num = byte.readFloatOneByte();
                elif readType == 2:
                    num = byte.readByte();
                elif readType == 3:
                    num = (byte.readByte() + 128.0) / 255.0;
                elif readType == 4:
                    num = byte.readFloat();
                else:
                    print('没有对应的数据类型要处理')
                    pass
                backArr.append(num);
                dataArr[pos + j] = num;

        return backArr

        return [];

    def readBytes2ArrayBuffer(self, byte: Pan3dByteArray, dataWidth: int, readType: int = 0):
        backArr = []
        verLength: int = byte.readInt();
        if verLength <= 0:
            return backArr

        scaleNum = 1.0;
        if readType == 0:
            scaleNum = byte.readFloat();

        readNum: int = int(verLength / dataWidth);
        for i in range(readNum):
            for j in range(dataWidth):
                if readType == 0:
                    num = byte.readFloatTwoByte(scaleNum);
                    pass
                elif readType == 1:
                    num = byte.readFloatOneByte();
                elif readType == 2:
                    num = byte.readByte();
                elif readType == 3:
                    num = (byte.readByte() + 128.0) / 255.0;
                else:
                    print('没有对应的数据类型要处理')
                    pass

                backArr.append(num);

        return backArr

        return [];
