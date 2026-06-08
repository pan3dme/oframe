
from ....base.ObjData import ObjData
class ParticleBallGpuData(ObjData):
    def __init__(self, scene):
        super().__init__(scene)

        self.basePos:list = [];
