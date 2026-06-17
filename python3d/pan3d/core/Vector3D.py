import math


class Vector3D:
    X_AXIS = (1, 0, 0);
    Y_AXIS = (0, 1, 0);
    Z_AXIS = (0, 0, 1);

    def __init__(self, tx=0, ty=0, tz=0, tw=0):
        self.x = tx;
        self.y = ty;
        self.z = tz;
        self.w = tw;

    def scaleBy(self, value):
        self.x *= value;
        self.y *= value;
        self.z *= value;

        pass

    def scaleByW(self):
        self.x *= self.w;
        self.y *= self.w;
        self.z *= self.w;

    def add(self, value):
        return Vector3D(value.x + self.x, value.y + self.y, value.z + self.z)

    def addByNum(self, x: float, y: float, z: float, w: float = 0):
        self.x += x;
        self.y += y;
        self.z += z;
        self.w += w;

    def setTo(self, x: float, y: float, z: float):
        self.x = x;
        self.y = y;
        self.z = z;

    def length(self):
        return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z);

    def dot(self, value):
        return self.x * value.x + self.y * value.y + self.z * value.z;

    def normalize(self):
        le: float = self.length();
        if le == 0:
            return;

        self.scaleBy(1 / le);
        pass

    def cross(self, value):
        return Vector3D(self.y * value.z - self.z * value.y,
                        self.z * value.x - self.x * value.z,
                        self.x * value.y - self.y * value.x);

    def toStr(self):
        return str(self.x) + ' ' + str(self.y) + ' ' + str(self.z)
