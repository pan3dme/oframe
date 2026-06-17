from OpenGL.GL import *
import numpy as np
import ctypes

from PIL import Image

from pan3d.display.Display3D import Display3D
from pan3d.mateial.TextureRes import TextureRes

from pan3d.program.Shader3D import Shader3D


class BaseColorShader3D(Shader3D):
    def __init__(self, scene):
        super().__init__(scene)

    def getVertexShaderString(self):
        vertex_shader_str = """

        #version 330 core
        layout (location = 0) in vec3 vertexPosition;
        layout (location = 1) in vec3 vertexColor;
        layout (location = 2) in vec2 vertexCoord;

        uniform mat4 posMatrix;
        uniform mat4 vpMatrix3D;

        out vec3 fragmentColor;
        out vec2 fragmentTexCoord;


        void main()
        {
            gl_Position =   vpMatrix3D* posMatrix * vec4(vertexPosition, 1.0);


            fragmentColor = vertexColor;
            fragmentTexCoord = vertexCoord;
        }
        """

        return vertex_shader_str

    def getFragmentShaderString(self):
        fragment_shader_str = """
        #version 330 core

        in vec3 fragmentColor;
        in vec2 fragmentTexCoord;

        out vec4 color;

        uniform sampler2D imageTexture;


        void main() {
            vec4 color0 = texture(imageTexture, fragmentTexCoord);
            color = vec4(fragmentColor,1);

        }
        """

        return fragment_shader_str





class BaseColorSprite3D(Display3D):

    def __init__(self, scene, color: tuple = (1.0, 1.0, 1.0)):
        super().__init__(scene)

        self.vertices: np.ndarray | None = None
        self.wood_texture = None
        self.shader = None

        self.makeShader()
        self.makeTexture()
        self.makeVertices(color)

        self.vao = glGenVertexArrays(1)
        glBindVertexArray(self.vao)
        self.vbo = glGenBuffers(1)
        glBindBuffer(GL_ARRAY_BUFFER, self.vbo)
        glBufferData(GL_ARRAY_BUFFER, self.vertices.nbytes, self.vertices, GL_STATIC_DRAW)
        glEnableVertexAttribArray(0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, ctypes.c_void_p(0))
        glEnableVertexAttribArray(1)
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, ctypes.c_void_p(12))
        glEnableVertexAttribArray(2)
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, ctypes.c_void_p(24))



    def makeShader(self):
        self.shader = BaseColorShader3D(self.scene3D)
        self.shader.encode()


    def makeTexture(self):
        self.wood_texture = TextureRes(self.scene3D)
        imWidth, imHeight = 32, 32
        # 创建白色背景图像（直接使用白色，无需先红后覆盖）
        white_image = Image.new('RGBA', (imWidth, imHeight), color='red')
        white_image_data = np.asarray(white_image, dtype=np.uint8).reshape(-1, 4)
        self.wood_texture.imageToTexTure(white_image_data, imWidth, imHeight)

    @staticmethod
    def createCubeVertices(width: float, height: float, depth: float = None, color: tuple = (1.0, 1.0, 1.0)):
        """
        生成立方体的顶点数据（每个顶点8个分量：位置xyz, 颜色rgb, 纹理uv）

        参数:
            width:  x轴方向的宽度（从 -width/2 到 width/2）
            height: y轴方向的高度（从 -height/2 到 height/2）
            depth:  z轴方向的深度（如果为None，则默认等于width）
            color:  统一颜色 (r, g, b)，取值范围0.0~1.0，例如 (1,0,0) 红色

        返回:
            tuple: 包含36个顶点（12个三角形）的扁平元组
        """
        if depth is None:
            depth = width

        r, g, b = color

        # 半长
        hw = width / 2.0  # half width
        hh = height / 2.0  # half height
        hd = depth / 2.0  # half depth

        # 六个面的顶点数据（每个面两个三角形，共6个面，每个三角形3个顶点）
        # 顶点顺序：位置(x,y,z), 颜色(r,g,b), 纹理(u,v)
        vertices = []

        # 辅助添加三角形函数
        def add_triangle(v1, v2, v3):
            # v1, v2, v3 格式为 (x, y, z, u, v)
            for v in (v1, v2, v3):
                vertices.extend([v[0], v[1], v[2], r, g, b, v[3], v[4]])

        # 前面 (z = +hd)
        z = hd
        add_triangle((-hw, -hh, z, 0, 0), (hw, -hh, z, 1, 0), (hw, hh, z, 1, 1))
        add_triangle((-hw, -hh, z, 0, 0), (hw, hh, z, 1, 1), (-hw, hh, z, 0, 1))

        # 后面 (z = -hd)
        z = -hd
        add_triangle((-hw, -hh, z, 0, 0), (-hw, hh, z, 0, 1), (hw, hh, z, 1, 1))
        add_triangle((-hw, -hh, z, 0, 0), (hw, hh, z, 1, 1), (hw, -hh, z, 1, 0))

        # 左面 (x = -hw)
        lx = -hw
        add_triangle((lx, -hh, -hd, 0, 0), (lx, -hh, hd, 1, 0), (lx, hh, hd, 1, 1))
        add_triangle((lx, -hh, -hd, 0, 0), (lx, hh, hd, 1, 1), (lx, hh, -hd, 0, 1))

        # 右面 (x = +hw)
        rx = hw
        add_triangle((rx, -hh, -hd, 0, 0), (rx, hh, hd, 1, 1), (rx, -hh, hd, 1, 0))
        add_triangle((rx, -hh, -hd, 0, 0), (rx, hh, -hd, 0, 1), (rx, hh, hd, 1, 1))

        # 上面 (y = +hh)
        ty = hh
        add_triangle((-hw, ty, -hd, 0, 0), (hw, ty, -hd, 1, 0), (hw, ty, hd, 1, 1))
        add_triangle((-hw, ty, -hd, 0, 0), (hw, ty, hd, 1, 1), (-hw, ty, hd, 0, 1))

        # 下面 (y = -hh)
        by = -hh
        add_triangle((-hw, by, -hd, 0, 0), (-hw, by, hd, 0, 1), (hw, by, hd, 1, 1))
        add_triangle((-hw, by, -hd, 0, 0), (hw, by, hd, 1, 1), (hw, by, -hd, 1, 0))

        return tuple(vertices)
    def makeVertices(self, color: tuple = (1.0, 1.0, 1.0)):

        self.vertices = np.array(self.createCubeVertices(10.0, 10.0, 10.0, color), dtype=np.float32)

    def upData(self):
        if not self.visible:
            return
        if self.wood_texture is None:
            return

        self.scene3D.context3D.setProgram(self.shader)
        self.scene3D.context3D.setRenderTexture(self.shader, 'imageTexture', 0, self.wood_texture)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'posMatrix', self.getPosMatrix())
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'vpMatrix3D', self.scene3D.camera3D.vpMatrix3D)

        glBindVertexArray(self.vao)
        glDrawArrays(GL_TRIANGLES, 0, int(len(self.vertices) / 8))


    def destroy(self):
        glDeleteVertexArrays(1, (self.vao,))
        glDeleteBuffers(1, (self.vbo,))
