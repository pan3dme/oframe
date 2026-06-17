from ...mateial.Material import Material
from ...mateial.daematerial.DaeMaterialShader import DaeMaterialShader
from ...mateial.TextureRes import TextureRes
from collada.material import CImage, Surface, Sampler2D
import collada
import pygame as pg

from PIL import Image
import numpy as np

import os


class DaeMaterial(Material):
    def __init__(self, scene):
        super().__init__(scene)
        self.materialName: str = None
        self.shader = DaeMaterialShader(self)
        self.shader.encode()
        # self.mainimageurl:str=None;
        self.mainTextureRes: TextureRes = None;


    def makeBaseTexture(self):
        self.mainTextureRes = TextureRes(self.scene3D);
        imGwidth, imGheight = 100, 2  # 设置纹理的宽度和高度
        white_image = Image.new('RGBA', (imGwidth, imGheight), color='gray')
        white_image_data = np.array(list(white_image.getdata()), np.uint8)
        self.mainTextureRes.imageToTexTure(white_image_data, imGwidth, imGheight);
        pass
    def setMeshMaterials(self, value, path: str):

        # saveDir = os.path.dirname(path) + '/tex/'
        saveDir = os.path.dirname(path)+ '/'
        self.materialName = value.id;

        # params = value.effect.params;
        for param in value.effect.params:
            if isinstance(param, Surface):
                imageurl = param.image.path
                # imageurl = imageurl.replace('_jpg', '.jpg')
                imageurl = saveDir + imageurl
                print(imageurl)
                self.mainTextureRes = TextureRes(self.scene3D, imageurl)
                # self.mainTextureRes = TextureRes(self.scene3D,
                #                                  'res/dae/u=2314443320,1968772470&fm=253&fmt=auto&app=138&f=JPEG(1).png')

                pass
            if isinstance(param, Sampler2D):
                pass

            pass

        if self.mainTextureRes is None:
            self.makeBaseTexture();
        # self.makeBaseTexture();

        pass
