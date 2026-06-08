from ..res.ResCount import ResCount
from OpenGL.GL import *
from OpenGL.GL.shaders import compileProgram, compileShader


class Shader3D(ResCount):
    def __init__(self, scene):
        super().__init__(scene)
        self.program = None;
        self.paramAry: list = None;
        self.vertex: str = '';
        self.fragment = self.getFragmentShaderString()

    def getVertexShaderString(self):
        return ''

    def getFragmentShaderString(self):
        return ''

    def encode(self, v: str=None, f: str=None):
        if v:
            self.vertex = v
        else:
            self.vertex = self.getVertexShaderString()

        # if f:
        #     self.fragment = f;
        # else:
        #     self.fragment = self.getFragmentShaderString();



        self.program = self.createProgamByStr(self.vertex, self.fragment)

    def createProgamByStr(self, vertex_src, fragment_src):


        v_str = vertex_src.replace("#version 330 core", "")
        f_str = fragment_src.replace("#version 330 core", "")
        v_str="#version 330 core\n"+v_str
        f_str="#version 330 core\n"+f_str

        programe = compileProgram(
            compileShader(v_str, GL_VERTEX_SHADER),
            compileShader(f_str, GL_FRAGMENT_SHADER)

        )
        return programe;
