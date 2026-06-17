from OpenGL.GL import *
import numpy as np
import glm

from OpenGL.GL import *
from ..display.Display3D import Display3D
from ..mateial.Material import Material
from ..mateial.TextureRes import TextureRes
from ..program.ProgrmaManager import ProgrmaManager


class DispBaseTri(Display3D):
    def __init__(self, scene):
        super().__init__(scene)

        self.shader = self.scene3D.progrmaManager.createShaderPath('shaders/vertex.txt', 'shaders/fragment.txt')

        self.vertices = (
            -0.5, -0.5, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0,
            0.5, -0.5, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,
            0.0, 0.5, 0.0, 0.0, 0.0, 1.0, 0.5, 0.0,
        )
        self.vertices = np.array(self.vertices, dtype=np.float32);
        self.vertex_count = 3;

        self.vao = glGenVertexArrays(1);

        glBindVertexArray(self.vao);

        self.vbo = glGenBuffers(1);
        glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
        glBufferData(GL_ARRAY_BUFFER, self.vertices.nbytes, self.vertices, GL_STATIC_DRAW)

        glEnableVertexAttribArray(0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, ctypes.c_void_p(0));
        glEnableVertexAttribArray(1)
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, ctypes.c_void_p(12));
        glEnableVertexAttribArray(2)
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, ctypes.c_void_p(20));

        self.wood_texture = TextureRes(self.scene3D,"abc.jpg")





    def upData(self):
        self.scaleX=100
        self.scaleY=100
        self.scaleZ=100
        self.scene3D.context3D.setProgram(self.shader)
        self.scene3D.context3D.setRenderTexture(self.shader, 'imageTexture', 0, self.wood_texture)

        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'model', self.getPosMatrix())
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'modelMatrix', self.scene3D.camera3D.vpMatrix3D)
 
        glBindVertexArray(self.vao);
        glDrawArrays(GL_TRIANGLES, 0, self.vertex_count);





    def destroy(self):
        glDeleteVertexArrays(1, (self.vao,))
        glDeleteBuffers(1, (self.vbo,))
