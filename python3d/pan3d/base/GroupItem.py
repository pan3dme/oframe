from ..core.Object3D import Object3D
class GroupItem(Object3D):
    def __init__(self):
        super().__init__()
        self.objUrl:str=''
        self.materialUrl:str=''
        self.particleUrl:str=''
        self.materialInfoArr=None
        self.isGroup:bool=False
        self.types:int=0
