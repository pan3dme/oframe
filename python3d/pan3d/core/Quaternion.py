from ..core.Matrix3D import Matrix3D
from ..core.Vector3D import Vector3D

import math


class QuaternionPan3d:
    def __init__(self, x: float = 0.0, y: float = 0.0, z: float = 0.0, w: float = 1.0):
        self.x = x;
        self.y = y;
        self.z = z;
        self.w = w;

    def setMd5W(self):
        self.w = 1 - (self.x * self.x + self.y * self.y + self.z * self.z);
        if self.w < 0:
            self.w = 0
        else:
            self.w = -math.sqrt(self.w);

    def fromMatrix(self, matrix3D: Matrix3D):
        m = [0 for _ in range(9)]

        m[0] = matrix3D.m[0][0]
        m[1] = matrix3D.m[0][1]
        m[2] = matrix3D.m[0][2]

        m[3] = matrix3D.m[1][0]
        m[4] = matrix3D.m[1][1]
        m[5] = matrix3D.m[1][2]

        m[6] = matrix3D.m[2][0]
        m[7] = matrix3D.m[2][1]
        m[8] = matrix3D.m[2][2]

        fTrace: float = m[0] + m[4] + m[8];
        fRoot: float;
        out = [0, 0, 0, 0];

        if fTrace > 0.0:

            fRoot = math.sqrt(fTrace + 1.0);
            out[3] = 0.5 * fRoot;
            fRoot = 0.5 / fRoot;
            out[0] = (m[5] - m[7]) * fRoot;
            out[1] = (m[6] - m[2]) * fRoot;
            out[2] = (m[1] - m[3]) * fRoot;
        else:

            i = 0;
            if m[4] > m[0]:
                i = 1;
            if m[8] > m[i * 3 + i]:
                i = 2;
            j: float = (i + 1) % 3;
            k: float = (i + 2) % 3;

            fRoot = math.sqrt(m[i * 3 + i] - m[j * 3 + j] - m[k * 3 + k] + 1.0);
            out[i] = 0.5 * fRoot;
            fRoot = 0.5 / fRoot;
            out[3] = (m[j * 3 + k] - m[k * 3 + j]) * fRoot;
            out[j] = (m[j * 3 + i] + m[i * 3 + j]) * fRoot;
            out[k] = (m[k * 3 + i] + m[i * 3 + k]) * fRoot;

        self.x = out[0]
        self.y = out[1]
        self.z = out[2]
        self.w = out[3]

    def toMatrix3D(self, taget: Matrix3D = None):
        if taget is None:
            taget = Matrix3D();

        out = taget.m
        x: float = self.x;
        y: float = self.y;
        z: float = self.z;
        w: float = self.w;
        x2: float = x + x;
        y2: float = y + y;
        z2: float = z + z;

        xx: float = x * x2;
        yx: float = y * x2;
        yy: float = y * y2;
        zx: float = z * x2;
        zy: float = z * y2;
        zz: float = z * z2;
        wx: float = w * x2;
        wy: float = w * y2;
        wz: float = w * z2;

        out[0][0] = 1 - yy - zz;
        out[0][1] = yx + wz;
        out[0][2] = zx - wy;
        out[0][3] = 0;

        out[1][0] = yx - wz;
        out[1][1] = 1 - xx - zz;
        out[1][2] = zy + wx;
        out[1][3] = 0;

        out[2][0] = zx + wy;
        out[2][1] = zy - wx;
        out[2][2] = 1 - xx - yy;
        out[2][3] = 0;

        out[3][0] = 0;
        out[3][1] = 0;
        out[3][2] = 0;
        out[3][3] = 1;

        return taget;

    def fromAxisAngle(self, axis, angle: float):
        sin_a: float = math.sin(angle / 2);
        cos_a: float = math.cos(angle / 2);

        self.x = axis.x * sin_a;
        self.y = axis.y * sin_a;
        self.z = axis.z * sin_a;
        self.w = cos_a;
        self.normalize();

    def normalize(self, val: float = 1):
        mag: float = val / math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z + self.w * self.w);
        self.x *= mag;
        self.y *= mag;
        self.z *= mag;
        self.w *= mag;

    def toEulerAngles(self):
        target = Vector3D()

        return target;
