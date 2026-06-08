from ..res.ResCount import ResCount
from ..mateial.Material import Material
from ..mateial.ConstItem import ConstItem
from ..mateial.DynamicConstItem import DynamicConstItem
from ..mateial.DynamicTexItem import DynamicTexItem
from ..program.Shader3D import Shader3D


class MaterialParam(ResCount):
    def __init__(self, scene3D):
        super().__init__(scene3D)
        self.material: Material = None;
        self.materialUrl: str = None;
        self.shader: Shader3D = None;
        self.dynamicTexList: list = None;
        self.dynamicConstList: list = None;

    def setMaterial(self, materialTree: Material):
        self.material = materialTree;
        self.materialUrl = materialTree.url;
        self.dynamicTexList = [];
        self.dynamicConstList = [];
        self.setTexList();
        self.setConstList();

    def setTexList(self):
        texList: list = self.material.texList;
        for i in range(len(texList)):
            dyTex: DynamicTexItem;
            if texList[i].isParticleColor:
                dyTex = DynamicTexItem(self.scene3D);
                dyTex.target = texList[i];
                dyTex.paramName = texList[i].paramName;
                dyTex.initCurve(4);
                self.dynamicTexList.append(dyTex);
                dyTex.isParticleColor = True;
            elif texList[i].isDynamic:
                dyTex = DynamicTexItem(self.scene3D);
                dyTex.target = texList[i];
                dyTex.paramName = texList[i].paramName;
                self.dynamicTexList.append(dyTex);

        pass

    def setConstList(self):
        constList: list = self.material.constList;

        for i in range(len(constList)):
            constItem: ConstItem = constList[i];
            dyCon: DynamicConstItem;
            if constItem.param0Type != 0:
                dyCon = DynamicConstItem(self.scene3D);
                dyCon.setTargetInfo(constItem, constItem.paramName0, constItem.param0Type);
                self.dynamicConstList.append(dyCon);
            if constItem.param1Type != 0:
                dyCon = DynamicConstItem(self.scene3D);
                dyCon.setTargetInfo(constItem, constItem.paramName1, constItem.param1Type);
                self.dynamicConstList.append(dyCon);

            if constItem.param2Type != 0:
                dyCon = DynamicConstItem(self.scene3D);
                dyCon.setTargetInfo(constItem, constItem.paramName2, constItem.param2Type);
                self.dynamicConstList.append(dyCon);

            if constItem.param3Type != 0:
                dyCon = DynamicConstItem(self.scene3D);
                dyCon.setTargetInfo(constItem, constItem.paramName3, constItem.param3Type);
                self.dynamicConstList.append(dyCon);

        pass

    def setLife(self, life: int):
        for i in range(len(self.dynamicTexList)):
            self.dynamicTexList[i].life = life;

    def setTextObj(self, ary: list):
        for i in range(len(ary)):
            obj: any = ary[i];
            for j in range(len(self.dynamicTexList)):
                dynamicTexItem: DynamicTexItem = self.dynamicTexList[j];
                if dynamicTexItem.paramName == obj['paramName']:
                    if dynamicTexItem.isParticleColor:
                        dynamicTexItem.curve.setData(obj['curve']);
                    else:
                        dynamicTexItem.url = obj['url'];
                    break;

        pass

    def setConstObj(self, ary: list):
        for i in range(len(ary)):
            obj: any = ary[i];
            for j in range(len(self.dynamicConstList)):
                if self.dynamicConstList[j].paramName == obj['paramName']:
                    self.dynamicConstList[j].curve.setData(obj['curve'])
                    break;

        pass
