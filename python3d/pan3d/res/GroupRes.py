from ..res.BaseRes import BaseRes

from ..base.GroupItem import GroupItem


class GroupRes(BaseRes):
    def __init__(self, scene):
        super().__init__(scene)
        self.dataAry=[];
        self.objDic = {}
        self.fun=None;


    def load(self, url: str, bfun):
        self.fun=bfun;
        with open(url, 'rb') as file:
            self.loadComplete(file)


    def loadComplete(self,file):
        self.byte.file = file;
        self.version = self.byte.readInt();
        self.read(self.readNext)

        self.fun();
        self.fun = None;
        self.byte = None;

    def initReg(self):
        pass



    def readItem(self,isG):
        types: int = self.byte.readInt();
        item:GroupItem=GroupItem()
        item.isGroup=isG;
        if item.isGroup:
            item.x = self.byte.readFloat();
            item.y = self.byte.readFloat();
            item.z = self.byte.readFloat();
            item.scaleX = self.byte.readFloat();
            item.scaleY = self.byte.readFloat();
            item.scaleZ = self.byte.readFloat();
            item.rotationX = self.byte.readFloat();
            item.rotationY = self.byte.readFloat();
            item.rotationZ = self.byte.readFloat();

        if types == BaseRes.PREFAB_TYPE :
            item.objUrl = self.byte.readUTF();
            item.materialUrl = self.byte.readUTF();
            item.materialInfoArr=self.readMaterialInfo();
            item.types = BaseRes.PREFAB_TYPE;
        if types==BaseRes.SCENE_PARTICLE_TYPE:
            item.particleUrl = self.byte.readUTF();
            item.types = BaseRes.SCENE_PARTICLE_TYPE;

        self.dataAry.append(item);



    def readNext(self):
        self.read()
        self.read()
        self.read()
        isGroup:bool = self.byte.readBoolean();
        if isGroup:
            length: int = self.byte.readInt();
            print('等待处理')

        else:
            self.readItem(False)

        # this._fun();
        # this._fun = null;
        # this._byte = null;


