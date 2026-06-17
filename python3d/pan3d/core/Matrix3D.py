import math

import glm
from ..core.Vector3D import Vector3D


class Matrix3D:

    def __init__(self):
        self.m = glm.mat4();

    def initBaseMatrixData(self, value):
        base = value

        self.m[0][0] = float(base[0][0])
        self.m[0][1] = float(base[1][0])
        self.m[0][2] = float(base[2][0])
        self.m[0][3] = float(base[3][0])

        self.m[1][0] = float(base[0][1])
        self.m[1][1] = float(base[1][1])
        self.m[1][2] = float(base[2][1])
        self.m[1][3] = float(base[3][1])

        self.m[2][0] = float(base[0][2])
        self.m[2][1] = float(base[1][2])
        self.m[2][2] = float(base[2][2])
        self.m[2][3] = float(base[3][2])

        self.m[3][0] = float(base[0][3])
        self.m[3][1] = float(base[1][3])
        self.m[3][2] = float(base[2][3])
        self.m[3][3] = float(base[3][3])

    def identity(self):
        base = glm.mat4();
        self.m[0][0] = base[0][0]
        self.m[0][1] = base[0][1]
        self.m[0][2] = base[0][2]
        self.m[0][3] = base[0][3]

        self.m[1][0] = base[1][0]
        self.m[1][1] = base[1][1]
        self.m[1][2] = base[1][2]
        self.m[1][3] = base[1][3]

        self.m[2][0] = base[2][0]
        self.m[2][1] = base[2][1]
        self.m[2][2] = base[2][2]
        self.m[2][3] = base[2][3]

        self.m[3][0] = base[3][0]
        self.m[3][1] = base[3][1]
        self.m[3][2] = base[3][2]
        self.m[3][3] = base[3][3]

    def changeTestData(self):
        self.m[0][0] = 1.1889365911483765

        self.m[0][1] = -0.4576219320297241

        self.m[0][2] = -0.433880478143692

        self.m[0][3] = -0.4330126941204071

        self.m[1][0] = 0

        self.m[1][1] = 1.5852488279342651

        self.m[1][2] = -0.5010020136833191

        self.m[1][3] = -0.5

        self.m[2][0] = 0.6864328980445862

        self.m[2][1] = 0.7926244139671326

        self.m[2][2] = 0.7515029907226562

        self.m[2][3] = 0.75

        self.m[3][0] = 0

        self.m[3][1] = 0

        self.m[3][2] = 490.9819641113281

        self.m[3][3] = 500

        pass

    def appendScale(self, x, y, z):
        self.m = glm.mul(glm.scale((x, y, z)), self.m)
        # self.m = glm.scale(self.m, (x, y, z));

    def prependScale(self, x, y, z):
        self.m = glm.mul(self.m, glm.scale((x, y, z)))

    def appendTranslation(self, x=0, y=0, z=0):
        base = glm.translate(glm.mat4(), (x, y, z));
        self.m = glm.mul(base, self.m)

    def prependTranslation(self, x=0, y=0, z=0):
        base = glm.translate(glm.mat4(), (x, y, z));
        self.m = glm.mul(self.m, base)
        pass

    def appendRotation(self, rad=0, axis=(0, 0, 0)):
        base = glm.rotate(glm.mat4(), rad * math.pi / 180, axis);
        self.m = glm.mul(base, self.m)

    def prependRotation(self, rad=0, axis=(0, 0, 0)):
        base = glm.rotate(glm.mat4(), rad * math.pi / 180, axis);
        self.m = glm.mul(self.m, base)
        pass

    def transformVector(self, v3d: Vector3D):
        p = v3d;

        out: Vector3D = Vector3D()
        base = [0 for _ in range(16)]

        base[0] = self.m[0][0]
        base[1] = self.m[0][1]
        base[2] = self.m[0][2]
        base[3] = self.m[0][3]

        base[4] = self.m[1][0]
        base[5] = self.m[1][1]
        base[6] = self.m[1][2]
        base[7] = self.m[1][3]

        base[8] = self.m[2][0]
        base[9] = self.m[2][1]
        base[10] = self.m[2][2]
        base[11] = self.m[2][3]

        base[12] = self.m[3][0]
        base[13] = self.m[3][1]
        base[14] = self.m[3][2]
        base[15] = self.m[3][3]

        out.x = base[0] * p.x + base[4] * p.y + base[8] * p.z + base[12] * p.w;
        out.y = base[1] * p.x + base[5] * p.y + base[9] * p.z + base[13] * p.w;
        out.z = base[2] * p.x + base[6] * p.y + base[10] * p.z + base[14] * p.w;
        out.w = base[3] * p.x + base[7] * p.y + base[11] * p.z + base[15] * p.w;

        return out;

    def invert(self):
        self.m = glm.inverse(self.m)

    def append(self, newMat):
        self.m = glm.mul(newMat.m, self.m)

    def prepend(self, newMat):
        self.m = glm.mul(self.m, newMat.m)

    def clone(self, target=None):
        if target is None:
            target = Matrix3D();

        target.m[0][0] = self.m[0][0]
        target.m[0][1] = self.m[0][1]
        target.m[0][2] = self.m[0][2]
        target.m[0][3] = self.m[0][3]

        target.m[1][0] = self.m[1][0]
        target.m[1][1] = self.m[1][1]
        target.m[1][2] = self.m[1][2]
        target.m[1][3] = self.m[1][3]

        target.m[2][0] = self.m[2][0]
        target.m[2][1] = self.m[2][1]
        target.m[2][2] = self.m[2][2]
        target.m[2][3] = self.m[2][3]

        target.m[3][0] = self.m[3][0]
        target.m[3][1] = self.m[3][1]
        target.m[3][2] = self.m[3][2]
        target.m[3][3] = self.m[3][3]

        return target;

    def position(self):
        return Vector3D(self.m[3][0], self.m[3][1], self.m[3][2], self.m[3][3]);

    def outStr(self):
        print('[', self.m[0][0], self.m[0][1], self.m[0][2], self.m[0][3]);
        print(' ', self.m[1][0], self.m[1][1], self.m[1][2], self.m[1][3]);
        print(' ', self.m[2][0], self.m[2][1], self.m[2][2], self.m[2][3]);
        print(' ', self.m[3][0], self.m[3][1], self.m[3][2], self.m[3][3], ']');
        print('----------')

    def toEulerAngles(self):
        c = glm.quat(self.m)
        d = glm.eulerAngles(c)
        target = Vector3D(d.x * 180 / math.pi, d.y * 180 / math.pi, d.z * 180 / math.pi)

        return target;

    def getScaling(self):
        m11 = self.m[0][0];
        m12 = self.m[0][1];
        m13 = self.m[0][2];
        m21 = self.m[1][0];
        m22 = self.m[1][1];
        m23 = self.m[1][2];
        m31 = self.m[2][0];
        m32 = self.m[2][1];
        m33 = self.m[2][2];

        a = math.sqrt(m11 * m11 + m12 * m12 + m13 * m13);
        b = math.sqrt(m21 * m21 + m22 * m22 + m23 * m23);
        c = math.sqrt(m31 * m31 + m32 * m32 + m33 * m33);

        return Vector3D(a, b, c);
