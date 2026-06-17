from ..skill.SkillType import SkillType
from ..skill.vo.SkillFixEffectKeyVo import SkillFixEffectKeyVo


class SkillVo():
    def __init__(self):
        self.action: str;
        self.skillname: str
        self.keyAry: list;
        self.shockAry: list;
        self.types: int;
        self.bloodTime: int;
        # self. static defaultBloodTime: number = 250;
        # self. sound: SkillKeyVo;

    def setData(self, info: any):
        self.keyAry = [];
        self.action = info['action'];
        self.skillname = info['skillname'];
        self.bloodTime = info['blood'];
        self.types = info['type'];

        if self.types == SkillType.FixEffect:
            self.keyAry = self.getFixEffect(info['data']);
        elif (self.types == SkillType.TrajectoryDynamicTarget or self.types == SkillType.TrajectoryDynamicPoint):
            self.keyAry = self.getTrajectoryDynamicTarget(info['data']);

        # if  info.sound：
        #     self.sound =   SkillKeyVo();
        #     self.sound.frame = info.sound.time * Scene_data.frameTime;
        #     self.sound.url = info.sound.name;
        #
        #
        # if info.shock:
        #     this.shockAry = this.getShockAry($info.shock);


    def getFixEffect(self,ary:list):
        keyAry: list =  [];
        for i in range(len(ary)):
            key: SkillFixEffectKeyVo =SkillFixEffectKeyVo();
            key.setData(ary[i]);
            keyAry.append(key);

        return keyAry
