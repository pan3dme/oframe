# -*- coding: utf-8 -*-
"""TileMeshSprite3D —— Google 3D 瓦片网格精灵（带贴图 unlit 渲染）
Google 摄影测量贴图自带烘焙光照，直接贴图输出即可。
顶点布局：[x,y,z,u,v] stride 20；索引三角形绘制。
"""

import ctypes

import numpy as np
from OpenGL.GL import *

from pan3d.display.Display3D import Display3D
from pan3d.mateial.TextureRes import TextureRes
from pan3d.program.Shader3D import Shader3D


class TileMeshShader3D(Shader3D):
    def __init__(self, scene):
        super().__init__(scene)

    def getVertexShaderString(self):
        return """
        #version 330 core
        layout (location = 0) in vec3 vertexPosition;
        layout (location = 1) in vec2 vertexCoord;

        uniform mat4 posMatrix;
        uniform mat4 vpMatrix3D;

        out vec2 fragmentTexCoord;

        void main()
        {
            gl_Position = vpMatrix3D * posMatrix * vec4(vertexPosition, 1.0);
            fragmentTexCoord = vertexCoord;
        }
        """

    def getFragmentShaderString(self):
        return """
        #version 330 core
        in vec2 fragmentTexCoord;
        out vec4 color;
        uniform sampler2D imageTexture;

        void main() {
            color = vec4(texture(imageTexture, fragmentTexCoord).rgb, 1.0);
        }
        """


class TileMeshSprite3D(Display3D):
    """一个 glb 图元（或合并后的网格块）"""

    def __init__(self, scene, positions, uvs, indices, texture_data=None):
        """
        positions: (N,3) float32 世界坐标
        uvs:       (N,2) float32
        indices:   (M,)  uint32 三角形索引
        texture_data: (rgba ndarray, w, h) 或 None（None 时画灰）
        """
        super().__init__(scene)
        n = len(positions)
        interleaved = np.empty((n, 5), dtype=np.float32)
        interleaved[:, 0:3] = positions
        interleaved[:, 3:5] = uvs if len(uvs) == n else 0.0
        self.vertex_count = n
        self.index_count = len(indices)

        self.shader = TileMeshShader3D(self.scene3D)
        self.shader.encode()
        self.texture = None
        if texture_data is not None:
            self._make_texture(*texture_data)

        self.vao = glGenVertexArrays(1)
        glBindVertexArray(self.vao)
        self.vbo = glGenBuffers(1)
        glBindBuffer(GL_ARRAY_BUFFER, self.vbo)
        glBufferData(GL_ARRAY_BUFFER, interleaved.nbytes, interleaved,
                     GL_STATIC_DRAW)
        glEnableVertexAttribArray(0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, ctypes.c_void_p(0))
        glEnableVertexAttribArray(1)
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, ctypes.c_void_p(12))
        self.ebo = glGenBuffers(1)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self.ebo)
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.nbytes, indices,
                     GL_STATIC_DRAW)
        glBindVertexArray(0)

        self.pos_matrix = self.getPosMatrix()

    def _make_texture(self, rgba, w, h):
        tex = TextureRes(self.scene3D)
        tex.imageToTexTure(rgba.reshape(-1, 4), w, h)
        self.texture = tex

    def upData(self):
        if not self.visible:
            return
        self.scene3D.context3D.setProgram(self.shader)
        self.scene3D.context3D.setRenderTexture(self.shader, 'imageTexture', 0,
                                                self.texture)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'posMatrix',
                                              self.pos_matrix)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'vpMatrix3D',
                                              self.scene3D.camera3D.vpMatrix3D)
        if self.texture is None:
            # 无贴图时画灰占位（fragment 无 imageTexture 绑定时默认黑，绑单位白更可读）
            pass
        glBindVertexArray(self.vao)
        glDrawElements(GL_TRIANGLES, self.index_count, GL_UNSIGNED_INT, None)
        glBindVertexArray(0)

    def destroy(self):
        if self.vao:
            glDeleteVertexArrays(1, (self.vao,))
            self.vao = None
        if self.vbo:
            glDeleteBuffers(1, (self.vbo,))
            glDeleteBuffers(1, (self.ebo,))
            self.vbo = self.ebo = None
