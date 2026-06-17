from ..base.ResGC import ResGC
from ..core.Pan3dByteArray import Pan3dByteArray
from ..vo.SkinMesh import SkinMesh
from ..vo.BoneSocketData import BoneSocketData
from ..core.Vector2D import Vector2D
from ..base.MeshData import MeshData
from ..res.RoleRes import RoleRes


class MeshDataManager(ResGC):
    def __init__(self, scene):
        super().__init__(scene)
        self.loadDic = {}

    def getMeshData(self, url, bfun):
        batchNum = 1
        if url in self.dic:
            bfun(self.dic[url])
            return
        if url in self.loadDic:
            self.loadDic[url].append(bfun)
            return
        self.loadDic[url] = []
        self.loadDic[url].append(bfun)

        def tempFun(roleRes: RoleRes):
            self.roleResCom(roleRes, bfun)

        self.scene3D.resManager.loadRoleRes(url, tempFun, batchNum)

        # pass

    def roleResCom(self, roleRes: RoleRes, bfun):

        url: str = roleRes.roleUrl;
        skinMesh: SkinMesh = self.dic[url];
        skinMesh.loadMaterial();
        skinMesh.setAction(roleRes.actionAry, url);

        for i in range(len(self.loadDic[url])):
            self.loadDic[url][i](self.dic[url])
        del self.loadDic[url]

    # public readData(byte, $batchNum, $url, $version): SkinMesh {
    def readData(self, byte: Pan3dByteArray, batchNum, url, version):
        skinMesh: SkinMesh = SkinMesh(self.scene3D)
        skinMesh.fileScale = byte.readFloat();
        skinMesh.tittleHeight = byte.readFloat();
        skinMesh.hitBox = Vector2D(byte.readFloat(), byte.readFloat())
        meshNum: int = byte.readInt();

        for i in range(meshNum):
            meshData: MeshData = MeshData(self.scene3D);

            meshData.bindPosAry = self.readBindPosByte(byte)
            meshData.getBindPosMatrix();

            self.readMesh2OneBuffer(byte, meshData);

            meshData.treNum = len(meshData.indexs);
            meshData.materialUrl = byte.readUTF();
            meshData.materialParamData = self.readMaterialParamData(byte)

            particleNum: int = byte.readInt();
            for j in range(particleNum):
                print('角色特效了解一下')
                pass
            skinMesh.addMesh(meshData)

        sokcetLenght: int = byte.readInt();
        for j in range(sokcetLenght):
            boneData: BoneSocketData = BoneSocketData();
            boneData.name = byte.readUTF();
            boneData.boneName = byte.readUTF();
            boneData.index = byte.readInt();
            boneData.x = byte.readFloat();
            boneData.y = byte.readFloat();
            boneData.z = byte.readFloat();
            boneData.rotationX = byte.readFloat();
            boneData.rotationY = byte.readFloat();
            boneData.rotationZ = byte.readFloat();
            skinMesh.boneSocketDic[boneData.name] = boneData;

        self.dic[url] = skinMesh;
        return skinMesh;

    def readMesh2OneBuffer(self, byte: Pan3dByteArray, meshData: MeshData):
        len: int = byte.readInt()
        typeItem = []
        dataWidth: int = 0;
        for i in range(5):
            tf: bool = byte.readBoolean();
            typeItem.append(tf);
            if tf:
                if i == 1:
                    dataWidth += 2;
                else:
                    dataWidth += 3;

        dataWidth += 8;
        len *= dataWidth * 4;

        uvsOffsets: int = 3;
        normalsOffsets: int = uvsOffsets + 2;
        tangentsOffsets: int = normalsOffsets + 3;
        bitangentsOffsets: int = tangentsOffsets + 3;
        boneIDOffsets: int = 0;
        boneWeightOffsets: int = boneIDOffsets + 4;
        if typeItem[2]:
            if typeItem[4]:
                boneIDOffsets = bitangentsOffsets + 3;
            else:
                boneIDOffsets = normalsOffsets + 3;
        else:
            boneIDOffsets = uvsOffsets + 2;

        vertices = self.readBytes2ArrayBuffer(byte, 3);
        uvs = self.readBytes2ArrayBuffer(byte, 2);
        normals = self.readBytes2ArrayBuffer(byte, 3);
        tangents = self.readBytes2ArrayBuffer(byte, 3);
        bitangents = self.readBytes2ArrayBuffer(byte, 3);
        boneIDAry = self.readBytes2ArrayBuffer(byte, 4, 2);
        boneWeightAry = self.readBytes2ArrayBuffer(byte, 4, 1);

        indexs = self.readIntForTwoByte(byte);
        boneNewIDAry = self.readIntForTwoByte(byte);

        meshData.compressBuffer = True;
        meshData.uvsOffsets = uvsOffsets * 4;
        meshData.normalsOffsets = normalsOffsets * 4;
        meshData.tangentsOffsets = tangentsOffsets * 4;
        meshData.bitangentsOffsets = bitangentsOffsets * 4;
        meshData.boneIDOffsets = boneIDOffsets * 4;
        meshData.boneWeightOffsets = boneWeightOffsets * 4;

        meshData.stride = dataWidth * 4;

        meshData.vertices = vertices;
        meshData.uvs = uvs;
        meshData.boneIDAry = boneIDAry;
        meshData.boneWeightAry = boneWeightAry;
        meshData.indexs = indexs;
        meshData.boneNewIDAry = boneNewIDAry;
        meshData.upToGPU()

    def readBindPosByte(self, byte):
        bindPosAry = []
        bindPosLength = byte.readInt();

        for i in range(bindPosLength):
            arr = [byte.readFloat(), byte.readFloat(), byte.readFloat(), byte.readFloat(), byte.readFloat(),
                   byte.readFloat()]
            bindPosAry.append(arr);

        return bindPosAry;
