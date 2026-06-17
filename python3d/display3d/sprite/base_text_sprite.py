from OpenGL.GL import *
import numpy as np
import ctypes

from PIL import Image, ImageDraw, ImageFont

from pan3d.display.Display3D import Display3D
from pan3d.mateial.TextureRes import TextureRes
from pan3d.program.Shader3D import Shader3D


class BaseTextShader3D(Shader3D):
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
            // 视图空间中z分量的绝对值即为相机到物体中心的距离
            float dist = abs(centerView.z);

            // 计算物体中心在裁剪空间中的位置
            vec4 centerClip = viewMatrixMatrix3D * centerView;
            float centerW = max(centerClip.w, 0.001);

            // 顶点偏移量（相对于中心点）
            vec3 offset = vertexPosition;

            // 从透视矩阵中提取投影缩放因子: proj[0][0] = cot(fov/2) / aspect
            // 对于单位偏移在NDC中的大小: ndc_offset = offset / dist * proj[0][0]
            // 要保持屏幕恒定: offset_screen = offset * screenScale (screenScale与距离无关)
            // 所以: ndc_offset = offset * screenScale * proj[0][0]
            // 乘以 centerW 转换到裁剪空间
            float projScale = viewMatrixMatrix3D[0][0];

            vec3 scaledOffset = offset * screenScale * projScale;

            gl_Position = vec4(centerClip.x + scaledOffset.x * centerW,
                               centerClip.y + scaledOffset.y * centerW,
                               centerClip.z,
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
            vec4 texColor = texture(imageTexture, fragmentTexCoord);
            color = vec4(fragmentColor, texColor.a);
        }
        """

        return fragment_shader_str


class BaseTextSprite3D(Display3D):

    def __init__(self, scene, text: str = "", font_size: int = 64, color: tuple = (1.0, 1.0, 1.0)):
        self._text = text
        self._font_size = font_size
        self._color = color
        self._tex_width = 1
        self._tex_height = 1

        self.vertices: np.ndarray | None = None
        self.wood_texture = None
        self.shader = None

        super().__init__(scene)

        self.makeShader()
        self.makeTexture()
        self.makeVertices()

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

    @property
    def text(self):
        return self._text

    @text.setter
    def text(self, value: str):
        self._text = value
        self._updateTexture()
        self._updateVBO()

    def makeShader(self):
        self.shader = BaseTextShader3D(self.scene3D)
        self.shader.encode()

    def makeTexture(self):
        self.wood_texture = TextureRes(self.scene3D)
        self._renderTextToTexture()

    def _renderTextToTexture(self):
        """将文本渲染到纹理上，背景透明"""
        text = self._text
        font_size = self._font_size

        # 尝试加载字体，优先使用系统中文字体
        try:
            font = ImageFont.truetype("msyh.ttc", font_size)
        except OSError:
            try:
                font = ImageFont.truetype("simhei.ttf", font_size)
            except OSError:
                font = ImageFont.load_default()

        # 计算文本尺寸
        dummy_img = Image.new('RGBA', (1, 1))
        dummy_draw = ImageDraw.Draw(dummy_img)
        bbox = dummy_draw.textbbox((0, 0), text, font=font)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]

        # 宽高至少为1，并稍微留边距
        img_width = max(text_width + 4, 1)
        img_height = max(text_height + 4, 1)

        # 创建透明背景图像
        img = Image.new('RGBA', (img_width, img_height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        draw.text((2 - bbox[0], 2 - bbox[1]), text, fill=(255, 255, 255, 255), font=font)

        image_data = np.asarray(img, dtype=np.uint8).reshape(-1, 4)
        self.wood_texture.imageToTexTure(image_data, img_width, img_height)

        # 保存纹理尺寸用于顶点计算
        self._tex_width = img_width
        self._tex_height = img_height

    def _updateTexture(self):
        """更新文本纹理"""
        if self.wood_texture is not None:
            self._renderTextToTexture()

    def _updateVBO(self):
        """更新VBO中的顶点数据"""
        self.makeVertices()
        glBindBuffer(GL_ARRAY_BUFFER, self.vbo)
        glBufferData(GL_ARRAY_BUFFER, self.vertices.nbytes, self.vertices, GL_STATIC_DRAW)

    def makeVertices(self):
        """创建一个平面四边形，宽高比与纹理一致"""
        r, g, b = self._color

        # 根据纹理宽高比计算平面尺寸，高度固定为1
        if self._tex_height > 0:
            aspect = self._tex_width / self._tex_height
        else:
            aspect = 1.0

        half_w = aspect / 2.0
        half_h = 0.5

        # 两个三角形组成一个面，面向 +z 方向
        self.vertices = np.array([
            # 三角形1
            -half_w, -half_h, 0.0, r, g, b, 0.0, 1.0,
            half_w, -half_h, 0.0, r, g, b, 1.0, 1.0,
            half_w, half_h, 0.0, r, g, b, 1.0, 0.0,
            # 三角形2
            -half_w, -half_h, 0.0, r, g, b, 0.0, 1.0,
            half_w, half_h, 0.0, r, g, b, 1.0, 0.0,
            -half_w, half_h, 0.0, r, g, b, 0.0, 0.0,
        ], dtype=np.float32)

    def upData(self):
        if not self.visible:
            return
        if self.wood_texture is None:
            return

        # Billboard: 让文本始终完整面向相机（绕Y轴和X轴）
        self.rotationY = -self.scene3D.camera3D.rotationY
        self.rotationX = -self.scene3D.camera3D.rotationX

        self.scene3D.context3D.setProgram(self.shader)
        self.scene3D.context3D.setRenderTexture(self.shader, 'imageTexture', 0, self.wood_texture)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'posMatrix', self.getPosMatrix())
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'camMatrix', self.scene3D.camera3D.camMatrix)
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'viewMatrixMatrix3D', self.scene3D.camera3D.viewMatrixMatrix3D)

        # 设置屏幕缩放因子，控制文本在屏幕上的恒定大小
        # screenScale * projScale 的效果等价于"每单位世界坐标对应多少NDC单位"
        # 值越大文本越大，可通过 scaleX/scaleY 或直接修改此值来调整
        screen_scale = self.scaleX*0.02
        glUniform1f(glGetUniformLocation(self.shader.program, 'screenScale'), screen_scale)

        # 启用混合，支持透明文字
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)

        glBindVertexArray(self.vao)
        glDrawArrays(GL_TRIANGLES, 0, int(len(self.vertices) / 8))

        glDisable(GL_BLEND)

    def destroy(self):
        glDeleteVertexArrays(1, (self.vao,))