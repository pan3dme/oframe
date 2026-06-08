from ..program.Shader3D import Shader3D


class Display3dMovieShader(Shader3D):
    def __init__(self, scene):
        super().__init__(scene)

    def getVertexShaderString(self):
        vstr = """
        #version 330 core
        layout (location = 0) in vec3 pos;
        layout (location = 1) in vec2 v2Uv;
        layout (location = 2) in vec4 boneID;
        layout (location = 3) in vec4 boneWeight;
        uniform mat4 model;
        uniform mat4 viewMatrix;
 
        
        
        uniform vec4 boneQ[54];
        uniform vec3 boneD[54];
 
        
        out vec2 fragmentTexCoord;
        out vec4 testColor;
 
        
        vec4 qdv(vec4 q,vec3 d, vec3 v ){  
            vec3 t = 2.0 * cross(q.xyz, v); 
            vec3 f = v + q.w * t + cross(q.xyz, t); 
            return  vec4(f.x+d.x,f.y+d.y,f.z+d.z,1.0); 
        } 
        vec4 getQDdata(vec3 vdata){ 
        
            vec4 tempnum = qdv(boneQ[int(boneID.x)],boneD[int(boneID.x)],vdata) * boneWeight.x;
            tempnum += qdv(boneQ[int(boneID.y)],boneD[int(boneID.y)],vdata) * boneWeight.y;
            tempnum += qdv(boneQ[int(boneID.z)],boneD[int(boneID.z)],vdata)* boneWeight.z;
            tempnum += qdv(boneQ[int(boneID.w)],boneD[int(boneID.w)],vdata) * boneWeight.w;
           
            tempnum.x = tempnum.x*-1.0; 
            return  tempnum; 
        } 

        void main()
        {   
      
             gl_Position = viewMatrix* model *  getQDdata(pos);
             testColor=boneQ[2];
             fragmentTexCoord = v2Uv;
        }
        """

        return vstr

    def getFragmentShaderString(self):
        fstr = """
        #version 330 core
 
        in vec2 fragmentTexCoord;
        in vec4 testColor;
        
        out vec4 color;
        uniform sampler2D imageTexture;
        void main() {
             color = testColor;
             color =   texture(imageTexture,fragmentTexCoord) ;
              
            
   
        }
        """

        return fstr
