from ..base.ResGC import ResGC
from ..core.TimeUtil import TimeUtil
from ..core.TimeUtil import TimeUtilInter
from ..core.Pan3dByteArray import Pan3dByteArray
from ..display.particle.CombineParticle import CombineParticle
from ..display.particle.CombineParticleData import CombineParticleData


class ParticleManager(ResGC):
    def __init__(self, scene):
        super().__init__(scene);
        self.loadDic: any = {};
        self.particleList: list = [];

        self.time: int = 0;

    def getParticleByte(self, url):
        url = url.replace("_byte.txt", ".txt")
        url = url.replace(".txt", "_byte.txt")

        combineParticle: CombineParticle = CombineParticle()

        if url in self.dic:
            baseData: CombineParticleData = self.dic[url];
            combineParticle = baseData.getCombineParticle();
            pass
        combineParticle.url = url;
        return combineParticle

    def addResByte(self, url: str, byte: Pan3dByteArray):
        if url in self.dic:
            return
        else:
            baseData: CombineParticleData = CombineParticleData(self.scene3D);
            baseData.setDataByte(byte);
            self.dic[url] = baseData;

        pass

    def addParticle(self, particle: CombineParticle):
        self.particleList.append(particle);

        pass

    def removeParticle(self, particle: CombineParticle):

        for i in range(len(self.particleList)):
            idx: int = len(self.particleList) - 1 - i
            if self.particleList[idx] == particle:
                del self.particleList[idx];
                pass

    def upFrame(self):
        self.updateTime();
        self.updateRender();
        pass

    def updateTime(self):
        tempTime: int = TimeUtilInter.getTimer()
        t: int = tempTime - self.time;
        for i in range(len(self.particleList)):
            idx: int = len(self.particleList) - 1 - i
            particle: CombineParticle = self.particleList[idx]
            if particle.sceneVisible:
                particle.updateTime(t)
                pass
        self.time = tempTime;

    def updateRender(self):
        self.scene3D.context3D.setWriteDepth(False);
        self.scene3D.context3D.disableCullFace();
        # print(len(self.particleList))
        for i in range(len(self.particleList)):
            particle: CombineParticle = self.particleList[i]
            particle.update();
