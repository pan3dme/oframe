from ....core.Matrix3D import Matrix3D
from ....event.EventDispatcher import EventDispatcher
from ....display.particle.ctrl.TimeLineData import TimeLineData
from ....display.particle.ctrl.KeyFrame import KeyFrame
from ....display.particle.ctrl.AxisMove import AxisMove
from ....display.particle.ctrl.AxisRotaion import AxisRotaion
from ....display.particle.ctrl.SelfRotation import SelfRotation
from ....core.Vector3D import Vector3D
from ....scene3D.Scene_data import Scene_data


class TimeLine(EventDispatcher):
    def __init__(self):
        super().__init__();
        self.isByteData: bool;
        self.maxFrameNum: float;
        self.beginTime: float;
        self.currentKeyFrame: any;
        self.keyFrameAry: list = [];
        self.time: int = 0;
        self.targetFlag: int = -1;
        self.visible: bool = False;
        self.selfRotaion: SelfRotation = None;
        self.currentKeyFrame: KeyFrame;
        self.axisMove: AxisMove=None;
        self.axisRotaion: AxisRotaion=None;
        pass

    def reset(self):
        self.time = 0;
        self.currentKeyFrame = self.keyFrameAry[0];
        self.visible = False;
        self.targetFlag = -1;
        pass

    def addKeyFrame(self, num: int):
        keyframe: KeyFrame = KeyFrame();
        keyframe.frameNum = num;
        self.keyFrameAry.append(keyframe)
        return keyframe;

        pass

    def inverAxisRotation(self, targetMatrix: Matrix3D):
        if self.axisRotaion:
            targetMatrix.prependRotation(-self.axisRotaion.num, self.axisRotaion.axis);

        pass

    def setAllDataInfo(self, data: TimeLineData):
        self.isByteData = True;
        lenNum: int = len(data.dataAry);
        for i in range(lenNum):
            key: KeyFrame = self.addKeyFrame(data.dataAry[i].frameNum);
            key.baseValue = data.dataAry[i].baseValue;
            key.animData = data.dataAry[i].animData;

            pass
        self.maxFrameNum = data.maxFrameNum;
        self.beginTime = data.beginTime;
        self.currentKeyFrame = self.keyFrameAry[0];
        pass

    def updateTime(self, t: int):

        if not self.currentKeyFrame:
            return;

        self.time = t;
        self.getTarget();
        #
        #
        # if (this._axisRotaion) {
        #     this._axisRotaion.update(this._time);
        # }
        #
        if self.selfRotaion:
            self.selfRotaion.update(self.time);

        #
        # if (this._axisMove) {
        #     this._axisMove.update(this._time);
        # }
        #
        # if (this._scaleChange) {
        #     this._scaleChange.update(this._time);
        # } else if (this._scaleNosie) {
        #     this._scaleNosie.update(this._time);
        # } else if (this._scaleAnim) {
        #     this._scaleAnim.update(this._time);
        # }
        pass

    def getTarget(self):
        flag: int = -1;
        for i in range(len(self.keyFrameAry)):
            if self.keyFrameAry[i].frameNum * Scene_data.frameTime < self.time:
                flag = i;
            else:
                break;

        if flag != self.targetFlag:
            self.currentKeyFrame = self.keyFrameAry[flag];
            self.targetFlag = flag;

            if flag >= (len(self.keyFrameAry) - 1) or not self.currentKeyFrame:
                self.visible = False;
                self.currentKeyFrame = None;
            else:
                self.visible = True;
                self.enterKeyFrame(self.currentKeyFrame.animData, self.currentKeyFrame.frameNum * Scene_data.frameTime,
                                   self.currentKeyFrame.baseValue);

        pass

    def applySelfRotation(self, targetMatrix: Matrix3D, axis: Vector3D):
        if self.selfRotaion:
            targetMatrix.prependRotation(self.selfRotaion.num, (axis.x, axis.y, axis.z));

        pass

    def enterKeyFrame(self, ary: list, baseTime: int = 0, baseValueAry: list = None):
        if baseValueAry is None:
            return;

        # for  i in range(10):
        #     if not  baseValueAry[i]:
        #         continue;
        #     if i== 1:
        #         if (not self.selfRotaion)
        #             self._selfRotaion = new SelfRotation;
        #         self._selfRotaion.num = self._selfRotaion.baseNum = baseValueAry[i];
        #
        #     if i== 2:
        #         if (!self._axisRotaion)
        #             self._axisRotaion = self AxisRotaion;
        #         self._axisRotaion.num = self._axisRotaion.baseNum = baseValueAry[i];
        #
        #     if i== 6:
        #         if (!self._scaleChange)
        #             self._scaleChange = new ScaleChange;
        #         self._scaleChange.num = self._scaleChange.baseNum = baseValueAry[i];
        #
        #     if i== 7:
        #         if (!self._scaleAnim)
        #             self._scaleAnim = self ScaleAnim;
        #         self._scaleAnim.num = self._scaleAnim.baseNum = baseValueAry[i];
        #
        #     if i== 8:
        #         if (!self._scaleNosie)
        #             self._scaleNosie = new ScaleNoise;
        #         self._scaleNosie.num = self._scaleNosie.baseNum = baseValueAry[i];
        #
        #     if i== 9:
        #         if (!self._axisMove)
        #             self._axisMove = new AxisMove;
        #         self._axisMove.num = self._axisMove.baseNum = baseValueAry[i];
        #
        #
        #
        #
        # if (this._selfRotaion)
        #     this._selfRotaion.isDeath = true;
        # if (this._axisRotaion)
        #     this._axisRotaion.isDeath = true;
        # if (this._scaleChange)
        #     this._scaleChange.isDeath = true;
        # if (this._scaleAnim)
        #     this._scaleAnim.isDeath = true;
        # if (this._scaleNosie)
        #     this._scaleNosie.isDeath = true;
        # if (this._axisMove)
        #     this._axisMove.isDeath = true;

        if ary == None:
            return;

        self.setBaseTimeByte(ary, baseTime, baseValueAry)

    def setBaseTimeByte(self, ary: list, baseTime: int = 0, baseValueAry: list = None):

        for i in range(len(ary)):
            if ary[i]['type'] == 1:
                if not self.selfRotaion:
                    self.selfRotaion = SelfRotation();
                else:
                    self.selfRotaion.reset();

                self.selfRotaion.dataByte(ary[i]['dataByte']);
                self.selfRotaion.baseTime = baseTime;

            pass
        #
        #     if (ary[i].type == 1) {
        #         if (!this._selfRotaion) {
        #             this._selfRotaion = new SelfRotation;
        #         } else {
        #             this._selfRotaion.reset();
        #         }
        #         // this._selfRotaion.data = (ary[i].data);
        #         this._selfRotaion.dataByte(ary[i].data, ary[i].dataByte);
        #         this._selfRotaion.baseTime = baseTime;
        #     } else if (ary[i].type == 2) {
        #         if (!this._axisRotaion) {
        #             this._axisRotaion = new AxisRotaion;
        #         } else {
        #             this._axisRotaion.reset();
        #         }
        #         this._axisRotaion.dataByte(ary[i].data, ary[i].dataByte);
        #         this._axisRotaion.baseTime = baseTime;
        #     } else if (ary[i].type == 6) {
        #         if (!this._scaleChange) {
        #             this._scaleChange = new ScaleChange;
        #         } else {
        #             this._scaleChange.reset();
        #         }
        #         //this._scaleChange.data = (ary[i].data);
        #         this._scaleChange.dataByte(ary[i].data, ary[i].dataByte);
        #         this._scaleChange.baseTime = baseTime;
        #     } else if (ary[i].type == 7) {
        #         if (!this._scaleAnim) {
        #             this._scaleAnim = new ScaleAnim;
        #         } else {
        #             this._scaleAnim.reset();
        #         }
        #         // this._scaleAnim.data = (ary[i].data);
        #         this._scaleAnim.dataByte(ary[i].data, ary[i].dataByte);
        #         this._scaleAnim.baseTime = baseTime;
        #     } else if (ary[i].type == 8) {
        #         if (!this._scaleNosie) {
        #             this._scaleNosie = new ScaleNoise;
        #         } else {
        #             this._scaleNosie.reset();
        #         }
        #         //this._scaleNosie.data = (ary[i].data);
        #         this._scaleNosie.dataByte(ary[i].data, ary[i].dataByte);
        #         this._scaleNosie.baseTime = baseTime;
        #     } else if (ary[i].type == 9) {
        #         if (!this._axisMove) {
        #             this._axisMove = new AxisMove;
        #         } else {
        #             this._axisMove.reset();
        #         }
        #         // this._axisMove.data = (ary[i].data);
        #         this._axisMove.dataByte(ary[i].data, ary[i].dataByte);
        #         this._axisMove.baseTime = baseTime;
        #     }
        # }

        pass

    def updateMatrix(self, posMatrix: Matrix3D, particle: any):
        if self.axisMove:
            posMatrix.prependTranslation(self.axisMove.axis.x * self.axisMove.num,
                                         self.axisMove.axis.y * self.axisMove.num,
                                         self.axisMove.axis.z * self.axisMove.num);

        if self.axisRotaion:
            posMatrix.prependRotation(self.axisRotaion.num, self.axisRotaion.axis);

        posMatrix.prependTranslation(particle.data.center.x, particle.data.center.y, particle.data.center.z);
        posMatrix.prependRotation(particle.data.rotationV3d.z, Vector3D.Z_AXIS);
        posMatrix.prependRotation(particle.data.rotationV3d.y, Vector3D.Y_AXIS);
        posMatrix.prependRotation(particle.data.rotationV3d.x, Vector3D.X_AXIS);

        # if (this._scaleChange) {
        #
        #     posMatrix.prependScale($particle.data._widthFixed ? 1 : this._scaleChange.num, $particle.data._heightFixed ? 1 : this._scaleChange.num,
        #         $particle.data._widthFixed ? 1 : this._scaleChange.num);
        # } else if (this._scaleNosie) {
        #
        #     posMatrix.prependScale($particle.data._widthFixed ? 1 : (1 + this._scaleNosie.num), $particle.data._heightFixed ? 1 : (1 + this._scaleNosie.num),
        #         $particle.data._widthFixed ? 1 : (1 + this._scaleNosie.num));
        # } else if (this._scaleAnim) {
        #
        #     posMatrix.prependScale($particle.data._widthFixed ? 1 : this._scaleAnim.num, $particle.data._heightFixed ? 1 : this._scaleAnim.num,
        #         $particle.data._widthFixed ? 1 : this._scaleAnim.num);
        # }

        pass
