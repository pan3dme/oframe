from ..base.ResCount import ResCount
from ..mateial.Material import Material
from ..mateial.TextureRes import TextureRes
from ..mateial.DynamicBaseTexItem import DynamicBaseTexItem
from ..mateial.MaterialVo import MaterialVo

class MaterialBaseParam(ResCount):
    def __init__(self, value):
        super().__init__(value)
        self.material: Material;
        self.dynamicConstList: list;
        self.dynamicTexList: list

    def update(self):
        if (self.material and self.dynamicConstList):
            for i in range(len(self.dynamicConstList)):
                self.dynamicConstList[i].update();

    def setData(self, material: Material, ary: list):
        self.material = material;
        self.dynamicConstList = [];
        self.dynamicTexList = [];
        constList: list = material.constList;
        texList: list = material.texList;
        for i in range(len(ary)):
            obj: MaterialVo = ary[i];
            if obj.type == 0:
                texItem: DynamicBaseTexItem = DynamicBaseTexItem(self.scene3D);
                texItem.paramName = obj.name;
                for j in range(len(texList)):
                    if texItem.paramName == texList[j].paramName:
                        texItem.target = texList[j];
                        break;

                mipmap: int = 0;

                def bfun(textureRes: TextureRes):
                    texItem.textureRes = textureRes;

                self.scene3D.textureManager.getTexture(obj.url, bfun, 0, None, 0, mipmap);
                self.dynamicTexList.append(texItem);

            else:
                print('还需要补充')
