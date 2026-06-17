from ....display.particle.Display3DParticle import Display3DParticle
from ....program.Shader3D import Shader3D
from ....core.Vector3D import Vector3D
from ....scene3D.Scene_data import Scene_data
from ....scene3D.Context3D import Context3D
from ....scene3D.Scene_data import Scene_data


class Display3DLocusShader(Shader3D):
    Display3DLocusShader = 'Display3DLocusShader';

    def getVertexShaderString(self):
        isWatchEye: bool = bool(self.paramAry[0]);
        isUV: bool = bool(self.paramAry[1]);
        hasParticleColor: bool = bool(self.paramAry[2]);

        defineBaseStr: str = """
                    
                    #version 330 core
                    layout (location = 0) in  vec3 v3Position;
                    layout (location = 1) in  vec2 v2TexCoord;
                    layout (location = 2) in  vec4 v3Normal;
                    
                    uniform mat4 modeMatrix;
                    uniform mat4 viewMatrix;
                    uniform mat4 camMatrix;
                
                    uniform vec3 resultUvVec; 
                    uniform vec3 camPosV3d; 
                    uniform vec3 isUv; 
                    
                    
                    varying vec2 v0;
                    varying vec2 v1;
                    varying vec4 v2;
                    
                    
                    """
        mainStr = """
               
                vec2 tempv0 = v2TexCoord; 
                tempv0.x -= resultUvVec.x; 
                float alpha = tempv0.x/resultUvVec.y; 
                alpha = 1.0 - clamp(abs(alpha),0.0,1.0); 
                float kill = -tempv0.x; 
                kill *= tempv0.x - resultUvVec.z; 

                v0 = tempv0; 
                v1 = v2TexCoord;
                v2 = vec4(kill,0.0,0.0,alpha);
               
                vec4 tempPos =  vec4(v3Position.xyz,1.0); 
                
               """

        if isUV:
            mainStr += """
                       tempv0.xy *= isUv.xy; 
                       if(isUv.z >= 0.0){ 
                           vec2 tempv1 = tempv0; 
                           tempv0.y = tempv1.x; 
                           tempv0.x = tempv1.y;
                       } 
                       v0 = tempv0;
                 """

        if isWatchEye:
            mainStr += """
        
                tempPos = modeMatrix * tempPos; 
                vec3 mulPos = vec3(tempPos.x,tempPos.y,tempPos.z); 
                vec3 normals = vec3(v3Normal.x,v3Normal.y,v3Normal.z); 
                mulPos = normalize(vec3(camPosV3d.xyz) - mulPos); 
                mulPos = cross(mulPos, normals); 
                mulPos = normalize(mulPos); 
                mulPos *= v3Normal.w; 
                tempPos.xyz = mulPos.xyz + v3Position.xyz; 
           
               """
            pass

        mainStr += """
                gl_Position = viewMatrix*camMatrix*modeMatrix*vec4(tempPos.xyz,1.0);
                 """

        resultStr: str = defineBaseStr + "void main(void){ " + mainStr + "}";

        return resultStr;



    def getFragmentShaderString(self):
        fstr = """
          
            precision mediump float;
            uniform sampler2D fs0;
            uniform sampler2D fs1;
            uniform vec4 fc[1];
            varying vec2 v0;
            varying vec4 v2;
            varying vec2 v1;
            void main(void){
            
                vec4 ft0 = texture2D(fs0,v0);
                vec4 ft1 = texture2D(fs1,v1);
                ft1.xyz = ft1.xyz * ft1.w;
                vec4 ft2 = ft0 * ft1;
                ft0 = ft2 * v2.w;
                ft1 = vec4(ft0.xyz,1.0);
                ft2.xyz = ft1.xyz;
                ft2.w = ft0.w;
                ft2.xyz = ft2.xyz * ft2.w;
                 if(v2.x<fc[0].x){discard;}
                gl_FragColor =ft2;
            
            }
          
          
               """

        return fstr


class Display3DLocusPartilce(Display3DParticle):
    def __init__(self, scene3D):
        super().__init__(scene3D);

    def update(self):
        if self.visible and (self.data is not None) and (self.scene3D is not None):
            if (self.data.materialParam is not None) and (self.data.materialParam.shader is not None):
                self.shader = self.data.materialParam.shader;
                ctx: Context3D = self.scene3D.context3D
                ctx.setProgram(self.shader);
                ctx.setBlendParticleFactors(self.data.alphaMode);
                self.updateMatrix();
                self.setMaterialVc();
                self.setMaterialTexture();
                self.setVc();
                self.setVa();
                pass

        pass

    def setVc(self):
        self.updateUV();
        ctx: Context3D = self.scene3D.context3D;

        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'modeMatrix', self.modelMatrix)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'viewMatrix', self.scene3D.camera3D.viewMatrixMatrix3D)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'camMatrix', self.scene3D.camera3D.camMatrix)

        locusdata = self.data;
        ctx.setVc3fv(self.shader, "resultUvVec", locusdata.resultUvVec, 1);
        ctx.setVc3fv(self.shader, "camPosV3d",
                     [self.scene3D.camera3D.x, self.scene3D.camera3D.y, self.scene3D.camera3D.z], 1);

        ctx.setVc3fv(self.shader, "isUv",
                     locusdata.uvVec, 1);

        pass

    def updateUV(self):
        locusdata = self.data;
        nowTime: float = self.time / Scene_data.frameTime;
        lifeRoundNum: float = locusdata.life / 100;
        moveUv: float = locusdata.speed * nowTime / locusdata.density / 10
        if locusdata.isEnd:
            moveUv = min(1, moveUv);

        if locusdata.isLoop:
            if locusdata.life:
                moveUv = moveUv % (lifeRoundNum + 1)
            else:
                moveUv = moveUv % 1;

        locusdata.resultUvVec[0] = moveUv;

        pass

    def setVa(self):
        super().setVa()
        modeldata = self.data;

        self.scene3D.context3D.setVaOld(modeldata.objData.vbo, 0, 3, modeldata.objData.stride, 0)
        self.scene3D.context3D.setVaOld(modeldata.objData.vbo, 1, 2, modeldata.objData.stride, 28)
        self.scene3D.context3D.setVaOld(modeldata.objData.vbo, 2, 4, modeldata.objData.stride, 12)

        self.scene3D.context3D.drawCallOld(modeldata.objData.eboID, len(modeldata.objData.indexs))
