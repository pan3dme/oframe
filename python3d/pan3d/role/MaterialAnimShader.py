from ..program.Shader3D import Shader3D


class MaterialAnimShader(Shader3D):
    MATERIAL_ANIM_SHADER = 'MaterialAnimShader';

    def __init__(self, scene):
        super().__init__(scene)

    def getVertexShaderString(self):
        usePbr: bool = self.paramAry[0];
        useNormal: bool = self.paramAry[1];
        hasFresnel: bool = self.paramAry[2];
        useDynamicIBL: bool = self.paramAry[3];
        lightProbe: bool = self.paramAry[4];
        directLight: bool = self.paramAry[5];
        noLight: bool = self.paramAry[6];

        str: str = """
        #version 330 core
        layout (location = 0) in vec3 pos;
        layout (location = 1) in vec2 v2Uv;
        layout (location = 2) in vec4 boneID;
        layout (location = 3) in vec4 boneWeight;
        
        varying vec2 v0;
        uniform vec4 boneQ[54];
        uniform vec3 boneD[54];
        uniform mat4 vpMatrix3D;
        uniform mat4 posMatrix3D;
          

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
         
        """
        str += """
        void main()
        {   
            v0 = v2Uv;
            vec4 vt0 = getQDdata(vec3(pos.x,pos.y,pos.z));
            vt0.xyz = vt0.xyz*1.0;
            vt0 = posMatrix3D * vt0;
  
        """

        str += """
            vt0 = vpMatrix3D * vt0;
            gl_Position = vt0;
 
        """

        str += """
        }
        """

        return str

    def getFragmentShaderString(self):
        fstr = """
        precision mediump float;
        uniform sampler2D fs0;
        varying vec2 v0;
        void main(void){
            vec4 ft0 = texture2D(fs0,v0);
            vec4 ft1 = vec4(ft0.xyz,1.0);
            vec4 ft2 = vec4(0,0,0,1);
            ft2.xyz = ft1.xyz;
            ft2.w = 1.0;
            gl_FragColor = ft2;
        }
        """

        return fstr
