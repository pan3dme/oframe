import glm

from ..core.Object3D import Object3D
from ..core.Matrix3D import Matrix3D
from ..core.Vector3D import Vector3D


class Display3D(Object3D):
    def __init__(self, scene):
        self.scene3D = scene
        self.visible = True
        super().__init__()
        self.posMatrix: Matrix3D = Matrix3D()

        pass

    def getPosMatrix(self):
        self.posMatrix.identity()
        self.posMatrix.appendScale(self.scaleX, self.scaleY, self.scaleZ)

        self.posMatrix.appendRotation(self.rotationZ, Vector3D.Z_AXIS)
        self.posMatrix.appendRotation(self.rotationY, Vector3D.Y_AXIS)
        self.posMatrix.appendRotation(self.rotationX, Vector3D.X_AXIS)

        self.posMatrix.appendTranslation(self.x, self.y, self.z)

        return self.posMatrix

    def updateMatrix(self):
        self.posMatrix.identity();
        self.posMatrix.appendScale(self.scaleX, self.scaleY, self.scaleZ);
        self.posMatrix.appendRotation(self.rotationX, Vector3D.X_AXIS)
        self.posMatrix.appendRotation(self.rotationY, Vector3D.Y_AXIS)
        self.posMatrix.appendRotation(self.rotationZ, Vector3D.Z_AXIS)
        self.posMatrix.appendTranslation(self.x, self.y, self.z);
        pass

    def upData(self):
        pass
