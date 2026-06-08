from ..res.BaseRes import BaseRes
from ..core.Vector3D import Vector3D
from ..core.Pan3dByteArray import Pan3dByteArray


class RoleRes(BaseRes):
    def __init__(self, scene):
        super().__init__(scene)
        self.actionAry: list = []
        self.meshBatchNum: int = 0
        self.roleUrl: str = ''
        self.ambientLightColor = Vector3D();
        self.sunLigthColor = Vector3D();
        self.nrmDircet = Vector3D();
        self.ambientLightIntensity: float = 1.0
        self.sunLigthIntensity: float = 1.0
        self.bFun=None

    def load(self, url, bfun):
        self.bFun=bfun;
        with open(url, 'rb') as file:
            self.loadComplete(file)
        pass

    def loadComplete(self, file):
        self.byte.file = file;
        self.version = self.byte.readInt();
        self.readMesh()

    def readMesh(self):
        self.roleUrl = self.byte.readUTF();

        self.ambientLightColor.x = self.byte.readFloat();
        self.ambientLightColor.y = self.byte.readFloat();
        self.ambientLightColor.z = self.byte.readFloat();
        self.ambientLightIntensity = self.byte.readFloat();
        self.ambientLightColor.scaleBy(self.ambientLightIntensity);

        self.sunLigthColor.x = self.byte.readFloat();
        self.sunLigthColor.y = self.byte.readFloat();
        self.sunLigthColor.z = self.byte.readFloat();
        self.sunLigthIntensity = self.byte.readFloat();
        self.sunLigthColor.scaleBy(self.sunLigthIntensity);

        self.nrmDircet.x = self.byte.readFloat();
        self.nrmDircet.y = self.byte.readFloat();
        self.nrmDircet.z = self.byte.readFloat();

        self.scene3D.meshDataManager.readData(self.byte, self.meshBatchNum, self.roleUrl, self.version);
        self.readAction()

    def readAction(self):
        actionByte: Pan3dByteArray = self.byte.getZipByte()
        self.actionAry = []
        actionNum: int = actionByte.readInt();
        for i in range(actionNum):
            actionName: str = actionByte.readUTF();
            self.scene3D.animManager.readData(actionByte, self.roleUrl + actionName);
            self.actionAry.append(actionName);

        self.read(self.readNext)

    def readNext(self):
        self.read();
        self.read();
        self.bFun()
