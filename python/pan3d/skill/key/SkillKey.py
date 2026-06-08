from ...display.particle.CombineParticle import CombineParticle
from ...scene3D.Scene_data import Scene_data
from ...skill.vo.SkillKeyVo import SkillKeyVo


class SkillKey:
    def __init__(self, scene):
        self.scene3D = scene
        self.time: int = 0;
        self.particle: CombineParticle = None;
        self.removeCallFun: any = None;
        pass

    def setInfo(self, obj: SkillKeyVo):
        self.time = obj.frame * Scene_data.frameTime;
        self.particle = self.scene3D.particleManager.getParticleByte(obj.url);
    def addToRender(self):
        if not self.particle:
            return;
        self.particle.reset();
        self.particle.sceneVisible = True
        self.scene3D.particleManager.addParticle(self.particle);
