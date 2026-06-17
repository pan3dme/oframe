from ..base.ResGC import ResGC
from ..res.RoleRes import RoleRes
from ..res.SkillRes import SkillRes


class ResManager(ResGC):
    def __init__(self, scene):
        super().__init__(scene)

    def loadRoleRes(self, url, bfun, meshBatchNum):
        roleRes = RoleRes(self.scene3D);
        roleRes.meshBatchNum = meshBatchNum;

        def abc():
            bfun(roleRes)

        roleRes.load(url, abc);

        pass

    def loadSkillRes(self, url, bfun):
        skillRes: SkillRes = SkillRes(self.scene3D);
        def loadBfun():
            bfun(skillRes)

        skillRes.load(url, loadBfun);
