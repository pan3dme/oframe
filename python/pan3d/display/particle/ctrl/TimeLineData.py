from ....core.Pan3dByteArray import Pan3dByteArray
from ....display.particle.ctrl.KeyFrame import KeyFrame
from ....scene3D.Scene_data import Scene_data
from ....core.Vector3D import Vector3D


class TimeLineData:
    def __init__(self):
        self.dataAry: list = [];
        self.beginTime: int = 0;
        self.maxFrameNum: int = 0;
        self.delayedTime: int = 0;
        pass

    def addKeyFrame(self, num: int):
        keyframe: KeyFrame = KeyFrame();
        keyframe.frameNum = num;
        self.dataAry.append(keyframe);
        return keyframe;
        pass

    def getByteDataTemp(self, byte: Pan3dByteArray):
        obj: any = {}
        animType: int = byte.readInt();
        dataLen: int = byte.readInt();
        obj['data'] = [];
        obj['dataByte'] = [];
        for i in range(dataLen):
            ko:any = {};
            ko['type'] = byte.readInt();

            if ko['type'] == 1:
                obj['dataByte'].append(byte.readFloat());

            if ko['type'] == 2:
                v: Vector3D = Vector3D();
                v.x = byte.readFloat();
                v.y = byte.readFloat();
                v.z = byte.readFloat();
                obj['dataByte'].append(v);

        obj['type'] = animType;

        return obj

    def setByteData(self, byte: Pan3dByteArray):
        lenNum: int = int(byte.readFloat());

        for i in range(lenNum):
            frameNum: int = int(byte.readFloat());
            key: KeyFrame = self.addKeyFrame(frameNum);
            key.frameNum = frameNum;
            key.baseValue = [];

            for j in range(10):
                key.baseValue.append(byte.readFloat());

            animLen: int = int(byte.readFloat());
            key.animData = [];
            if animLen > 0:
                for k in range(animLen):
                    key.animData.append(self.getByteDataTemp(byte));

        self.maxFrameNum = self.dataAry[len(self.dataAry) - 1].frameNum;
        self.beginTime = self.dataAry[0].frameNum * Scene_data.frameTime;

        pass
