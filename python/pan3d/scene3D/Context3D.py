from OpenGL.GL import *
import glm
from ..program.Shader3D import Shader3D
from ..core.Matrix3D import Matrix3D


class Context3D:
    def __init__(self):
        pass

    def setVcMatrix4fv(self, shader: Shader3D, name: str, mat: Matrix3D):
        glUniformMatrix4fv(glGetUniformLocation(shader.program, name), 1, GL_FALSE,
                           glm.value_ptr(mat.m))
        pass

    # def setVc4fv(self, shader: Shader3D, name, value, countNum: int = 54):
    def setVc4fv(self, shader: Shader3D, name, value, countNum: int ):
        glUniform4fv(glGetUniformLocation(shader.program, name), countNum, value)
        pass

    def setVc3fv(self, shader: Shader3D, name, value, countNum: int ):
        glUniform3fv(glGetUniformLocation(shader.program, name), countNum, value)
        pass

    def setVc2fv(self, shader: Shader3D, name: str, value):
        glUniform2fv(glGetUniformLocation(shader.program, name), 1, value)

        # gl.uniform2fv($program.getWebGLUniformLocation($name), $m);
        pass

    def setVa(self, vao):
        glBindVertexArray(vao);

    def setVaOld(self, vetbuff, dataId, dataWidth, stride, offset):
        glBindVertexArray(vetbuff);
        glBindBuffer(GL_ARRAY_BUFFER, vetbuff);
        glEnableVertexAttribArray(dataId)
        glVertexAttribPointer(dataId, dataWidth, GL_FLOAT, GL_FALSE, stride, ctypes.c_void_p(offset));

    def drawCall(self, treNum):

        glDrawArrays(GL_TRIANGLES, 0, treNum);

    def drawCallOld(self, indexBuff, idxNum):
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuff)
        glDrawElements(GL_TRIANGLES, idxNum, GL_UNSIGNED_INT, None)

    def setProgram(self, shader: Shader3D):
        glUseProgram(shader.program);
        pass

    def setBaseRender(self):

        glClearColor(60 / 255, 60 / 255, 60 / 255, 1.0);
        glClearDepth(1.0);

        glDepthMask(True);
        glEnable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glFrontFace(GL_CW);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)
        glDisable(GL_CULL_FACE)

        pass

    def setWriteDepth(self, tf: bool):
        glDepthMask(tf)
        pass

    def setBlendParticleFactors(self, type: int):
        if type == 0:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)
        elif type == 1:
            glBlendFunc(GL_ONE, GL_ONE);
        elif type == 2:
            glBlendFunc(GL_DST_COLOR, GL_ZERO);

        elif type == 3:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);

        elif type == 4:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);

        elif type == -1:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        pass

    def disableCullFace(self):
        glDisable(GL_CULL_FACE);
        pass

    def setRenderTexture(self, shader: Shader3D, name, idx, info):

        if idx == 0:
            glActiveTexture(GL_TEXTURE0)
        elif idx == 1:
            glActiveTexture(GL_TEXTURE1)
        elif idx == 2:
            glActiveTexture(GL_TEXTURE2)
        elif idx == 3:
            glActiveTexture(GL_TEXTURE3)
        elif idx == 4:
            glActiveTexture(GL_TEXTURE4)
        elif idx == 5:
            glActiveTexture(GL_TEXTURE5)
        elif idx == 6:
            glActiveTexture(GL_TEXTURE6)
        elif idx == 7:
            glActiveTexture(GL_TEXTURE7)

        if info is None:
            return

        glBindTexture(GL_TEXTURE_2D, info.texture)
        glUniform1i(glGetUniformLocation(shader.program, name), idx)

        pass
