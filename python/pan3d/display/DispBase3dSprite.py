from ..display.Display3D import Display3D
from ..base.ObjData import ObjData

from ..program.Shader3D import Shader3D


class DispBase3dShader(Shader3D):
    def __init__(self, scene):
        super().__init__(scene)
        pass

    def getVertexShaderString(self):
        vstr = """

        attribute  vec3 vertexPosition;
        attribute  vec2 uvspos;
        uniform mat4 posMatrix3D;
        uniform mat4 vpMatrix3D;

        out vec2 fragmentTexCoord;

        void main()
        {
             gl_Position = vpMatrix3D* posMatrix3D * vec4(vertexPosition, 1.0);
             fragmentTexCoord = uvspos;
        }
        """

        return vstr

    def getFragmentShaderString(self):
        fstr = """
        #version 330 core
        in vec2 fragmentTexCoord;

        void main() {
             gl_FragColor =   vec4(1,0,0,1) ;

        }
        """

        return fstr


class DispBase3dSprite(Display3D):
    def __init__(self, scene):
        super().__init__(scene)
        self.objData: ObjData = None
        self.shader:Shader3D=None
        self.encodeBaseShader()

    def encodeBaseShader(self):
        self.shader = DispBase3dShader(self)
        self.shader.encode()

    def setVc(self):
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'posMatrix3D', self.getPosMatrix())
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'vpMatrix3D', self.scene3D.camera3D.vpMatrix3D)

    def upData(self):
        self.scene3D.context3D.setProgram(self.shader);
        self.setVc()
        self.scene3D.context3D.setVaOld(self.objData.vbo, 0, 3, 20, 0)
        self.scene3D.context3D.setVaOld(self.objData.vbo, 1, 2, 20, 12)
        self.scene3D.context3D.drawCallOld(self.objData.eboID, len(self.objData.indexs))

        pass
