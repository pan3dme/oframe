from ..res.ResCount import ResCount
from ..core.Pan3dByteArray import Pan3dByteArray
from io import BytesIO
from ..mateial.MaterialVo import MaterialVo


class BaseRes(ResCount):
    IMG_TYPE: int = 1
    OBJS_TYPE: int = 2
    MATERIAL_TYPE: int = 3
    PARTICLE_TYPE: int = 4;
    SCENE_TYPE: int = 4;
    ZIP_OBJS_TYPE: int = 6;

    PREFAB_TYPE: int = 1
    SCENE_PARTICLE_TYPE: int = 11

    def __init__(self, scene):
        super().__init__(scene)
        self.byte: Pan3dByteArray = Pan3dByteArray()
        self.version: int = 0;
        pass

    def readMaterialInfo(self):
        len: int = self.byte.readInt();
        if len > 0:
            arr = [];
            for i in range(len):
                temp: MaterialVo = MaterialVo();
                temp.type = self.byte.readInt()
                temp.name = self.byte.readUTF()
                if temp.type == 0:
                    temp.url = self.byte.readUTF();
                if temp.type == 1:
                    temp.x = self.byte.readUTF();
                if temp.type == 2:
                    temp.x = self.byte.readFloat();
                    temp.y = self.byte.readFloat();
                if temp.type == 3:
                    temp.x = self.byte.readFloat();
                    temp.y = self.byte.readFloat();
                    temp.z = self.byte.readFloat();
                arr.append(temp);

                return arr

            else:
                return None

    def readImg(self):
        imgNum = self.byte.readInt();
        for i in range(imgNum):
            url: str = self.byte.readUTF();
            print(url)
            imgSize: int = self.byte.readInt()
            imgByte = self.byte.file.read(imgSize);

            # material = Material();
            # material.imageToTexTure(imgByte);
            # self.scene3D.testTexture = material;

            self.scene3D.textureManager.addRes(url, imgByte)

        pass

    def readObj(self, srcByte):

        objNum: int = srcByte.readInt();
        for i in range(objNum):
            url: str = srcByte.readUTF();
            objbyteSize: int = srcByte.readInt();

            newByte = Pan3dByteArray()
            newByte.file = BytesIO(srcByte.file.read(objbyteSize));

            self.scene3D.objDataManager.loadObjCom(newByte, url);

        pass

    def readParticle(self):
        particNum = self.byte.readInt();
        for i in range(particNum):
            url: str = self.byte.readUTF();
            size: int = self.byte.readInt();
            newByte = Pan3dByteArray()
            newByte.file = BytesIO(self.byte.file.read(size));
            self.scene3D.particleManager.addResByte(url, newByte);

    def readMaterial(self):
        matNum = self.byte.readInt();
        for i in range(matNum):
            url: str = self.byte.readUTF();
            size: int = self.byte.readInt();
            newByte = Pan3dByteArray()
            newByte.file = BytesIO(self.byte.file.read(size));
            self.scene3D.materialManager.addResByte(url, newByte)

    def readZipObj(self):
        newByte: Pan3dByteArray = self.byte.getZipByte()

        # objNum: int = newByte.readInt();
        self.readObj(newByte);
        pass

    def read(self, bfun=None):
        fileType = self.byte.readInt()
        if fileType == BaseRes.IMG_TYPE:
            self.readImg()
            if bfun:  bfun()
        elif fileType == BaseRes.OBJS_TYPE:
            self.readObj(self.byte)
        elif fileType == BaseRes.MATERIAL_TYPE:
            self.readMaterial()
        elif fileType == BaseRes.PARTICLE_TYPE:
            self.readParticle()
        elif fileType == BaseRes.ZIP_OBJS_TYPE:
            self.readZipObj()

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
                else:
                    print('没有对应的数据类型要处理')
                    pass

                backArr.append(num);

        return backArr

        return [];
