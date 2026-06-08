import numpy as np

from display3d.sprite.base_color_sprite import BaseColorSprite3D


class BaseBallSprite3D(BaseColorSprite3D):

    def __init__(self, scene, color: tuple = (1.0, 1.0, 1.0)):
        super().__init__(scene, color)

    def makeVertices(self, color: tuple = (1.0, 1.0, 1.0)):
        r, g, b = color
        radius = 5.0
        sectors = 24  # 经线数
        stacks = 16   # 纬线数

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
