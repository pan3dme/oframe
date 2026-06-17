from ..base.ResCount import ResCount
from ..core.Object3D import Object3D
from ..vo.SkillVo import SkillVo
# from ..role.Display3dMovie import Display3dMovie
from ..skill.SkillType import SkillType
from ..skill.SkillData import SkillData
from ..skill.SkillFixEffect import SkillFixEffect
from ..skill.key.SkillTrajectory import SkillTrajectory
from ..skill.vo.SkillTrajectoryTargetKeyVo import SkillTrajectoryTargetKeyVo
from ..skill.key.SkillMulTrajectory import SkillMulTrajectory


class Skill(ResCount):
    MaxTime: int = 1000 * 5;

    def __init__(self, scene):
        super().__init__(scene)

        self.skillVo: SkillVo;
        self.name: str;
        self.key: str;
        self.isDeath: bool = True;
        self.keyAry: list = None;
        self.completeNum: int = None;
        self.src: bool = False;
        self.active: any;
        self.completeFun: any = None;
        self.time: int = 0;
        self.targetFlag: int = 0;
        self.targetShockFlag: int = 0;
        self.active: Object3D = None;
        # self.trajectoryAry: Array < SkillTrajectory >;
        # self.skillData: SkillData;
        self.batterObj: any;
        self.tbSkillId: int;
        self.soundPlay: bool = None;
        self.needSound: bool = False;
        self.hasDestory: bool = False;
        self.actionEnd: bool = False

    def reset(self):
        self.time = 0;
        self.completeNum = 0;
        self.active = None;
        self.completeFun = None;
        self.targetFlag = 0;
        self.targetShockFlag = 0;
        self.soundPlay = False;
        self.needSound = False;

    def play(self):
        if not self.skillVo:
            self.skillComplete();
            return;

        if self.active:

            if self.actionEnd:
                self.active.play(self.skillVo.action, 1, False);
            else:
                self.active.play(self.skillVo.action, 2, False);

        pass

    def skillComplete(self):

        self.scene3D.skillManager.removeSkill(self);
        self.isDeath = True;
        if self.completeFun:
            self.completeFun();

        self.idleTime = 0;

        pass

    def update(self, t: int):

        self.time += t;
        if self.time > Skill.MaxTime:
            # print("超时结束");
            self.skillComplete();

        self.getKeyTarget();
        self.getShockTarget();
        self.updateTrajector(t);
        pass

    def getKeyTarget(self):
        if not self.keyAry:
            return;
        for idx in range(len(self.keyAry) - self.targetFlag):
            i = idx + self.targetFlag;
            if self.keyAry[i].time < self.time:
                self.keyAry[i].addToRender();
                if self.skillVo.types == SkillType.TrajectoryDynamicTarget or self.skillVo.types == SkillType.TrajectoryDynamicPoint:
                    ss: any = self.keyAry[i];
                    self.trajectoryAry.append(ss);
                self.targetFlag = i + 1;

        pass

    def getShockTarget(self):
        pass

    def updateTrajector(self, t: int):
        pass

    def setData(self, data: any, skillData: SkillData):
        if self.hasDestory:
            return;

        self.skillVo = SkillVo();
        self.skillVo.setData(data);
        self.setKeyAry();
        self.trajectoryAry: list = None;
        self.skillData = skillData;

    def configFixEffect(self, active: Object3D, completeFun: any = None, posObj: list = None):
        self.active = active;
        self.completeFun = completeFun;

        if not self.keyAry:
            return;

        for i in range(len(self.keyAry)):
            if self.skillVo.types != SkillType.FixEffect:
                continue;

            skillFixEffect: SkillFixEffect = SkillFixEffect(self.keyAry[i]);
            skillFixEffect.active = active;

            if posObj and len(posObj):
                if i > (posObj.length - 1):
                    skillFixEffect.outPos = posObj[posObj.length - 1];

                else:
                    skillFixEffect.outPos = posObj[i];

            else:
                skillFixEffect.outPos = None;

        pass

    def setKeyAry(self):
        self.keyAry = [];
        if self.skillVo.types == SkillType.FixEffect:
            for i in range(len(self.skillVo.keyAry)):
                keySkill: SkillFixEffect = SkillFixEffect(self.scene3D);
                keySkill.setInfo(self.skillVo.keyAry[i]);

                def bfun(value):
                    self.removeKey(value)

                keySkill.removeCallFun = bfun;
                keySkill.active = self.active;
                self.keyAry.append(keySkill);
        elif (
                self.skillVo.types == SkillType.TrajectoryDynamicTarget or self.skillVo.types == SkillType.TrajectoryDynamicPoint):
            for i in range(len(self.skillVo.keyAry)):
                trajectory: SkillTrajectory;
                tkv: SkillTrajectoryTargetKeyVo = self.skillVo.keyAry[i];
                if tkv.multype == 1:
                    trajectory = SkillMulTrajectory(self.scene3D);
                else:
                    trajectory = SkillTrajectory(self.scene3D);

                trajectory.setInfo(self.skillVo.keyAry[i]);
                self.keyAry.append(trajectory);
