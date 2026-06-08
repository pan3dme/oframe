from ..mateial.DynamicBaseTexItem import DynamicBaseTexItem
from ..mateial.Curve import Curve
from ..mateial.TextureRes import TextureRes

from OpenGL.GL import *

from PIL import Image
import numpy as np


class DynamicTexItem(DynamicBaseTexItem):
    def __init__(self, value):
        super().__init__(value)
        self.isParticleColor: bool = False;
        self.url: str = None
        self.curve: Curve;
        self.textureDynamic: TextureRes = None

    def getUsetextureRes(self):
        if self.textureDynamic:
            return self.textureDynamic;
        else:
            return super().getUsetextureRes()
        pass

    def initCurve(self, type: int):
        self.curve = Curve(self.scene3D);
        self.curve.type = type;
        pass

    def creatTextureByCurve(self):

        endVecIndex: int = len(self.curve.valueVec) - 1;
        imgNumVec: list = [];
        for i in range(int(self.life)):
            if i < self.curve.begintFrame:

                imgNumVec.extend([self.curve.valueVec[0][0] * 0xff, self.curve.valueVec[0][1] * 0xff,
                                  self.curve.valueVec[0][2] * 0xff, self.curve.valueVec[0][3] * 0xff]);

            elif i > self.curve.maxFrame:
                if self.curve.maxFrame == 0 and self.curve.begintFrame < 0:
                    imgNumVec.extend([0xff, 0xff, 0xff, 0xff]);
                else:
                    imgNumVec.extend([self.curve.valueVec[endVecIndex][0] * 0xff,
                                      self.curve.valueVec[endVecIndex][1] * 0xff,
                                      self.curve.valueVec[endVecIndex][2] * 0xff,
                                      self.curve.valueVec[endVecIndex][3] * 0xff]);


            else:
                if self.curve.begintFrame < 0:
                    imgNumVec.extend([0xff, 0xff, 0xff, 0xff]);
                else:
                    index: int = i - self.curve.begintFrame;
                    imgNumVec.extend([self.curve.valueVec[index][0] * 0xff, self.curve.valueVec[index][1] * 0xff,
                                      self.curve.valueVec[index][2] * 0xff, self.curve.valueVec[index][3] * 0xff]);

        imgVecLen: int = int(len(imgNumVec) / 4);
        self.textureDynamic = TextureRes(self.scene3D);
        imGwidth, imGheight = 100, 2  # 设置纹理的宽度和高度
        white_image = Image.new('RGBA', (imGwidth, imGheight), color='red')
        pixels = white_image.load()

        for x in range(imGwidth):
            for y in range(imGheight):
                idx: int = int(x / imGwidth * imgVecLen) * 4;
                pixels[x, y] = (
                    int(imgNumVec[idx + 0]), int(imgNumVec[idx + 1]), int(imgNumVec[idx + 2]), int(imgNumVec[idx + 3]))

        white_image_data = np.array(list(white_image.getdata()), np.uint8)
        self.textureDynamic.imageToTexTure(white_image_data, imGwidth, imGheight);

        pass
