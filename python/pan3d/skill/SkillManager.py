from ..base.ResGC import ResGC
from ..core.TimeUtil import TimeUtil
from ..core.TimeUtil import TimeUtilInter
from ..core.Pan3dByteArray import Pan3dByteArray

from ..skill.Skill import Skill
from ..skill.SkillData import SkillData


class SkillManager(ResGC):
    def __init__(self, scene):
        super().__init__(scene);
        self.time = TimeUtilInter.getTimer()
        self.skillDic: any = {};
        self.loadDic: any = {};
        self.skillAry: list = [];

    def playSkill(self, skill: Skill):
        self.skillAry.append(skill);
        skill.play();
        pass

    def removeSkill(self, skill: Skill):

        self.skillAry.remove(skill)


        pass

    def update(self):
        tempTime: int = TimeUtilInter.getTimer()
        t: int = tempTime - self.time;
        lenNum=len(self.skillAry)
        for i in range(lenNum):
            self.skillAry[lenNum-i-1].update(t);

        self.time = tempTime;
        pass

    def getSkill(self, url: str, name: str, callback: any = None):

        skill: Skill;
        key: str = url + name;
        if key in self.skillDic:
            ary: list = self.skillDic[key];

            for i in range(len(ary)):
                skill = ary[i];
                if (skill.isDeath and skill.useNum == 0):
                    skill.reset();
                    skill.isDeath = False;
                    return skill;
        else:
            self.skillDic[key] = [];

        skill = Skill(self.scene3D);
        skill.name = name;
        skill.isDeath = False;
        self.skillDic[key].append(skill)

        if url in self.dic:
            skill.setData(self.dic[url].data[skill.name], self.dic[url]);
            skill.key = key;
            self.dic[url].useNum += 1;
            return skill;

        obj: any = {};
        obj['name'] = name;
        obj['skill'] = skill;
        obj['callback'] = callback;

        if not url in self.loadDic:
            self.loadDic[url] = []
            self.loadDic[url].append(obj);

            def bfun(skillRes):
                self.loadSkillCom(url, skillRes)
                pass

            self.scene3D.resManager.loadSkillRes(url, bfun);
        else:
            self.loadDic[url].append(obj);

        return skill;

    def loadSkillCom(self, url, skillRes):
        skillData: SkillData = SkillData(self.scene3D);
        skillData.data = skillRes.data;
        for i in range(len(self.loadDic[url])):

            obj: any = self.loadDic[url][i];
            skill: Skill = obj['skill']
            if not skill.hasDestory:
                skill.setData(skillData.data[obj['name']], skillData);
                skill.key = url + obj['name'];
                skillData.useNum += 1;

        self.dic[url] = skillData;
        self.addSrc(url, skillData);

        for i in range(len(self.loadDic[url])):
            obj: any = self.loadDic[url][i];
            if obj['callback']:
                obj['callback']();
        self.loadDic[url] = None;
        pass

    def addSrc(self, url, skillData: SkillData):
        for key in skillData.data:
            pass
            # var skill: Skill = new Skill(this.scene3D);
            # skill.name = key;
            # skill.isDeath = true;
            # skill.src = true;
            # skill.setData(skillData.data[key], skillData);
            # skillData.addSrcSkill(skill);
            # //skillData.useNum++;
            #
            # var dkey: string = $url + key
            # if (!this._skillDic[dkey]) {
            #     this._skillDic[dkey] = new Array;
            # }
            # this._skillDic[dkey].push(skill);

        pass
