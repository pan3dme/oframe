from typing import Optional
import numpy as np
import glm
import ctypes

from OpenGL.GL import *
from PIL import Image, ImageDraw

from pan3d.display.Display3D import Display3D
from pan3d.mateial.TextureRes import TextureRes
from pan3d.core.Vector3D import Vector3D
from config import settings
from pan3d.program.Shader3D import Shader3D


class AreaMapDisplay3DShader(Shader3D):
    def __init__(self, scene):
        super().__init__(scene)
        pass

    def getVertexShaderString(self):
        vstr = """
                
        #version 330 core
        
        layout (location = 0) in vec3 vertexPosition;
        layout (location = 1) in vec3 vertexColor;
        layout (location = 2) in vec2 vertexCoord;
        
        uniform mat4 model;
        uniform mat4 vpMatrix3D;
        
        uniform vec4 roundRect;
        
        out vec2 roadUv;
        out vec3 fragmentColor;
        out vec2 fragmentTexCoord;
        
        
        void main()
        {
           vec4 worldpos=model * vec4(vertexPosition, 1.0);
            gl_Position =   vpMatrix3D* worldpos;
            
            
        
            fragmentColor = vec3(roundRect.x,roundRect.y,roundRect.z);
            fragmentTexCoord = vertexCoord;
            roadUv=vec2((worldpos.x-roundRect.x)/roundRect.z,(worldpos.z-roundRect.y)/roundRect.w);
            
            
            roadUv=roadUv;
        }
        """

        return vstr

    def getFragmentShaderString(self):
        fstr = """
        #version 330 core
        
        in vec3 fragmentColor;
        in vec2 fragmentTexCoord;
        in vec2 roadUv;
        
        out vec4 color;
        
        uniform sampler2D imageTexture;
        uniform sampler2D loadMapTexture;
        
        void main() {
            vec4 color0 = texture(imageTexture, fragmentTexCoord);
            vec4 color1 = texture(loadMapTexture, roadUv);
        
            if (color1.r > 0.8 && color1.g < 0.2 && color1.b < 0.2) {
                color = vec4(0.0, 1.0, 0.0, 1.0);  
            } else {
                color = color0;   
            }
        
        }
        """

        return fstr

