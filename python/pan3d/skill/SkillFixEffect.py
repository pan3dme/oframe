

from ..skill.SkillEffect import SkillEffect
from ..skill.vo.SkillKeyVo import SkillKeyVo
from ..skill.vo.SkillFixEffectKeyVo import SkillFixEffectKeyVo
class SkillFixEffect(SkillEffect):
    def __init__(self, scene):
        super().__init__(scene)

    def setInfo(self, obj: SkillKeyVo):
        super().setInfo(obj);
        data: SkillFixEffectKeyVo = obj;
        self.pos = data.pos;
        self.rotation = data.rotation;
        self.hasSocket = data.hasSocket;
        self.socket = data.socket;

