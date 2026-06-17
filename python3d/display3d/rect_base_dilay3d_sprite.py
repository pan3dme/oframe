from OpenGL.GL import *
import numpy as np
import ctypes

from pan3d.display.Display3D import Display3D
from pan3d.mateial.TextureRes import TextureRes
from pan3d.core.Vector3D import Vector3D

class RectBaseDisplay3d(Display3D):
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
        self.indices = None
        self.index_count = 0
        self.use_indices = False

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
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, ctypes.c_void_p(24));

        # self.wood_texture = TextureRes(self.scene3D, "abc.jpg")
        # self.wood_texture = TextureRes(self.scene3D, "res/blandermap/8-00001-texture.png")

    def setTextureUrl(self,url):

        # self.wood_texture = TextureRes(self.scene3D,"res/blandermap/003/"+url)
        self.wood_texture = TextureRes(self.scene3D,"res/blandermap/004/"+url)




    def upData(self):
        if self.wood_texture ==None:
            return

        self.scene3D.context3D.setProgram(self.shader)
        self.scene3D.context3D.setRenderTexture(self.shader, 'imageTexture', 0, self.wood_texture)

        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'model', self.getPosMatrix())
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'modelMatrix', self.scene3D.camera3D.vpMatrix3D)

        glBindVertexArray(self.vao)

        # 根据是否有索引数据选择渲染方式
        if self.use_indices:
            glDrawElements(GL_TRIANGLES, self.index_count, GL_UNSIGNED_INT, None)
        else:
            glDrawArrays(GL_TRIANGLES, 0, self.vertex_count)

    def destroy(self):
        glDeleteVertexArrays(1, (self.vao,))
        glDeleteBuffers(1, (self.vbo,))
