
from ..base.GC import GC

class ResCount(GC):
    GCTime:int=4;
    def __init__(self,scene3D):
        super().__init__()
        self.scene3D = scene3D;
        self.useNum:int=0;