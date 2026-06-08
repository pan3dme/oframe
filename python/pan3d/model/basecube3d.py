import numpy as np
from ..program.Shader3D import Shader3D
from ..data.ObjData import ObjData
from ..display.Display3D import Display3D
from ..mateial.TextureManager import TextureManagerGetInstance




class CuBeObjData(ObjData):
    def __init__(self, ctx):
        super().__init__(ctx)

        # self.format: str = None
        # self.attribs: list = None
        self.format = '2f 3f 3f'
        self.attribs = ['in_texcoord_0', 'in_normal', 'in_position']

    def get_vertex_data(self):
        vertices = [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1),
                    (-1, 1, -1), (-1, -1, -1), (1, -1, -1), (1, 1, -1)]

        indices = [(0, 2, 3), (0, 1, 2)]
        vertex_data = self.get_data(vertices, indices)

        normals = [(0, -1, 0), (0, -1, 0), (0, -1, 0), (0, -1, 0), (0, -1, 0), (0, -1, 0)]

        uvs = []
        zeroNum = 0;
        oneNum = 1;

        uvs.append((zeroNum, zeroNum))
        uvs.append((oneNum, oneNum))
        uvs.append((zeroNum, oneNum))
        uvs.append((zeroNum, zeroNum))
        uvs.append((oneNum, zeroNum))
        uvs.append((oneNum, oneNum))

        tex_coord_data = np.array(uvs, dtype='f4')
        normals = np.array(normals, dtype='f4')

        vertex_data = np.hstack([normals, vertex_data])
        vertex_data = np.hstack([tex_coord_data, vertex_data])
        return vertex_data




class BaseCube3D(Display3D):
    def __init__(self, app   ):
        super().__init__(app.ctx)
        self.app = app

        self.shader = Shader3D(self.ctx)
        self.program = self.shader.program
        self.renVo = self.get_vao(
            program=self.program ,
            vbo=CuBeObjData(self.ctx))


        self.camera = self.app.camera
        self.on_init();

    def on_init(self):
        TextureManagerGetInstance.setCtx(self.ctx)
        self.texture = TextureManagerGetInstance.getTextureByPath(path='textures/img.png')
        self.program['u_texture_0'] = 0;
        self.texture.use(location=0);
    def update(self):
        self.program['m_proj'].write(self.camera.m_proj);
        self.program['m_view'].write(self.camera.m_view)
        self.program['m_model'].write(self.m_model)










