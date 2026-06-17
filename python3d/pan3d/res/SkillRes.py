from ..res.BaseRes import BaseRes
from ..core.Vector3D import Vector3D
from ..core.Pan3dByteArray import Pan3dByteArray
from io import BytesIO


class SkillRes(BaseRes):
    def __init__(self, scene):
        super().__init__(scene)
        self.skillUrl: str = None;
        self.fun: any = None;
        self.meshBatchNum: int = None;
        self.data: any = None;

    def load(self, url, bfun):
        self.bFun = bfun;
        with open(url, 'rb') as file:
            self.loadComplete(file)
        pass

    def loadComplete(self, file):
        self.byte.file = file;
        self.version = self.byte.readInt();
        self.skillUrl = self.byte.readUTF();
        self.read();
        self.readNext();
        pass

    def readNext(self):
        self.read();
        self.read();

        if self.version < 27:
            strinfo: str = self.byte.readUTF();

        self.data = self.readData(self.byte)
        self.bFun()
        pass

    def readData(self, byte: Pan3dByteArray):
        lenNum: int = byte.readInt();
        byteData: any = {}
        for i in range(lenNum):
            obj: any = {}
            name: str = byte.readUTF()
            action: str = byte.readUTF();
            obj['skillname'] = name;
            obj['action'] = action;
            obj['type'] = byte.readFloat();
            obj['blood'] = byte.readInt();
            soundTime: int = byte.readInt();
            if soundTime > 0:
                soundName: str = byte.readUTF();
                obj['sound'] = {'time': soundTime, 'name': soundName};

            shockLen: int = byte.readInt();
            if shockLen:
                shockAry: list = []
                for k in range(shockLen):
                    shobj: any = {};
                    shobj['time'] = byte.readInt();
                    shobj['lasttime'] = byte.readInt();
                    shobj['amp'] = byte.readFloat();
                    shockAry.append(shobj);

                obj['shock'] = shockAry;

            obj['data'] = []
            dLen: int = byte.readInt()
            for j in range(dLen):
                dataObj: any = {};
                dataObj['url'] = byte.readUTF()
                dataObj['frame'] = byte.readFloat()
                if obj['type'] == 1:
                    dataObj['beginType'] = byte.readInt();
                    if dataObj['beginType'] == 0:
                        dataObj['beginPos'] = Vector3D(byte.readFloat(), byte.readFloat(), byte.readFloat());
                    elif dataObj['beginType'] == 1:
                        dataObj['beginSocket'] = byte.readUTF();

                    dataObj['hitSocket'] = byte.readUTF();
                    dataObj['endParticle'] = byte.readUTF();

                    dataObj['multype'] = byte.readInt();
                    dataObj['speed'] = byte.readFloat();

                if obj['type'] == 3:
                    dataObj['beginSocket'] = byte.readUTF()
                    dataObj['beginType'] = byte.readFloat()
                    dataObj['multype'] = byte.readFloat()
                    dataObj['speed'] = byte.readFloat()

                    break;
                if obj['type'] == 4:

                    hasSocket: bool = byte.readBoolean();
                    dataObj['hasSocket'] = hasSocket;
                    if hasSocket:
                        dataObj['socket'] = byte.readUTF();
                    else:
                        dataObj['pos'] = self.readV3d(byte);
                        dataObj['rotation'] = self.readV3d(byte);

                obj['data'].append(dataObj)

            byteData[name] = obj

        return byteData

    def readV3d(self, byte: Pan3dByteArray):
        v3d = Vector3D();
        v3d.x = byte.readFloat();
        v3d.y = byte.readFloat();
        v3d.z = byte.readFloat();
        v3d.w = byte.readFloat();
        return v3d;
