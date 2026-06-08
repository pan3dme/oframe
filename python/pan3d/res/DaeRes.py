import collada
import glm

import numpy as np
from ..res.BaseRes import BaseRes
from ..core.Matrix3D import Matrix3D
from ..core.Vector3D import Vector3D
from ..mateial.daematerial.DaeMaterial import DaeMaterial
from ..display.DaeDisplay3DSprite import DaeDisplay3DSprite
from collada.camera import Camera

from collada import *


class DaeRes(BaseRes):
    def __init__(self, scene):
        super().__init__(scene)
        self.daeSpriteItem = [];

        self.fun = None;

    def type_to_str(self, obj):
        return type(obj).__name__

    def findmateriaByArr(self, arr, nameStr):
        for temp in arr:
            if temp.materialName == nameStr:
                return temp
                pass
            pass
        pass

    def load(self, url: str, bfun):
        self.fun = bfun;
        mesh = collada.Collada(url)
        tex_paths = [im.path for im in mesh.images]  # gets a list of the texture paths
        geometries = [g for g in mesh.geometries]
        materialArr = []
        for ma in mesh.materials:
            daeMaterial: DaeMaterial = DaeMaterial(self.scene3D)
            daeMaterial.setMeshMaterials(ma, url)
            materialArr.append(daeMaterial)

            pass
        for noden in mesh.scene.nodes:
            for geometryNode in noden.children:
                if self.type_to_str(geometryNode) == 'GeometryNode':
                    for geom in geometries:
                        if geom.name == geometryNode.geometry.name:
                            for prim in geom.primitives:
                                daeDis: DaeDisplay3DSprite = DaeDisplay3DSprite(self.scene3D)
                                daeDis.daeMaterial = self.findmateriaByArr(materialArr, prim.material)
                                daeDis.name = geom.name;
                                daeDis.makeDaeToObjData(prim);
                                self.daeSpriteItem.append(daeDis);
                                self.setDaeDisplayPostion(daeDis, noden)
                                pass
                        pass

                    pass

                pass

            pass

        self.loadComplete()

    def setDaeDisplayPostion(self, daeDis, noden):
        matrix3D: Matrix3D = Matrix3D()
        matrix3D.initBaseMatrixData(noden.matrix);

        eulerv3 = matrix3D.toEulerAngles()
        scalev3 = matrix3D.getScaling()

        pos = matrix3D.position()
        pos.scaleBy(10.0)
        daeDis.x = pos.x * -1.0;
        daeDis.y = pos.y;
        daeDis.z = pos.z;
        daeDis.rotationX = -eulerv3.x;
        daeDis.rotationY = -eulerv3.y;
        daeDis.rotationZ = -eulerv3.z;

        daeDis.scaleX = scalev3.x;
        daeDis.scaleY = scalev3.y;
        daeDis.scaleZ = scalev3.z;

    def loadComplete(self):
        self.fun();
        pass
