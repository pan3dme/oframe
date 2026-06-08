from ..base.MeshData import MeshData


class AnimData:
    def __init__(self):
        self.isProcessAction: bool = False;
        self.hasProcess: bool = False
        self.name: str = None
        self.hierarchyList = []
        self.frameAry = []
        self.inter = []
        self.bounds = []
        self.matrixAry = []
        self.posAry = []
        self.nameHeight = 0
        self.boneQPAry = []
        self.meshBoneQPAryDic = {};

    def getBoneQPAryByMesh(self, meshData: MeshData):

        return self.meshBoneQPAryDic[meshData.uid];
