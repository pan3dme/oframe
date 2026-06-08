from ..program.Shader3D import Shader3D


class MaterialShader(Shader3D):
    MATERIAL_SHADER = 'Material_shader';

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
        fogMode: int = self.paramAry[7];

        vstr = """

            layout (location = 0) in  vec3 v3Position;
            layout (location = 1) in  vec2 v2CubeTexST;
            varying vec2 v0;
           """

        if directLight:
            vstr = vstr + """
            varying vec3 v2;
                       """

        elif noLight:
            pass
        else:
            vstr = vstr + """
            layout (location = 2) in  vec2 v2lightuv;
            varying vec2 v2;;
                     """

        if fogMode != 0:
            vstr = vstr + """
            varying vec3 v1;
                                      """

        vstr = vstr + """
            uniform mat4 posMatrix3D;
            uniform mat4 vpMatrix3D;
              """

        vstr = vstr + """
           void main()
           {
                v0 = vec2(v2CubeTexST.x, v2CubeTexST.y);
                vec4 vt0= vec4(v3Position, 1.0);
                vt0 = posMatrix3D * vt0;

         """

        if fogMode != 0:
            vstr = vstr + """
                v1 = vec3(vt0.x,vt0.y,vt0.z);
                     """

        if noLight == False:
            vstr = vstr + """
                v2 =vec2(v2lightuv.x, v2lightuv.y);
                     """

        vstr = vstr + """
                gl_Position = vpMatrix3D*  vt0;
                       
                   }
                 """

        vstrCOPY = """

            layout (location = 0) in  vec3 v3Position;
            layout (location = 1) in  vec2 v2CubeTexST;
            varying vec2 v0;
            layout (location = 2) vec2 v2lightuv;
            varying vec2 v2;;
            
                     
            uniform mat4 posMatrix3D;
            uniform mat4 vpMatrix3D;
              
           void main()
           {
                gl_Position = vpMatrix3D* posMatrix3D * vec4(v3Position, 1.0);
                v0 = v2CubeTexST;
  
           }
                  
                   """

        return vstr;

    def getFragmentShaderString(self):
        fstr = """
               
            precision mediump float;
            uniform sampler2D fs0;
            uniform sampler2D fs1;
            uniform vec4 fc[3];
            varying vec2 v0;
            varying vec3 v1;
            varying vec2 v2;
            void main(void){
            
                vec4 ft0 = texture2D(fs0,v0);
                vec4 ft1 = texture2D(fs1,v2);
                if(ft0.w<fc[0].x){discard;}
        
                
                gl_FragColor = ft0;
            }
    
               """

        return fstr
