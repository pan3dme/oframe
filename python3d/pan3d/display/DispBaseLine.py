from OpenGL.GL import *
import numpy as np
import glm

from OpenGL.GL import *
from ..display.Display3D import Display3D
from ..program.ProgrmaManager import ProgrmaManager
from ..core.Vector3D import Vector3D
from ..core.Matrix3D import Matrix3D


class DispBaseLine(Display3D):
    def __init__(self, scene):
        super().__init__(scene)
        self.vertices = [];
        self.vertex_count = 0;


        self.shader = self.scene3D.progrmaManager.createShaderPath('shaders/vertex001.txt', 'shaders/fragment001.txt')

        self.vao = glGenVertexArrays(1);
        glBindVertexArray(self.vao);
        self.vbo = glGenBuffers(1);
        glBindBuffer(GL_ARRAY_BUFFER, self.vbo);

        w = 100;
        n = 10;
        skeep = w / n;
        baseColor = Vector3D(128 / 255, 128 / 255, 128 / 255)
        self.makeLineMode(Vector3D(0, 0, -w), Vector3D(0, 0, w), Vector3D(1, 0, 0));
        self.makeLineMode(Vector3D(w, 0, 0), Vector3D(-w, 0, 0), Vector3D(0, 1, 0));
        for idx in range(n):
            tx = skeep * (idx + 1)
            self.makeLineMode(Vector3D(tx, 0, -w), Vector3D(tx, 0, w), baseColor);
            self.makeLineMode(Vector3D(-tx, 0, -w), Vector3D(-tx, 0, w), baseColor);
            self.makeLineMode(Vector3D(-w, 0, -tx), Vector3D(w, 0, -tx), baseColor);
            self.makeLineMode(Vector3D(-w, 0, +tx), Vector3D(w, 0, +tx), baseColor);

        self.upGpu()

    def upGpu(self):
        self.vertices = np.array(self.vertices, dtype=np.float32);

        glBufferData(GL_ARRAY_BUFFER, self.vertices.nbytes, self.vertices, GL_STATIC_DRAW)

        glEnableVertexAttribArray(0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, ctypes.c_void_p(0));

        glEnableVertexAttribArray(1)
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, ctypes.c_void_p(12));

    def makeLineMode(self, a, b, c):
        self.vertex_count = self.vertex_count + 2;
        vers = self.vertices

        vers.append(a.x)
        vers.append(a.y)
        vers.append(a.z)

        vers.append(c.x)
        vers.append(c.y)
        vers.append(c.z)

        vers.append(b.x)
        vers.append(b.y)
        vers.append(b.z)

        vers.append(c.x)
        vers.append(c.y)
        vers.append(c.z)

    def upData(self):
        self.scene3D.context3D.setProgram(self.shader)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'model', self.getPosMatrix())
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'modelMatrix', self.scene3D.camera3D.vpMatrix3D)

        glBindVertexArray(self.vao);
        glDrawArrays(GL_LINES, 0, self.vertex_count);

    def getPosMatrix(self):
        return Matrix3D()
        # return glm.rotate(self.rotationY * 3.14 / 180, glm.vec3(0.0, 1.0, 0.0))

    def destroy(self):
        glDeleteVertexArrays(1, (self.vao,))
        glDeleteBuffers(1, (self.vbo,))
