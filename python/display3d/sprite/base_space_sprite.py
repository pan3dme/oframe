
import numpy as np

from display3d.sprite.bace_scale_ball_sprite import BaseScaleBallSprite3D
from display3d.sprite.base_text_sprite import BaseTextSprite3D


class BaseSpaceSprite3D(BaseScaleBallSprite3D):

    def __init__(self, scene,text ,  color:  tuple = (1.0, 1.0, 1.0)):
        super().__init__(scene,color)

        self.text_sprite = BaseTextSprite3D(self.scene3D, text=text, font_size=64, color=(1.0, 1.0, 0.0))
        self.text_sprite .scaleX = 2
        self.text_sprite .scaleY = 2

    def upData(self):
        super().upData()
        self.text_sprite.x=self.x
        self.text_sprite.y=self.y+10
        self.text_sprite.z=self.z
        self.text_sprite.upData()
 