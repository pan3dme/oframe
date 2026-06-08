from ..res.ResCount import ResCount
from ..core.Vector2D import Vector2D
from ..base.MeshData import MeshData
from ..mateial.Material import Material
from ..mateial.MaterialBaseParam import MaterialBaseParam
from ..vo.AnimData import AnimData
from ..role.MaterialAnimShader import MaterialAnimShader


class SkinMesh(ResCount):
    def __init__(self, scene):
        super().__init__(scene)
        self.meshAry = [];
        self.animUrlAry = [];
        self.animDic = {};
        self.fileScale: float = 1.0
        self.tittleHeight: float = 0.0
        self.hitBox: Vector2D = Vector2D();
        self.boneSocketDic:any={};

    def loadMaterial(self, bfun=None):
        for i in range(len(self.meshAry)):
            self.loadByteMeshDataMaterial(self.meshAry[i], bfun);

    def loadByteMeshDataMaterial(self, meshData: MeshData, bfun=None):
        url: str = meshData.materialUrl;
        url = url.replace("_byte.txt", ".txt")
        url = url.replace(".txt", "_byte.txt")

        def backFun(material: Material):
            meshData.material = material;
            if meshData.materialParamData:
                meshData.materialParam = MaterialBaseParam(self.scene3D);
                meshData.materialParam.setData(meshData.material, meshData.materialParamData);

            pass

        self.scene3D.materialManager.getMaterialByte(url, backFun, None, True, MaterialAnimShader.MATERIAL_ANIM_SHADER,
                                                     MaterialAnimShader)

    def addMesh(self, mesh: MeshData):
        mesh.uid = len(self.meshAry);
        self.meshAry.append(mesh);
        pass

    def setAction(self, actionAry, roleUrl):
        self.animUrlAry = [];
        for i in range(len(actionAry)):
            name: str = actionAry[i];
            url: str = roleUrl + actionAry[i];
            anim: AnimData = self.scene3D.animManager.getAnimDataImmediate(url);
            self.animDic[name] = anim;
            self.animUrlAry.append(url);

        pass

#  public setAction(actionAry: Array<string>, roleUrl: string): void {
#             this.animUrlAry = new Array;
#             for (var i: number = 0; i < actionAry.length; i++) {
#                 var name: string = actionAry[i];
#                 var url: string = roleUrl + actionAry[i];
#                 var anim: AnimData = this.scene3D.animManager.getAnimDataImmediate(url);
#                 // anim.processMesh(this);
#                 this.animDic[name] = anim;
#                 this.animUrlAry.push(url);
#             }
#         }
