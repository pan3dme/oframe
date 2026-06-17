from ...program.Shader3D import Shader3D


class DaeMaterialShader(Shader3D):
    def __init__(self, scene):
        super().__init__(scene)
        pass

    def getVertexShaderString(self):
        vstr = """

        layout (location = 0) in  vec3 v3Position;
        layout (location = 1) in  vec3 v3Normal;
        layout (location = 2) in  vec2 v2CubeTexST;
        
        varying vec2 v0;
        varying vec3 v1;
        varying vec3 v2;
        varying vec3 normal;
            
        uniform vec3 sunDirect;
        uniform mat4 posMatrix3D;
        uniform mat4 vpMatrix3D;

   

        void main()
        {
             v0 = v2CubeTexST;
             normal = v3Normal;
             gl_Position = vpMatrix3D* posMatrix3D * vec4(v3Position, 1.0);
             float suncos=clamp(dot(normal.xyz,sunDirect.xyz),0.0,1.0); 
             v2 = vec3(0.8,0.8,0.8)*suncos;
             
        }
        """

        return vstr

    def getFragmentShaderString(self):
        fstr = """
        #version 330 core
        
        uniform sampler2D fs0;
        varying vec2 v0;
        varying vec3 v1;
        varying vec3 v2;
        varying vec3 normal;
        
        

        void main() {
            vec4 ft0 = texture2D(fs0,v0);
            vec4 colorend =vec4((v2.xyz+vec3(  0.2,  0.2,  0.2))*ft0.xyz, 1); 
            gl_FragColor = colorend ;

        }
        """

        return fstr
