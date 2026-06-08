from ..core.Vector3D import Vector3D


class Object3D(Vector3D):
    def __init__(self):
        super().__init__();
        self.rotationX = 0;
        self.rotationY = 0;
        self.rotationZ = 0;
        self.scaleX = 1;
        self.scaleY = 1;
        self.scaleZ = 1;
        pass
