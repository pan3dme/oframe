from ....program.Shader3D import Shader3D

from ....core.Vector3D import Vector3D
from ....scene3D.Scene_data import Scene_data
from ....scene3D.Context3D import Context3D
from ....display.particle.Display3DParticle import Display3DParticle
from ....vo.DualQuatFloat32Array import DualQuatFloat32Array


class Display3DBallShader(Shader3D):
    Display3DBallShader = 'Display3DBallShader';

    def getVertexShaderString(self):

        hasParticle: int = self.paramAry[0];
        hasRandomClolr: int = self.paramAry[1];
        isMul: int = self.paramAry[2];
        needRotation: int = self.paramAry[3];
        needScale: int = self.paramAry[4];
        needAddSpeed: int = self.paramAry[5];
        uvType: int = self.paramAry[6];

        defineBaseStr: str = "";
        funBaseStr: str = "";
        mainBaseStr: str = "";
        rotationStr: str = "";

        defineBaseStr = """
          
            layout (location = 0) in  vec3 vPosition;
            layout (location = 1) in  vec3 texcoord;
            layout (location = 2) in  vec4 basePos;
            layout (location = 3) in  vec3 speed;
            
            uniform mat4 viewMatrix; 
            uniform mat4 camMatrix; 
            uniform mat4 modeMatrix; 
            uniform mat4 rotMatrix; 
            
            uniform vec4 vcmat50; 
            uniform vec4 vcmat51; 
            uniform vec4 vcmat52; 
            uniform vec4 vcmat53; 
            uniform vec4 camPos; 
            uniform vec4 uvAnimData; 
            
            varying vec2 v0; 
            varying vec2 v1; 
            

            """

        funBaseStr = """
            vec4 IW(vec4 v) {
                return viewMatrix*camMatrix*modeMatrix* v;
            }

            float CTM() {
                float t = vcmat50.x- basePos.w;
                if (vcmat50.w > 0.0 && t >= 0.0) {
                    t = fract(t /vcmat50.z) * vcmat50.z;
                }
                return t;
            }
            
            float STM(float ctime) {
                float t = ctime - vcmat51.w;
                t = max(t,0.0);
                return t;
            }
            vec4 S_POS(vec4 pos ,float stime) {
                float sf = vcmat51.x * stime;
                if (vcmat51.y != 0.0 && vcmat51.z != 0.0) {
                  sf += sin(vcmat51.y * stime) * vcmat51.z;
                }
                sf=min(sf,vcmat52.z);
                sf=max(sf,vcmat52.w);
                vec2 sv2 = vec2(vcmat52.x * sf, vcmat52.y * sf);
                sv2 = sv2 + 1.0;
                pos.x *= sv2.x;
                pos.y *= sv2.y;
                return pos;
            
            }
            vec2 UV_ANIM_FUN(vec2 pos ,float stime) {
                float row4= uvAnimData.x ;
                float low4= uvAnimData.y;
                float speed2= uvAnimData.w;
                float timeNum= floor(stime/speed2);
                float aa= floor( timeNum/ row4);
                float bb= fract( timeNum/ row4)*row4;
                vec2 outVec2= vec2(pos.x +bb/ row4 ,pos.y +aa/ low4);
                return outVec2;
            }
            vec3 ADD_POS(vec3 speed ,float ctime) {
                vec3 addPos = speed * ctime;
                vec3 uspeed = vec3(0,0,0);
                if(vcmat50.y != 0.0 && length(speed) != 0.0) {
                    uspeed = vec3(speed.x, speed.y, speed.z);
                    uspeed = normalize(uspeed);
                    uspeed = uspeed * vcmat50.y;       
                    uspeed.xyz = uspeed.xyz + vcmat53.xyz;
                } else {
                    uspeed = vec3(vcmat53.x, vcmat53.y, vcmat53.z);
                }
                addPos.xyz = addPos.xyz + uspeed.xyz * ctime * ctime;
                return addPos;
            }
            
            """
        if needRotation > 0:
            defineBaseStr += """ 
             layout (location = 4) in  vec2 rotation;
             """;
            rotationStr = """
                float angle = rotation.x + rotation.y * ctime;
                vec4 np = vec4(sin(angle), cos(angle), 0, 0);
                np.z = np.x * pos.y + np.y * pos.x;
                np.w = np.y * pos.y - np.x * pos.x;
                pos.xy = np.zw;
                """

        sceleStr: str = " ";
        if needScale:
            sceleStr = """
                        pos = S_POS(pos,stime);
                        """
        mulStr: str = "";
        if isMul:
            mulStr = """
                vec3  speedNrm = normalize(speed.xyz); 
                vec3  camNrm =normalize(camPos.xyz); 
                vec3 crossv3d=cross(speedNrm.xyz, camNrm.xyz); 
                crossv3d = normalize(crossv3d); 
                pos.xyz = pos.x*speedNrm.xyz+pos.y*crossv3d.xyz; 
                """

        mainBaseStr = """ 
                vec4 pos = vec4(vPosition.xyz,1.0); 
                float ctime = CTM(); 
                float stime = STM(ctime); 
                """ + rotationStr + """ 
                if (ctime < 0.0 || ctime > vcmat50.z) { 
                    pos.x =0.0; 
                    pos.y =0.0; 
                }else{  
                """ + sceleStr + """
                    pos = rotMatrix*pos;
                    vec3 addPos =ADD_POS(speed,ctime);
                """ + mulStr + """
                     pos.xyz = pos.xyz + basePos.xyz + addPos.xyz; 
                } 
                gl_Position =IW(pos);
                v0=UV_ANIM_FUN(texcoord.xy,ctime);
                v1=vec2(ctime/vcmat50.z,0.0);
            """

        vstr: str = defineBaseStr + funBaseStr + """
            void main() 
            { 
             """ + mainBaseStr + """
            };
             """

        return vstr

    def getFragmentShaderString(self):
        fstr = """
            precision mediump float;
            uniform sampler2D fs0;
            uniform sampler2D fs1;
            varying vec2 v0;
            varying vec2 v1;
            void main(void){
            
                vec4 ft0 = texture2D(fs0,v0);
                vec4 ft1 = texture2D(fs1,v1);
          
                
                gl_FragColor = ft1;
                
            }
            
                  """

        return fstr


