from ..res.ResCount import ResCount
import pygame
import pygame as pg
from OpenGL.GL import *

from io import BytesIO

from PIL import Image


class TextureRes(ResCount):
    def __init__(self, scene, url:str=None):
        super().__init__(scene);
        self.texture = None;
        self.width: int = 0;
        self.height: int = 0;
        if url is not None:
            self.makeTextureByUrl(url)
    def makeTextureByUrl(self,filepath:str):
        # u = 2314443320, 1968772470 & fm = 253 & fmt = auto & app = 138 & f = JPEG(1).png
        # u=2314443320,1968772470&fm=253&fmt=auto&app=138&f=JPEG(1).png
        # u_2314443320_1968772470_fm_253_fmt_auto_app_138_f_JPEG_1__png
        self.texture = glGenTextures(1);
        glBindTexture(GL_TEXTURE_2D, self.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        image = Image.open(filepath).convert("RGBA")
        image_width, image_height = image.size
        image_data = image.tobytes()
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data)
        glGenerateMipmap(GL_TEXTURE_2D)

    def use(self):
        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_2D, self.texture);

    def byteToTexTure(self, imgByte):
        image_bytes_io = BytesIO(imgByte)
        image = Image.open(image_bytes_io)

        image = pygame.image.frombuffer(image.tobytes(), image.size, image.mode)
        image_width, image_height = image.get_rect().size;
        image_data = pygame.image.tostring(image, "RGBA");
        self.imageToTexTure(image_data,image_width,image_height)

    def imageToTexTure(self, image_data,image_width,image_height):

        self.texture = glGenTextures(1);
        glBindTexture(GL_TEXTURE_2D, self.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);



        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data)
        glGenerateMipmap(GL_TEXTURE_2D)