class AreaMapDisplay3DSprite(Display3D):
    def __init__(self, scene):
        super().__init__(scene)

        self.vertices = None
        self.imWidth,  self.imHeight = 100, 100
        self.white_image = None
        self.minPos: Optional[Vector3D] = None
        self.maxPos: Optional[Vector3D] = None


        self.loadMapTexture = None

        self.shader = AreaMapDisplay3DShader(self.scene3D)
        self.shader.encode()

        self.loadMapTexture = TextureRes(self.scene3D)
        self.clear_all_load_line()




    def clear_all_load_line(self):
        self.imWidth=10
        self.imHeight=10
        self.white_image = Image.new('RGBA', (self.imWidth, self.imHeight), color='white')
        white_image_data = np.asarray(self.white_image, dtype=np.uint8)
        self.loadMapTexture.imageToTexTure(white_image_data, self.imWidth, self.imHeight)
        pass
    def receive_load_to_scene(self,gps_coords):
        lastPos:Vector3D=None
        arr=[]
        for coord in gps_coords:
            a =settings.gps_to_world_pos(coord)
            if lastPos is not None:
                if self.test_pos_in_model_rect(a)or self.test_pos_in_model_rect(lastPos):
                    arr.append([lastPos,a])
                    pass
            lastPos = a
        def postoUv(value):
            tx = (value.x - self.minPos.x) / (self.maxPos.x - self.minPos.x)
            tz = (value.z - self.minPos.z) / (self.maxPos.z - self.minPos.z)
            cx, cy = self.imWidth * tx, self.imHeight * tz
            return (cx, cy)
        if len(arr):
            if self.imWidth < 200 or self.imHeight < 200:
                self.imWidth, self.imHeight = 250, 250
                self.white_image = Image.new('RGBA', (self.imWidth, self.imHeight), color='white')
            draw = ImageDraw.Draw(self.white_image)
            for temp in arr:
                a=temp[0]
                b=temp[1]
                if self.test_pos_in_model_rect(a) or self.test_pos_in_model_rect(b):
                    cx,cy = postoUv(a)
                    dx,dy = postoUv(b)
                    draw.line((cx, cy, dx, dy), fill='red', width=3)

            white_image_data = np.asarray(self.white_image, dtype=np.uint8)
            self.loadMapTexture.imageToTexTure(white_image_data, self.imWidth, self.imHeight)





    def setTextureUrl(self,url):


        self.wood_texture = TextureRes(self.scene3D,"res/blandermap/005/"+url)

    def _mathVectin3dWordPostion(self,value):
        a,b,c=value
        pos=  Vector3D(a,b,c)

        if self.minPos is None:
            self.minPos = Vector3D(pos.x,pos.y,pos.z)
        else:
            self.minPos.x = min(self.minPos.x, pos.x)
            self.minPos.y = min(self.minPos.y, pos.y)
            self.minPos.z = min(self.minPos.z, pos.z)
        if self.maxPos is None:
            self.maxPos = Vector3D(pos.x,pos.y,pos.z)
        else:
            self.maxPos.x = max(self.maxPos.x, pos.x)
            self.maxPos.y = max(self.maxPos.y, pos.y)
            self.maxPos.z = max(self.maxPos.z, pos.z)





    def setDaeInfo(self, obj):

        self.getPosMatrix()

        # 处理顶点、法线和纹理坐标数据
        vertices = obj.get('vertices', [])
        normals = obj.get('normals', [])
        texcoords = obj.get('texcoords', [])
        indices = obj.get('indices', [])

        # 合并顶点、法线和纹理坐标数据
        # 每个顶点包含: x, y, z, nx, ny, nz, u, v
        vertex_data = []

        # 如果有索引数据，根据索引来组合数据
        if indices:
            # indices是二维数组，每个元素是一个三角形，包含三个顶点的索引
            for triangle_indices in indices:
                for index in triangle_indices:
                    temp=vertices[index[0]]
                    # x反一下
                    temp = (0-temp[0] , temp[1] , temp[2] )
                    # 模型放大100倍
                    temp=(temp[0]*100,temp[1]*100,temp[2]*100)
                    #模型偏移
                    temp=(temp[0]+55,temp[1],temp[2]+18.5)

                    # 添加顶点坐标
                    vertex_data.extend(temp)
                    self._mathVectin3dWordPostion(temp)
                    # 添加法线
                    if normals:
                        vertex_data.extend(normals[index[1]])
                    else:
                        vertex_data.extend([0.0, 0.0, 1.0])  # 默认法线

                    # 添加纹理坐标
                    if texcoords:
                        vertex_data.extend((texcoords[index[2]][0],1-texcoords[index[2]][1]))
                    else:
                        vertex_data.extend([0.0, 0.0])  # 默认纹理坐标

            self.vertices = np.array(vertex_data, dtype=np.float32)
            self.vertex_count = len(indices) * 3

            # 如果有索引数据，就不需要再使用索引缓冲
            self.use_indices = False
            self.indices = None
            self.index_count = 0
        else:
            # 如果没有索引数据，按顺序组合顶点数据
            vertex_count = len(vertices) // 3

            for i in range(vertex_count):
                # 添加顶点坐标
                vertex_data.extend(vertices[i*3:(i+1)*3])

                # 添加法线
                if normals:
                    vertex_data.extend(normals[i*3:(i+1)*3])
                else:
                    vertex_data.extend([0.0, 0.0, 1.0])  # 默认法线

                # 添加纹理坐标
                if texcoords:
                    vertex_data.extend(texcoords[i*2:(i+1)*2])
                else:
                    vertex_data.extend([0.0, 0.0])  # 默认纹理坐标

            self.vertices = np.array(vertex_data, dtype=np.float32)
            self.vertex_count = vertex_count
            self.use_indices = False
            self.indices = None
            self.index_count = 0







        self.vao = glGenVertexArrays(1)
        glBindVertexArray(self.vao)

        self.vbo = glGenBuffers(1)
        glBindBuffer(GL_ARRAY_BUFFER, self.vbo)
        glBufferData(GL_ARRAY_BUFFER, self.vertices.nbytes, self.vertices, GL_STATIC_DRAW)

        # 如果有索引数据，创建EBO
        if self.use_indices:
            self.ebo = glGenBuffers(1)
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self.ebo)
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, self.indices.nbytes, self.indices, GL_STATIC_DRAW)

        glEnableVertexAttribArray(0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, ctypes.c_void_p(0))
        glEnableVertexAttribArray(1)
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, ctypes.c_void_p(12))
        glEnableVertexAttribArray(2)
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, ctypes.c_void_p(24))



    def drawLineToMap(self):
        pass

    def get_terrain_height(self, world_x: float, world_z: float) -> Optional[float]:
        """获取指定世界坐标(x, z)处的地形高度

        通过从上方垂直向下发射射线，与模型三角形求交来获取地形高度。
        如果没有相交的三角形则返回None（可能在其它地块中找到）。

        Args:
            world_x: 世界坐标X
            world_z: 世界坐标Z

        Returns:
            float: 地形高度(Y值)，如果没有交点则返回None
        """
        if self.vertices is None or self.vertex_count == 0:
            return None

        # 先做AABB包围盒快速判断，x和z是否在模型范围内
        if not (self.minPos.x <= world_x <= self.maxPos.x and self.minPos.z <= world_z <= self.maxPos.z):
            return None

        # 每个顶点8个float（3位置 + 3法线 + 2纹理），步长为8
        stride = 8
        best_y = None

        # 遍历所有三角形
        for i in range(self.vertex_count // 3):
            # 获取三角形三个顶点的世界坐标
            idx0 = (i * 3) * stride
            idx1 = (i * 3 + 1) * stride
            idx2 = (i * 3 + 2) * stride

            v0 = np.array([self.vertices[idx0], self.vertices[idx0 + 1], self.vertices[idx0 + 2]])
            v1 = np.array([self.vertices[idx1], self.vertices[idx1 + 1], self.vertices[idx1 + 2]])
            v2 = np.array([self.vertices[idx2], self.vertices[idx2 + 1], self.vertices[idx2 + 2]])

            # 射线：从(world_x, +∞, world_z)垂直向下方向(0, -1, 0)
            # 判断射线与三角形是否相交
            y = self._ray_triangle_intersect_y(world_x, world_z, v0, v1, v2)
            if y is not None:
                # 取最高的交点（如果有多个三角形重叠，取最上面的）
                if best_y is None or y > best_y:
                    best_y = y

        return best_y

    @staticmethod
    def _ray_triangle_intersect_y(px: float, pz: float, v0: np.ndarray, v1: np.ndarray, v2: np.ndarray) -> Optional[float]:
        """计算垂直射线(px, pz)与三角形v0v1v2的交点Y值

        使用重心坐标法判断点(px, pz)是否在三角形v0v1v2在XZ平面上的投影内，
        如果在则通过插值计算交点的Y值。

        Args:
            px: 射线X坐标
            pz: 射线Z坐标
            v0: 三角形顶点0 (x, y, z)
            v1: 三角形顶点1 (x, y, z)
            v2: 三角形顶点2 (x, y, z)

        Returns:
            float: 交点Y值，如果无交点则返回None
        """
        # 计算三角形在XZ平面上的投影
        # 使用重心坐标判断点(px, pz)是否在投影三角形内
        d00 = (v1[0] - v0[0]) * (v1[0] - v0[0]) + (v1[2] - v0[2]) * (v1[2] - v0[2])
        d01 = (v1[0] - v0[0]) * (v2[0] - v0[0]) + (v1[2] - v0[2]) * (v2[2] - v0[2])
        d11 = (v2[0] - v0[0]) * (v2[0] - v0[0]) + (v2[2] - v0[2]) * (v2[2] - v0[2])
        d20 = (px - v0[0]) * (v1[0] - v0[0]) + (pz - v0[2]) * (v1[2] - v0[2])
        d21 = (px - v0[0]) * (v2[0] - v0[0]) + (pz - v0[2]) * (v2[2] - v0[2])

        denom = d00 * d11 - d01 * d01
        if abs(denom) < 1e-10:
            return None  # 退化三角形

        v = (d11 * d20 - d01 * d21) / denom
        w = (d00 * d21 - d01 * d20) / denom
        u = 1.0 - v - w

        # 判断重心坐标是否在三角形内（含边界）
        if u < -1e-6 or v < -1e-6 or w < -1e-6:
            return None

        # 通过重心坐标插值计算Y值
        y = u * v0[1] + v * v1[1] + w * v2[1]
        return float(y)

    def test_pos_in_model_rect(self,value:Vector3D):

        pos=Vector3D(value.x,value.y,value.z)

        return  (self.minPos.x < pos.x and self.minPos.z < pos.z and self.maxPos.x > pos.x and self.maxPos.z > pos.z)
    def drawGpsPointTomap(self, latitude, longitude):
        return
        # print(latitude,longitude)
        pos = settings.gps_to_world_pos((latitude, longitude))

        if  self.test_pos_in_model_rect(pos):
            if self.imWidth < 200 or self.imHeight < 200:
                self.imWidth, self.imHeight = 250, 250
                self.white_image = Image.new('RGBA', (self.imWidth, self.imHeight), color='white')

            tx= ( pos.x-self.minPos.x)/ (self.maxPos.x-self.minPos.x)
            tz= ( pos.z-self.minPos.z)/( self.maxPos.z-self.minPos.z)


            draw = ImageDraw.Draw(self.white_image)
            cx, cy = self.imWidth *tx, self.imHeight *tz
            radius = 2  # 半径，可根据需要调整
            left = cx - radius
            top = cy - radius
            right = cx + radius
            bottom = cy + radius
            draw.ellipse((left, top, right, bottom), fill='red')  # 实心圆
            white_image_data = np.asarray(self.white_image, dtype=np.uint8)
            self.loadMapTexture.imageToTexTure(white_image_data, self.imWidth, self.imHeight)





    def upData(self):


        if not self.visible:
            return
        if self.wood_texture is None or self.vertices is None:
            return




        self.scene3D.context3D.setProgram(self.shader)
        self.scene3D.context3D.setRenderTexture(self.shader, 'imageTexture', 0, self.wood_texture)
        self.scene3D.context3D.setRenderTexture(self.shader, 'loadMapTexture', 1, self.loadMapTexture)

        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'model', self.getPosMatrix())
        self.scene3D.context3D.setVcMatrix4fv(self.shader, 'vpMatrix3D', self.scene3D.camera3D.vpMatrix3D)
        rect=[self.minPos.x,self.minPos.z,self.maxPos.x-self.minPos.x,self.maxPos.z-self.minPos.z]
        self.scene3D.context3D.setVc4fv(self.shader, "roundRect", rect, 1);

        glBindVertexArray(self.vao)

        # 根据是否有索引数据选择渲染方式
        if self.use_indices:
            glDrawElements(GL_TRIANGLES, self.index_count, GL_UNSIGNED_INT, None)
        else:
            glDrawArrays(GL_TRIANGLES, 0, self.vertex_count)

    def set_route_cords(self, route_cords):
        """接收路径坐标数据
        
        Args:
            route_cords: 路径坐标列表 [(x, y), ...]
        """
        self.route_cords = route_cords
        print(f"AreaMapDisplay3DSprite已接收路径数据，包含{len(route_cords)}个坐标点")
        
    def destroy(self):
        glDeleteVertexArrays(1, (self.vao,))
        glDeleteBuffers(1, (self.vbo,))
        if self.use_indices:
            glDeleteBuffers(1, (self.ebo,))
