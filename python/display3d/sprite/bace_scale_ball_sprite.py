from OpenGL.GL import *
import numpy as np
import ctypes

from PIL import Image

from pan3d.display.Display3D import Display3D
from pan3d.mateial.TextureRes import TextureRes

from pan3d.program.Shader3D import Shader3D


class BaseScaleBallShader(Shader3D):
    def __init__(self, scene):
        super().__init__(scene)

    def getVertexShaderString(self):
        vertex_shader_str = """

        #version 330 core
        layout (location = 0) in vec3 vertexPosition;
        layout (location = 1) in vec3 vertexColor;
        layout (location = 2) in vec2 vertexCoord;

        uniform mat4 posMatrix;
        uniform mat4 camMatrix;
        uniform mat4 viewMatrixMatrix3D;
        uniform float screenScale;

        out vec3 fragmentColor;
        out vec2 fragmentTexCoord;


        void main()
        {
            // 计算物体中心在视图空间中的位置
            vec4 centerView = camMatrix * posMatrix * vec4(0.0, 0.0, 0.0, 1.0);

            // 计算物体中心在裁剪空间中的位置
            vec4 centerClip = viewMatrixMatrix3D * centerView;
            float centerW = max(centerClip.w, 0.001);

            // 顶点偏移量（相对于中心点）
            vec3 offset = vertexPosition;

            // 从透视矩阵中提取投影缩放因子
            float projScale = viewMatrixMatrix3D[0][0];

            vec3 scaledOffset = offset * screenScale * projScale;

            gl_Position = vec4(centerClip.x + scaledOffset.x * centerW,
                               centerClip.y + scaledOffset.y * centerW,
                               centerClip.z + scaledOffset.z * centerW,
                               centerClip.w);

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





class BaseScaleBallSprite3D(Display3D):

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
        self.shader = BaseScaleBallShader(self.scene3D)
        self.shader.encode()


    def makeTexture(self):
        self.wood_texture = TextureRes(self.scene3D)
        imWidth, imHeight = 32, 32
        # 创建白色背景图像（直接使用白色，无需先红后覆盖）
        white_image = Image.new('RGBA', (imWidth, imHeight), color='red')
        white_image_data = np.asarray(white_image, dtype=np.uint8).reshape(-1, 4)
        self.wood_texture.imageToTexTure(white_image_data, imWidth, imHeight)


    def makeVertices(self, color: tuple = (1.0, 1.0, 1.0)):
        r, g, b = color
        radius = 3.0
        sectors = 24  # 经线数
        stacks = 16  # 纬线数

        vertices = []
        for i in range(stacks):
            lat0 = np.pi * (-0.5 + i / stacks)
            lat1 = np.pi * (-0.5 + (i + 1) / stacks)
            y0, yr0 = np.sin(lat0), np.cos(lat0)
            y1, yr1 = np.sin(lat1), np.cos(lat1)

            for j in range(sectors):
                lng0 = 2 * np.pi * j / sectors
                lng1 = 2 * np.pi * (j + 1) / sectors
                x0, z0 = np.cos(lng0), np.sin(lng0)
                x1, z1 = np.cos(lng1), np.sin(lng1)

                # 四个角点的坐标和UV
                p0 = (radius * yr0 * x0, radius * y0, radius * yr0 * z0)
                p1 = (radius * yr0 * x1, radius * y0, radius * yr0 * z1)
                p2 = (radius * yr1 * x1, radius * y1, radius * yr1 * z1)
                p3 = (radius * yr1 * x0, radius * y1, radius * yr1 * z0)

                u0, u1 = j / sectors, (j + 1) / sectors
                v0, v1 = i / stacks, (i + 1) / stacks

                # 三角形1: p0, p1, p2
                vertices.extend([*p0, r, g, b, u0, v0])
                vertices.extend([*p1, r, g, b, u1, v0])
                vertices.extend([*p2, r, g, b, u1, v1])
                # 三角形2: p0, p2, p3
                vertices.extend([*p0, r, g, b, u0, v0])
                vertices.extend([*p2, r, g, b, u1, v1])
                vertices.extend([*p3, r, g, b, u0, v1])

        self.vertices = np.array(vertices, dtype=np.float32)
    def upData(self):
        if not self.visible:
            return
        if self.wood_texture is None:
            return

        self.scene3D.context3D.setProgram(self.shader)
        self.scene3D.context3D.setRenderTexture(self.shader, 'imageTexture', 0, self.wood_texture)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'posMatrix', self.getPosMatrix())
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'camMatrix', self.scene3D.camera3D.camMatrix)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'viewMatrixMatrix3D', self.scene3D.camera3D.viewMatrixMatrix3D)

        # 设置屏幕缩放因子，控制球体在屏幕上的恒定大小
        screen_scale = self.scaleX * 0.005
        glUniform1f(glGetUniformLocation(self.shader.program, 'screenScale'), screen_scale)

        glBindVertexArray(self.vao)
        glDrawArrays(GL_TRIANGLES, 0, int(len(self.vertices) / 8))


    def destroy(self):
        glDeleteVertexArrays(1, (self.vao,))
        glDeleteBuffers(1, (self.vbo,))
