from ....display.particle.Display3DParticle import Display3DParticle
from ....program.Shader3D import Shader3D
from ....core.Vector3D import Vector3D
from ....scene3D.Scene_data import Scene_data
import numpy as np


class Display3DFacetShader(Shader3D):
    Display3DFacetShader = 'Display3DFacetShader';

    def getVertexShaderString(self):
        vstr: str = """
             #version 330 core
            layout (location = 0) in  vec3 v3Position;
            layout (location = 1) in  vec2 v2CubeTexST;

            varying vec2 v0;

            uniform mat4 modelMatrix;
            uniform mat4 vpMatrix3D;
            uniform mat4 rotMatrix;
            uniform vec2 uvMove;


           void main()
           {

                gl_Position = vpMatrix3D * modelMatrix*rotMatrix*vec4(v3Position.xyz,1.0);
                v0 = v2CubeTexST+uvMove;

           }

                   """

        return vstr;

    def getFragmentShaderString(self):
        fstr = """
            precision mediump float;
            uniform sampler2D fs0;
            varying vec2 v0;
            uniform vec4 fc[1];
            
            void main(void){
            
                vec4 ft0 = texture2D(fs0,v0);
                ft0.xyz *= ft0.w;
                vec4 ft1 = ft0 * fc[0];
                ft0.xyz = ft1.xyz;
                ft0.w = ft1.w;
                gl_FragColor = ft0;

            }
               """

        return fstr


class Display3DFacetParticle(Display3DParticle):
    def __init__(self, scene3D):
        super().__init__(scene3D);
        self.uvMove: list = [1, 1];

    def update(self):
        super().update()
        pass

    def updateUV(self):
        facetdata = self.data;
        currentFrame: int = int(self.time / Scene_data.frameTime);
        if currentFrame > facetdata.maxAnimTime:
            currentFrame = facetdata.maxAnimTime

        currentFrame = int(currentFrame / facetdata.animInterval) % (facetdata.animLine * facetdata.animRow);

        self.uvMove[
            0] = int(
            currentFrame % facetdata.animLine) / facetdata.animLine + self.time / Scene_data.frameTime * facetdata.uSpeed;

        self.uvMove[
            1] = int(
            currentFrame / facetdata.animLine) / facetdata.animRow + self.time / Scene_data.frameTime * facetdata.vSpeed;

        pass

    def setVc(self):
        self.updateUV();
        self.updateRotaionMatrix()
        self.scene3D.context3D.setVc2fv(self.shader, "uvMove", self.uvMove);
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'rotMatrix', self.rotationMatrix)

        super().setVc();

        pass

    def updateRotaionMatrix(self):
        facetdata = self.data;
        self.rotationMatrix.identity();

        if facetdata.watchEye:
            self.timeline.inverAxisRotation(self.rotationMatrix);
            if not facetdata.locky and not facetdata.lockx:
                self.inverBind();

            if not facetdata.locky:
                self.rotationMatrix.prependRotation(-self.scene3D.camera3D.rotationY, Vector3D.Y_AXIS);

            if not facetdata.lockx:
                self.rotationMatrix.prependRotation(-self.scene3D.camera3D.rotationX, Vector3D.X_AXIS);

        if self.data.isZiZhuan:
            self.timeline.applySelfRotation(self.rotationMatrix, self.data.ziZhuanAngly);

        pass

    def inverBind(self):
        pass

    def setVa(self):
        super().setVa()
        modeldata = self.data;
        self.scene3D.context3D.setVaOld(modeldata.objData.vbo, 0, 3, 20, 0)
        self.scene3D.context3D.setVaOld(modeldata.objData.vbo, 1, 2, 20, 12)
        self.scene3D.context3D.drawCallOld(modeldata.objData.eboID, len(modeldata.objData.indexs))
