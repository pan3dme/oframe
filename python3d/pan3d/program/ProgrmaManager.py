from OpenGL.GL import *
from OpenGL.GL.shaders import compileProgram, compileShader

from ..base.ResGC import ResGC
from ..program.Shader3D import Shader3D
from ..mateial.Material import Material


class ProgrmaManager(ResGC):
    def __init__(self, scene3D):
        super().__init__(scene3D)
        pass

    def createShaderPath(self, vertexFilePath, fragmentFilepath):
        with open(vertexFilePath, 'r') as f:
            vertex_src = f.readlines();

        with open(fragmentFilepath, 'r') as f:
            fragment_src = f.readlines();

        return self.createShaderByStr(vertex_src, fragment_src);

    def createShaderByStr(self, vertex_src, fragment_src):
        shader: Shader3D = Shader3D(self.scene3D)
        shader.program = compileProgram(
            compileShader(vertex_src, GL_VERTEX_SHADER),
            compileShader(fragment_src, GL_FRAGMENT_SHADER)

        )
        return shader;

    def getMaterialProgram(self, key: str, shaderCls: any, material: Material, paramAry: any = None,
                           parmaByFragmet: bool = False):

        keyStr: str = key + "_" + material.url;
        if paramAry:
            for i in range(len(paramAry)):
                keyStr += "_" + str(paramAry[i]);

            if parmaByFragmet:
                keyStr += "true_";
            else:
                keyStr += "false_";

        if keyStr in self.dic:
            self.dic[keyStr].useNum += 1;
            return self.dic[keyStr]

        if parmaByFragmet:
            paramAry = [material.usePbr, material.useNormal, material.hasFresnel, material.useDynamicIBL,
                        material.lightProbe, material.directLight, material.noLight, material.fogMode];

        shader: Shader3D = shaderCls(self.scene3D);
        shader.paramAry = paramAry;
        shader.fragment = material.shaderStr;

        if keyStr.find('Display3DModelShader44') != -1:
            print(shader.getVertexShaderString())
            print('-------')
            print(shader.fragment)
            shader.fragment = shader.getFragmentShaderString()
            print(shader.fragment)


            pass

        encodetf: bool = shader.encode();

        return shader;
