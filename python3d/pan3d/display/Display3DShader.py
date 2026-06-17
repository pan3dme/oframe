from ..program.Shader3D import Shader3D


class Display3DShader(Shader3D):
    def __init__(self, scene):
        super().__init__(scene)
        pass

    def getVertexShaderString(self):
        vstr = """
       
        attribute  vec3 vertexPosition;
        attribute  vec2 vetexCoord;
        uniform mat4 model;
        uniform mat4 viewMatrix;
   
        out vec2 fragmentTexCoord;
    
        void main()
        {
             gl_Position = viewMatrix* model * vec4(vertexPosition, 1.0);
             fragmentTexCoord = vetexCoord;
        }
        """

        return vstr

    def getFragmentShaderString(self):
        fstr = """
        #version 330 core
        in vec2 fragmentTexCoord;
    
        uniform sampler2D imageTexture;
        void main() {
             gl_FragColor =   texture(imageTexture,fragmentTexCoord) ;
             
        }
        """

        return fstr

