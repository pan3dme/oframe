from ....program.Shader3D import Shader3D

from ....scene3D.Scene_data import Scene_data
from ....display.particle.Display3DParticle import Display3DParticle
from ....vo.DualQuatFloat32Array import DualQuatFloat32Array

import numpy as np


class Display3DBoneShader(Shader3D):
    Display3DBoneShader = 'Display3DBoneShader';

    def getVertexShaderString(self):
        vstr: str = """
             #version 330 core
            layout (location = 0) in  vec3 v3Position;
            layout (location = 1) in  vec2 v2CubeTexST;
            layout (location = 2) in  vec4 boneID;
            layout (location = 3) in  vec4 boneWeight;
            
         
            varying vec2 v0;
            
            uniform vec4 boneQ[54]; 
            uniform vec3 boneD[54]; 
            
            uniform mat4 modelMatrix;
            uniform mat4 vpMatrix3D;
            
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
                vec4 vt0 = getQDdata(v3Position); 
                gl_Position = vpMatrix3D * modelMatrix*vt0;
                v0 = v2CubeTexST;

           }

                   """

        return vstr;

    def getFragmentShaderString(self):
        fstr = """
            precision mediump float;
            uniform sampler2D fs0;
            
            uniform vec4 fc[1];
       
            varying vec2 v0;
            
            void main(void){
    
               
              vec4 ft0 = texture2D(fs0,v0);
ft0.xyz *= ft0.w;
vec4 ft1 = ft0 * fc[0];
ft0.xyz = ft1.xyz;
ft0.w = ft1.w;
ft0.xyz = ft0.xyz * ft0.w;
gl_FragColor = ft0;
            }
               """

        return fstr


class Display3DBonePartilce(Display3DParticle):
    def __init__(self, scene3D):
        super().__init__(scene3D);


    def setVc(self):
        super().setVc();

        currentFrame: int = int(self.time / Scene_data.frameTime / 2);
        modeldata = self.data;
        frameDualQuat: list = modeldata.animData.boneQPAry[0]
        frameLen: int = len(frameDualQuat);
        frameId: int = currentFrame % frameLen;

        dualQuatFrame: DualQuatFloat32Array = frameDualQuat[frameId];

        self.scene3D.context3D.setVc4fv(modeldata.materialParam.shader, "boneQ",
                                        np.array(dualQuatFrame.quat, dtype=np.float32),54);
        self.scene3D.context3D.setVc3fv(modeldata.materialParam.shader, "boneD",
                                        np.array(dualQuatFrame.pos, dtype=np.float32),54);

        pass

    def setVa(self):
        modeldata = self.data;
        self.scene3D.context3D.setVaOld(modeldata.meshData.vbo, 0, 3, 52, 0)
        self.scene3D.context3D.setVaOld(modeldata.meshData.vbo, 1, 2, 52, 12)
        self.scene3D.context3D.setVaOld(modeldata.meshData.vbo, 2, 4, 52, 20)
        self.scene3D.context3D.setVaOld(modeldata.meshData.vbo, 3, 4, 52, 36)



        self.scene3D.context3D.drawCallOld(modeldata.meshData.eboID, len(modeldata.meshData.indexs))

        pass
