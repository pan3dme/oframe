from ..core.Pan3dByteArray import Pan3dByteArray
from ..mateial.TexItem import TexItem
from ..mateial.ConstItem import ConstItem
from ..core.Vector3D import Vector3D
from ..program.Shader3D import Shader3D
from ..res.ResCount import ResCount


class Material(ResCount):

    def __init__(self, scene):
        super().__init__(scene)
        self.shader: Shader3D = None;
        self.shaderStr: str = '';
        self.hasTime: bool = False;
        self.timeSpeed: float = 0.0;
        self.blendMode: float = 0.0;
        self.backCull: bool = False;
        self.killNum: float = 0.0;
        self.hasVertexColor: bool = False;
        self.usePbr: bool = False;
        self.useNormal: bool = False;
        self.roughness: float = 0.0;
        self.writeZbuffer: bool = False;
        self.hasFresnel: bool = False;
        self.useDynamicIBL: bool = False;
        self.normalScale: float = 0.0;
        self.lightProbe: bool = False;
        self.useKill: bool = False;
        self.directLight: bool = False;
        self.noLight: bool = False;
        self.scaleLightMap: bool = False;
        self.fogMode: int = 0;
        self.fcNum: int = 0;
        self.fcIDAry: list = None;
        self.hasParticleColor: bool = False;
        self.texList = [];
        self.constList = [];
        self.fcData = [0, 0, 0, 0];

    def setByteData(self, byte: Pan3dByteArray):

        fs: Pan3dByteArray = byte;

        vesion: int = fs.readInt();

        self.shaderStr = fs.readUTF()
        self.hasTime = fs.readBoolean()
        self.timeSpeed = fs.readFloat()
        self.blendMode = fs.readFloat()
        self.backCull = fs.readBoolean()
        self.killNum = fs.readFloat()
        self.hasVertexColor = fs.readBoolean()
        self.usePbr = fs.readBoolean()
        self.useNormal = fs.readBoolean()
        self.roughness = fs.readFloat()
        self.writeZbuffer = fs.readBoolean()
        self.hasFresnel = fs.readBoolean()
        self.useDynamicIBL = fs.readBoolean()
        self.normalScale = fs.readFloat()
        self.lightProbe = fs.readBoolean()
        self.useKill = fs.readBoolean()
        self.directLight = fs.readBoolean()
        self.noLight = fs.readBoolean()
        self.scaleLightMap = fs.readBoolean()

        self.fogMode = fs.readInt();
        self.fcNum = fs.readByte();
        leg: int = fs.readByte();
        self.fcIDAry = [];
        for i in range(leg):
            self.fcIDAry.append(fs.readByte());

        self.hasParticleColor = False

        self.initFcData()
        self.readTexList(fs)
        self.readConstLis(fs)

    def readTexList(self, fs: Pan3dByteArray):

        texListLen: int = fs.readInt();
        for i in range(texListLen):
            texItem: TexItem = TexItem()
            texItem.id = int(fs.readFloat())
            texItem.url = fs.readUTF()
            texItem.isDynamic = fs.readBoolean()
            texItem.paramName = fs.readUTF()
            texItem.isMain = fs.readBoolean()
            texItem.isParticleColor = fs.readBoolean()
            texItem.type = int(fs.readFloat())
            texItem.wrap = fs.readFloat()
            texItem.filter = fs.readFloat()
            texItem.mipmap = fs.readFloat()

            if texItem.isParticleColor:
                self.hasParticleColor = True;

            self.texList.append(texItem);

        pass

    def readConstLis(self, fs: Pan3dByteArray):
        constLisLen: int = fs.readInt();
        self.constList = [];
        for i in range(constLisLen):
            constItem: ConstItem = ConstItem();

            constItem.id = fs.readFloat()
            constItem.value = Vector3D(fs.readFloat(), fs.readFloat(), fs.readFloat(), fs.readFloat());

            constItem.paramName0 = fs.readUTF()
            constItem.param0Type = int(fs.readFloat())
            constItem.param0Index = int(fs.readFloat())

            constItem.paramName1 = fs.readUTF()
            constItem.param1Type = int(fs.readFloat())
            constItem.param1Index = int(fs.readFloat())

            constItem.paramName2 = fs.readUTF()
            constItem.param2Type = int(fs.readFloat())
            constItem.param2Index = int(fs.readFloat())

            constItem.paramName3 = fs.readUTF()
            constItem.param3Type = int(fs.readFloat())
            constItem.param3Index = int(fs.readFloat())
            constItem.creat(self.fcData);

            self.constList.append(constItem);

        pass

    def update(self, t: int):
        self.updateTime(t);
        self.updateCam(self.scene3D.camera3D.x / 200, self.scene3D.camera3D.y / 200, self.scene3D.camera3D.z / 200);
        self.updateScene();

    def updateScene(self):
        pass

    def updateTime(self, t: int):
        if self.hasTime:
            self.fcData[1] = t;

    def updateCam(self, x: float, y: float, z: float):
        if self.usePbr or self.fogMode == 1:
            idx: int = self.fcIDAry[0] * 4;
            self.fcData[0 + idx] = x;
            self.fcData[1 + idx] = y;
            self.fcData[2 + idx] = z;

    def initFcData(self):
        self.fcData = [0 for _ in range(self.fcNum * 4)]
        if self.useKill:
            self.fcData[0] = self.killNum;

        pass
