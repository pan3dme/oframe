from ....display.particle.ParticleData import ParticleData
from ....display.particle.facet.Display3DFacetParticle import Display3DFacetParticle
from ....display.particle.facet.Display3DFacetParticle import Display3DFacetShader
from ....core.Pan3dByteArray import Pan3dByteArray
from ....base.ObjData import ObjData
from ....core.Vector2D import Vector2D


import numpy as np



class ParticleFacetData(ParticleData):
    def __init__(self, scene):
        super().__init__(scene)
        self.maxAnimTime: int = None;
        self.isCycle: bool = None;
        self.lockx: bool = None;
        self.locky: bool = None;


    def setAllByteInfo(self, byte: Pan3dByteArray):
        self.maxAnimTime = byte.readFloat();
        self.isCycle = byte.readBoolean();
        self.lockx = byte.readBoolean();
        self.locky = byte.readBoolean();

        super().setAllByteInfo(byte);

        self.initVcData();

        self.uploadGpu();

        pass

    def initVcData(self):
        pass

    def uploadGpu(self):
        self.objData = ObjData(self.scene3D);

        self.makeRectangleData(self.width, self.height, self.originWidthScale, self.originHeightScale,
                               self.isUV, self.isU, self.isV, self.animLine, self.animRow);

        pass

    def makeRectangleData(self, width: float, height: float, offsetX: float = 0.5, offsetY: float = 0.5,
                          isUV: bool = False, isU: bool = False, isV: bool = False,
                          animLine: float = 1, animRow: float = 1):

        uvAry: list = [];
        verterList: list = [];
        ary: list = [];
        ary.append(Vector2D(0, 0));
        ary.append(Vector2D(0, 1 / animRow));
        ary.append(Vector2D(1 / animLine, 1 / animRow));
        ary.append(Vector2D(1 / animLine, 0));
        if isU:
            for i in range(len(ary)):
                ary[i].x = - ary[i].x;

        if isV:
            for i in range(len(ary)):
                ary[i].y = - ary[i].y;

        if isUV:
            ary.append(ary.pop(0));

        for i in range(len(ary)):
            uvAry.append(ary[i].x);
            uvAry.append(ary[i].y);

        verterList.append(-offsetX * width);
        verterList.append(height - offsetY * height);
        verterList.append(0);

        verterList.append(ary[0].x);
        verterList.append(ary[0].y);

        verterList.append(width - offsetX * width, );
        verterList.append(height - offsetY * height);
        verterList.append(0);

        verterList.append(ary[1].x);
        verterList.append(ary[1].y);

        verterList.append(width - offsetX * width, );
        verterList.append(-offsetY * height);
        verterList.append(0);

        verterList.append(ary[2].x);
        verterList.append(ary[2].y);

        verterList.append(-offsetX * width);
        verterList.append(-offsetY * height);
        verterList.append(0);

        verterList.append(ary[3].x);
        verterList.append(ary[3].y);

        indexs: list = [0, 1, 2, 0, 2, 3];

        self.objData.buffArr = np.array(verterList, dtype=np.float32);
        self.objData.indexs = indexs;
        self.objData.upToGPU()


    def getParticle(self):
        return Display3DFacetParticle(self.scene3D);

    def regShader(self):

        self.materialParam.shader = self.scene3D.progrmaManager.getMaterialProgram(
            Display3DFacetShader.Display3DFacetShader,
            Display3DFacetShader, self.materialParam.material);
        pass
