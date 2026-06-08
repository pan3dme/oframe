from ..base.ResGC import ResGC
from ..core.Pan3dByteArray import Pan3dByteArray
from ..mateial.TexItem import TexItem
from ..mateial.DynamicTexItem import DynamicTexItem
from ..mateial.TextureRes import TextureRes
from ..mateial.Material import Material
from ..mateial.MaterialLoad import MaterialLoad
from ..mateial.MaterialParam import MaterialParam


class MaterialManager(ResGC):
    def __init__(self, scene):
        super().__init__(scene)
        self.loadDic = {}
        self.resDic = {}
        self.regDic = {}

    def getMaterialByte(self, url, bfun, info: any = None, autoReg: bool = False, regName: str = None,
                        shader3DCls: any = None):
        if url in self.dic:
            bfun(self.dic[url])
            return

        materialLoad: MaterialLoad = MaterialLoad(bfun, info, url, autoReg, regName, shader3DCls)
        if url in self.loadDic:
            self.loadDic[url].append(materialLoad)
            return

        self.loadDic[url] = []
        self.loadDic[url].append(materialLoad);

        if url in self.resDic:
            self.meshByteMaterialByt(self.resDic[url], materialLoad);

        pass

    def addResByte(self, url: str, data: Pan3dByteArray):
        if not (url in self.dic) and not (url in self.resDic):
            self.resDic[url] = data;

    def meshByteMaterialByt(self, byte: Pan3dByteArray, info: MaterialLoad):
        material: Material = Material(self.scene3D)
        material.setByteData(byte)
        material.url = info.url;
        self.loadMaterial(material);

        if info.autoReg:
            material.shader = self.scene3D.progrmaManager.getMaterialProgram(info.regName, info.shader3D, material,
                                                                             None, True);

        ary: list = self.loadDic[info.url];

        for i in range(len(ary)):
            textureLoad: MaterialLoad = ary[i];
            if textureLoad.info:
                textureLoad.fun(material, textureLoad.info);
            else:
                textureLoad.fun(material);

        del self.loadDic[info.url];
        self.dic[info.url] = material;

    def loadMaterial(self, material: Material):

        texVec: list = material.texList;

        for i in range(len(texVec)):
            if texVec[i].isParticleColor or texVec[i].isDynamic or texVec[i].type != 0:
                continue;

            def TexTureBfun(textureRes: TextureRes, texItem: TexItem):
                texItem.textureRes = textureRes;
                pass

            self.scene3D.textureManager.getTexture(texVec[i].url, TexTureBfun, texVec[i].wrap, texVec[i],
                                                   texVec[i].filter, texVec[i].mipmap);

        pass

    def loadDynamicTexUtil(self, material: MaterialParam):

        dynamicTexList: list = material.dynamicTexList;

        for i in range(len(dynamicTexList)):
            dynamicTexItem: DynamicTexItem = dynamicTexList[i]
            if dynamicTexItem.isParticleColor:
                dynamicTexItem.creatTextureByCurve();
            else:
                def bfun(textureRes: TextureRes, tempDynamicTexItem: DynamicTexItem):
                    tempDynamicTexItem.textureRes = textureRes;
                    pass

                self.scene3D.textureManager.getTexture(dynamicTexItem.url, bfun, 0, dynamicTexItem);

        pass
