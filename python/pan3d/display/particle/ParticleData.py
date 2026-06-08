from ...core.Pan3dByteArray import Pan3dByteArray
from ...display.particle.ctrl.TimeLineData import TimeLineData
from ...display.particle.ctrl.TimeLine import TimeLine
from ...display.particle.Display3DParticle import Display3DParticle
from ...scene3D.Scene_data import Scene_data
from ...core.Vector3D import Vector3D
from ...base.ObjData import ObjData
from ...mateial.Material import Material
from ...mateial.MaterialParam import MaterialParam


class ParticleData:
    def __init__(self, scene):
        self.scene3D = scene
        self.timelineData: TimeLineData = None;
        self.beginTime: int = -1;
        self.delayedTime:int=0;
        self.width: float = 1;
        self.height: float = 1;
        self.widthFixed: bool = 1;
        self.heightFixed: bool = 1;
        self.originWidthScale: float;
        self.originHeightScale: float;
        self.eyeDistance: float;
        self.alphaMode: float;
        self.uSpeed: float;
        self.vSpeed: float;
        self.animLine: float;
        self.animRow: float;
        self.animInterval: float;
        self.renderPriority: float;
        self.distortion: bool;
        self.isUV: bool;
        self.isU: bool;
        self.isV: bool;
        self.life: float;
        self.watchEye: bool;
        self.ziZhuanAngly: Vector3D;
        self.rotationV3d: Vector3D;
        self.center: Vector3D;
        self.overAllScale: float;
        self.isZiZhuan = False;
        self.materialParamData: any = None;
        self.materialByteUrl: str = None;
        self.materialParam: MaterialParam = None;
        self.objData:ObjData=None

    def readMaterialPara(self, byte):
        self.materialParamData = {}
        materlUrl: str = byte.readUTF();
        texAryLen: int = byte.readInt();
        self.materialParamData['texAry'] = [];
        for i in range(texAryLen):
            temp: any = {};
            temp['isParticleColor'] = byte.readBoolean()
            temp['paramName'] = byte.readUTF()
            temp['url'] = byte.readUTF()
            if temp['isParticleColor']:
                temp['curve'] = {}
                self.readTempCurve(byte, temp['curve']);

            self.materialParamData['texAry'].append(temp)

        self.readMaterialParaConAry(byte)
        pass

    def readMaterialParaConAry(self, byte):
        arr: list = [];
        conAryLen: int = byte.readInt()
        for i in range(conAryLen):
            obj: any = {};
            obj['type'] = int(byte.readFloat());
            obj['indexID'] = int(byte.readFloat());
            obj['paramName'] = byte.readUTF();

            obj['curve'] = {};
            self.readTempCurve(byte, obj['curve'])

            arr.append(obj);

        self.materialParamData['conAry'] = arr
        pass

    def readTempCurve(self, byte, curve):
        curve['values'] = [];
        has: bool = False
        valuesLen: int = byte.readInt()
        if valuesLen > 0:
            scaleNum: float = byte.readFloat();

        for j in range(valuesLen):
            rgbLen: int = byte.readInt()
            valuesArr: list = [];
            for k in range(rgbLen):
                valuesArr.append(byte.readByte() / 127 * scaleNum)

            curve['values'].append(valuesArr)

        has = True

        curve['type'] = int(byte.readFloat());
        curve['maxFrame'] = byte.readFloat();
        curve['sideType'] = byte.readBoolean();
        curve['speedType'] = byte.readBoolean();
        curve['useColorType'] = byte.readBoolean();
        curve['items'] = self.readItems(byte);
        if not has:
            self.makeCurveData(curve)

        pass

    def makeCurveData(self, curve: any):
        print('需要补充')
        pass

    def readItems(self, byte):
        items: list = [];
        itemsLen: int = byte.readInt()
        for u in range(itemsLen):
            obj: any = {};
            obj['frame'] = byte.readInt();
            obj['vec3'] = byte.readVector3D(True)
            obj['rotation'] = byte.readVector3D(True)
            obj['rotationLeft'] = byte.readVector3D(True)
            items.append(obj);

        return items;

    def creatPartilce(self):
        particle: Display3DParticle = self.getParticle();
        particle.data = self;
        tl: TimeLine = TimeLine();
        tl.setAllDataInfo(self.timelineData);
        particle.setTimeLine(tl);
        particle.onCreated();

        return particle

    def getParticle(self):
        return None;

    def setAllByteInfo(self, byte: Pan3dByteArray):
        self.timelineData = TimeLineData();
        self.timelineData.setByteData(byte);
        self.beginTime = self.timelineData.beginTime;
        if self.version >= 15:
            self.delayedTime = byte.readFloat();

        self.width = byte.readFloat();
        self.height = byte.readFloat();
        self.widthFixed = byte.readBoolean();
        self.heightFixed = byte.readBoolean();
        self.originWidthScale = byte.readFloat();
        self.originHeightScale = byte.readFloat();
        self.eyeDistance = byte.readFloat();
        self.alphaMode = byte.readFloat();
        self.uSpeed = byte.readFloat();
        self.vSpeed = byte.readFloat();

        self.animLine = byte.readFloat();
        self.animRow = byte.readFloat();
        self.animInterval = byte.readFloat();
        self.renderPriority = byte.readFloat();

        self.distortion = byte.readBoolean();
        self.isUV = byte.readBoolean();
        self.isU = byte.readBoolean();
        self.isV = byte.readBoolean();

        self.life = byte.readFloat();
        if self.life > 10000:
            self.life = Scene_data.MAX_NUMBER;

        self.watchEye = byte.readBoolean();

        self.ziZhuanAngly = Vector3D();
        self.ziZhuanAngly.x = byte.readFloat();
        self.ziZhuanAngly.y = byte.readFloat();
        self.ziZhuanAngly.z = byte.readFloat();
        self.ziZhuanAngly.w = byte.readFloat();

        self.rotationV3d = Vector3D()
        self.rotationV3d.x = byte.readFloat();
        self.rotationV3d.y = byte.readFloat();
        self.rotationV3d.z = byte.readFloat();

        self.center = Vector3D();
        self.center.x = byte.readFloat();
        self.center.y = byte.readFloat();
        self.center.z = byte.readFloat();
        self.center.w = byte.readFloat();

        self.overAllScale = byte.readFloat();

        if self.ziZhuanAngly.x != 0 or self.ziZhuanAngly.y != 0 or self.ziZhuanAngly.z != 0:
            self.isZiZhuan = True;

        self.readMaterialPara(byte);
        strMaterialUrl: str = byte.readUTF();
        strMaterialUrl = strMaterialUrl.replace("_byte.txt", ".txt");
        strMaterialUrl = strMaterialUrl.replace(".txt", "_byte.txt");
        self.setMaterialByteUrl(strMaterialUrl);
        pass

    def setMaterialByteUrl(self, value: str):
        self.materialByteUrl = value;
        self.scene3D.materialManager.getMaterialByte(value, self.onMaterialLoad)
        pass

    def onMaterialLoad(self, material: Material):
        self.materialParam = MaterialParam(self.scene3D);
        self.materialParam.setMaterial(material);
        self.materialParam.setLife(self.life);

        if self.materialParamData:
            self.materialParam.setTextObj(self.materialParamData['texAry']);
            self.materialParam.setConstObj(self.materialParamData['conAry']);

        self.scene3D.materialManager.loadDynamicTexUtil(self.materialParam);
        self.regShader();
        pass

    def regShader(self):
        pass
