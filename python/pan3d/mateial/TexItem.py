from ..mateial.TextureRes import TextureRes


class TexItem:
    LIGHTMAP: int = 1;
    LTUMAP: int = 2;
    CUBEMAP: int = 3;
    HEIGHTMAP: int = 4;
    REFRACTIONMAP: int = 5;

    def __init__(self):
        self.url: str;
        self.id: int;
        self.textureRes: TextureRes = None;
        self.isDynamic: bool;
        self.paramName: str;
        self.isParticleColor: bool;
        self.isMain: bool;

        self.type: int;

        self.wrap: int;
        self.filter: int;
        self.mipmap: int;
        self.permul: bool;

    def getName(self):
        return "fs" + str(self.id);
