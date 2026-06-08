
from ..mateial.TexItem import TexItem
from ..mateial.TextureRes import TextureRes
class DynamicBaseTexItem:
    def __init__(self,value):
        self.scene3D=value;
        self. target: TexItem;
        self .paramName: str;
        self. textureRes: TextureRes;

    def getUsetextureRes(self):
        return self. textureRes;
