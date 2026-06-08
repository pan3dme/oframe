from ...res.ResCount import ResCount
from ...scene3D.Scene_data import Scene_data
from ...core.Pan3dByteArray import Pan3dByteArray
from ...display.particle.ParticleData import ParticleData
from ...display.particle.Display3DParticle import Display3DParticle
from ...display.particle.CombineParticle import CombineParticle
from ...display.particle.bone.ParticleBoneData import ParticleBoneData
from ...display.particle.ball.ParticleBallData import ParticleBallData
from ...display.particle.facet.ParticleFacetData import ParticleFacetData
from ...display.particle.locus.ParticleLocusData import ParticleLocusData
from ...display.particle.model.ParticleModelData import ParticleModelData

from ...display.particle.locusball.ParticleLocusballData import ParticleLocusballData



class CombineParticleData(ResCount):
    def __init__(self, scene):
        super().__init__(scene)
        self.maxTime: int = 0;
        self.dataAry: list = []
        pass


    def getCombineParticle(self):
        particle: CombineParticle = CombineParticle();
        particle.maxTime = self.maxTime;
        for i in range(len(self.dataAry)):
            display: Display3DParticle = self.dataAry[i].creatPartilce();
            particle.addPrticleItem(display);

        particle.sourceData = self;
        return particle

    def getParticleDataType(self, type: int):
        pdata: ParticleData;
        if type == 1:
            pdata = ParticleFacetData(self.scene3D)
            pass
        elif type == 3:
            pdata = ParticleLocusData(self.scene3D);
            pass
        elif type == 9 or type == 4 or type == 7:
            pdata = ParticleModelData(self.scene3D);
            pass
        elif type == 18:
            pdata = ParticleBallData(self.scene3D);
            pass
        elif type == 13:
            pdata = ParticleBoneData(self.scene3D);
            pass
        elif type == 14:
            pdata = ParticleLocusballData(self.scene3D)
            pass
        else:
            print('要补充')


        return pdata;

    def setDataByte(self, byte: Pan3dByteArray):
        version: int = byte.readInt();
        len: int = byte.readInt();
        self.maxTime = 0;
        self.dataAry = [];
        for i in range(len):
            particleType: int = byte.readInt();
            pdata: ParticleData = self.getParticleDataType(particleType);
            pdata.version = version;
            pdata.setAllByteInfo(byte);
            if pdata.timelineData.maxFrameNum > self.maxTime:
                self.maxTime = pdata.timelineData.maxFrameNum;

            self.dataAry.append(pdata);
            pass
        self.maxTime *= Scene_data.frameTime;
        pass
