from ....display.particle.facet.Display3DFacetParticle import Display3DFacetParticle
from ....display.particle.facet.Display3DFacetParticle import Display3DFacetShader
from ....program.Shader3D import Shader3D
from ....core.Vector3D import Vector3D
from ....scene3D.Scene_data import Scene_data
from ....scene3D.Context3D import Context3D
from ....scene3D.Scene_data import Scene_data


class Display3DModelShader(Display3DFacetShader):
    Display3DModelShader = 'Display3DModelShader';


class Display3DModelPartilce(Display3DFacetParticle):
    def __init__(self, scene3D):
        super().__init__(scene3D);