class Display3DBallPartilce(Display3DParticle):
    def __init__(self, scene3D):
        super().__init__(scene3D);

    def update(self):
        if self.visible and (self.data is not None) and (self.scene3D is not None):
            if (self.data.materialParam is not None) and (self.data.materialParam.shader is not None):
                self.shader = self.data.materialParam.shader;
                ctx: Context3D = self.scene3D.context3D

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
        self.updateWatchCaramMatrix();
        balldata = self.data;
        ctx = self.scene3D.context3D;
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'modeMatrix', self.modelMatrix)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'viewMatrix', self.scene3D.camera3D.viewMatrixMatrix3D)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'camMatrix', self.scene3D.camera3D.camMatrix)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'rotMatrix', self.rotationMatrix)

        tm = self.time / Scene_data.frameTime * balldata.playSpeed;
        timeVec: Vector3D = balldata.timeVec;
        timeVec.x = tm;
        ctx.setVc4fv(self.shader, "vcmat50", [timeVec.x, timeVec.y, timeVec.z, timeVec.w], 1);

        scaleVec: Vector3D = balldata.scaleVec;
        ctx.setVc4fv(self.shader, "vcmat51", [scaleVec.x, scaleVec.y, scaleVec.z, scaleVec.w], 1);

        scaleCtrl: Vector3D = balldata.scaleCtrlVec;
        ctx.setVc4fv(self.shader, "vcmat52", [scaleCtrl.x, scaleCtrl.y, scaleCtrl.z, scaleCtrl.w], 1);

        addSpeedVec: Vector3D = balldata.addSpeedVec;
        ctx.setVc4fv(self.shader, "vcmat53", [addSpeedVec.x, addSpeedVec.y, addSpeedVec.z, addSpeedVec.w], 1);

        camera3D = self.scene3D.camera3D;
        ctx.setVc4fv(self.shader, "camPos", [camera3D.x, camera3D.y, camera3D.z, 1], 1);

        ctx.setVc4fv(self.shader, "uvAnimData",
                     [balldata.animLine, balldata.animRow, balldata.totalNum, balldata.animInterval], 1);

        pass

    def updateWatchCaramMatrix(self):

        self.rotationMatrix.identity();
        balldata = self.data;
        if balldata.facez:
            self.rotationMatrix.prependRotation(90, Vector3D.X_AXIS);

        elif balldata.is3Dlizi:
            self.timeline.inverAxisRotation(self.rotationMatrix);

        elif balldata.watchEye:

            self.timeline.inverAxisRotation(self.rotationMatrix);
            self.rotationMatrix.prependRotation(-self.scene3D.camera3D.rotationY, Vector3D.Y_AXIS);
            self.rotationMatrix.prependRotation(-self.scene3D.camera3D.rotationX, Vector3D.X_AXIS);

        pass

    def setVa(self):
        super().setVa()
        modeldata = self.data;
        self.scene3D.context3D.setVaOld(modeldata.objData.vbo, 0, 3, modeldata.objData.stride, 0)
        self.scene3D.context3D.setVaOld(modeldata.objData.vbo, 1, 3, modeldata.objData.stride, 12)
        self.scene3D.context3D.setVaOld(modeldata.objData.vbo, 2, 4, modeldata.objData.stride, 24)
        self.scene3D.context3D.setVaOld(modeldata.objData.vbo, 3, 3, modeldata.objData.stride, 40)
        if modeldata.needSelfRotation:
            self.scene3D.context3D.setVaOld(modeldata.objData.vbo, 4, 2, modeldata.objData.stride, 52);

        self.scene3D.context3D.drawCallOld(modeldata.objData.eboID, len(modeldata.objData.indexs))
