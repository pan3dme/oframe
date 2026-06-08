from OpenGL.GL import *
import numpy as np
import glm

from OpenGL.GL import *
from ..display.Display3D import Display3D
from ..program.ProgrmaManager import ProgrmaManagerGetInstance
from ..core.Vector3D import Vector3D


class DispBaseLineTest(Display3D):
    def __init__(self, scene):
        super().__init__(scene)
        self.vertices = [];
        self.vertex_count = 0;

        self.scene = scene
        self.shader = ProgrmaManagerGetInstance.createShader('shaders/vertex001.txt', 'shaders/fragment001.txt')

        self.vao = glGenVertexArrays(1);
        glBindVertexArray(self.vao);
        self.vbo = glGenBuffers(1);
        glBindBuffer(GL_ARRAY_BUFFER, self.vbo);



        # self.makeLineMode(Vector3D(0, 0, 0), Vector3D(1, 0, 0), Vector3D(1, 0, 0));
        # self.makeLineMode(Vector3D(0, 0, 0), Vector3D(0, 1, 0), Vector3D(0, 1, 0));
        # self.makeLineMode(Vector3D(0, 0, 0), Vector3D(0 ,0, 1), Vector3D(0, 0, 1));

        self.makeLineMode(Vector3D(-1, -1, -1), Vector3D(1,1, 1), Vector3D(0, 0, 1));



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
        self.scene.context3D.setProgram(self.shader)
        self.scene.context3D.setVcMatrix4fv(self.shader, 'model', self.getPosMatrix())
        self.scene.context3D.setVcMatrix4fv(self.shader, 'view', self.scene.camera3D.viewMatixe)
        self.scene.context3D.setVcMatrix4fv(self.shader, 'projection', self.scene.camera3D.projection)

        glBindVertexArray(self.vao);
        glLineWidth(2.0)
        glDrawArrays(GL_LINES, 0, self.vertex_count);
        glLineWidth(1.0)

    def getPosMatrix(self):
        m=glm.translate(glm.vec3(self.x,self.y,self.z))
        return glm.rotate(m,self.rotationY * 3.14 / 180, glm.vec3(0.0, 1.0, 0.0))

    def destroy(self):
        glDeleteVertexArrays(1, (self.vao,))
        glDeleteBuffers(1, (self.vbo,))
