from OpenGL.GL import *
from ..res.GroupRes import GroupRes
from ..display.Display3D import Display3D
from ..display.DispBase3dSprite import DispBase3dSprite
from ..core.TimeUtil import TimeUtil
from ..res.BaseRes import BaseRes
from ..base.ObjData import ObjData
from ..mateial.TextureRes import TextureRes
from ..mateial.MaterialBaseParam import MaterialBaseParam
from ..mateial.TexItem import TexItem
from ..mateial.Material import Material
from ..program.MaterialShader import MaterialShader
import numpy as np


class Display3DSprite(DispBase3dSprite):
    def __init__(self, scene):
        super().__init__(scene)
        self.dynamic = False;
        self.time: int = 0;
        self.lightTextureRes = None
        self.material: Material = None;

        self.materialParam: MaterialBaseParam = None;

        pass

    def setLighturl(self, url: str):

        def bfun(textureRes: TextureRes):
            self.lightTextureRes = textureRes

        self.scene3D.textureManager.getTexture(url, bfun);

    def setMaterialUrl(self, baseUrl, paramData):
        url: str = baseUrl.replace("_byte.txt", ".txt")
        url = url.replace(".txt", "_byte.txt")

        def backFun(material: Material):
            self.material = material;
            if None != paramData:
                self.materialParam = MaterialBaseParam(self.scene3D);
                self.materialParam.setData(self.material, paramData);

        self.scene3D.materialManager.getMaterialByte(url, backFun, None, True, MaterialShader.MATERIAL_SHADER,
                                                     MaterialShader)
        pass

    def setObjUrl(self, url):
        def bfun(value: ObjData):
            self.objData = value
            self.objData.upToGPU()

        self.scene3D.objDataManager.getObjData(url, bfun)
        pass

    def setModelUrl(self, path):
        def bfun(groupRes: GroupRes):
            if len(groupRes.dataAry):
                for item in groupRes.dataAry:
                    if item.types == BaseRes.PREFAB_TYPE:
                        self.setObjUrl(item.objUrl)
                        self.setMaterialUrl(item.materialUrl, item.materialInfoArr)
                        self.dynamic = True;
                    else:
                        print('模型有错')

        self.scene3D.groupDataManager.getGroupData(path, bfun)

    def upData(self):
        if self.material is None or self.objData is None:
            return

        self.scene3D.context3D.setProgram(self.material.shader);
        self.setMaterialTexture(self.material, self.materialParam);
        self.setVc();
        self.setMaterialVa();
        self.setMaterialVc(self.material, self.materialParam);

        self.scene3D.context3D.drawCallOld(self.objData.eboID, len(self.objData.indexs))

    def setMaterialVa(self):
        if self.objData:
            self.scene3D.context3D.setVaOld(self.objData.vbo, 0, 3, self.objData.stride, 0)
            self.scene3D.context3D.setVaOld(self.objData.vbo, 1, 2, self.objData.stride, self.objData.uvsOffsets)
            if not self.material.noLight:
                self.scene3D.context3D.setVaOld(self.objData.vbo, 2, 2, self.objData.stride,
                                                self.objData.lightuvsOffsets)


    def setVc(self):
        self.scene3D.context3D.setVcMatrix4fv(self.material.shader, 'posMatrix3D', self.getPosMatrix())
        self.scene3D.context3D.setVcMatrix4fv(self.material.shader, 'vpMatrix3D', self.scene3D.camera3D.vpMatrix3D)
    def setMaterialVc(self, material: Material, mp: MaterialBaseParam):

        if material.fcNum <= 0:
            return;

        t: float = 0;
        if material.hasTime:
            t = TimeUtil.getTimer() - self.time;

        material.update(t);

        if None != mp:
            mp.update();

        # var ctx: Context3D = this.scene3D.context3D;
        # ctx.setVc4fv($material.shader, "fc", $material.fcData);]

        self.scene3D.context3D.setVc4fv(material.shader, 'fc', np.array(material.fcData, dtype=np.float32), 54)
        # self.scene3D.context3D.setVc4fv(meshData.material.shader, "boneQ",
        #                                 np.array(dualQuatFrame.quat, dtype=np.float32));
        pass

    def setMaterialTexture(self, material: Material, mp: MaterialBaseParam):

        if mp:
            for i in range(len(mp.dynamicTexList)):
                if mp.dynamicTexList[i].target:
                    self.scene3D.context3D.setRenderTexture(material.shader, mp.dynamicTexList[i].target.getName(),
                                                            mp.dynamicTexList[i].target.id,
                                                            mp.dynamicTexList[i].textureRes)

        for i in range(len(material.texList)):
            texItem: TexItem = material.texList[i]
            if texItem.type == TexItem.LIGHTMAP:
                self.scene3D.context3D.setRenderTexture(material.shader, texItem.getName(), texItem.id,
                                                        self.lightTextureRes)

                pass
            if texItem.textureRes:
                print('setMaterialTexture 等待处理')
                pass

        pass

    def destroy(self):
        pass
