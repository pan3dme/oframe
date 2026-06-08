import math
import json

from ..res.BaseRes import BaseRes
from ..core.Vector3D import Vector3D
from ..core.Pan3dByteArray import Pan3dByteArray
from io import BytesIO


class SceneRes(BaseRes):
    def __init__(self, scene):
        super().__init__(scene)
        self.sceneData:dict=None;

    def load(self, url, completeFun):

        with open(url, 'rb') as file:
            self.loadComplete(file)

        completeFun()

    def loadComplete(self, file):
        self.byte.file = file;
        self.version = self.byte.readInt();
        self.read(self.readNext)

    def readNext(self):
        self.read();
        self.read();
        self.read();
        self.readScene();

    def readScene(self):
        types: int = self.byte.readInt();
        self.readAstat()
        self.readTerrainIdInfoBitmapData(self.byte)

        size: int = self.byte.readInt();
        self.sceneData = json.loads(self.byte.readUTFBytes(size))



        pass

    def readAstat(self):

        hasAstat: bool = self.byte.readBoolean();

        if hasAstat:
            self.byte.readFloat();
            self.byte.readFloat();
            self.byte.readFloat();
            self.byte.readFloat();
            i: int = 0;
            j: int = 0;
            tw: int = self.byte.readInt();
            th: int = self.byte.readInt();

            heightScaleNum: float = self.byte.readFloat();
            astrBase: list = self.readAstarFromByte(self.byte);
            jumpBase: list = self.readAstarFromByte(self.byte);

            astrBaseId: int = 0;
            jumpBaseId: int = 0;

            for i in range(th):
                for j in range(tw):
                    self.byte.readShort()

    def readAstarFromByte(self, byte):
        lenNum: int = byte.readUnsignedInt();
        intLen: int = math.ceil(lenNum / 32)
        backArr: list = [];
        for i in range(intLen):
            num: int = byte.readUnsignedInt();
            backArr.append(num)

        return backArr

    def readTerrainIdInfoBitmapData(self, byte):
        lenNum: int = byte.readInt();
        if lenNum > 0:
            newByte: Pan3dByteArray = byte.getZipByte();
            pass

        pass
