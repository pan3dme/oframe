import moderngl as mgl
from ..base.ResGC import ResGC
from ..mateial.TextureRes import TextureRes
from ..mateial.Material import Material
import pygame
import pygame as pg
from OpenGL.GL import *

from io import BytesIO

from PIL import Image


class TextureManager(ResGC):
    def __init__(self, scene):
        super().__init__(scene)

    def getTexture(self, url: str, bfun: any, wrapType: int = 0, info: any = None, filteType: int = 0, mipmapType: int = 0):
        if url in self.dic:
            if info is None:
                bfun(self.dic[url])
            else:
                bfun(self.dic[url],info)


        else:
            print('要加图片')

    def addRes(self, url, imgByte):
        textureRes = TextureRes(self.scene3D);
        textureRes.byteToTexTure(imgByte);
        self.dic[url] = textureRes;
