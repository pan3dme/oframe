from ..core.Vector3D import Vector3D


class MaterialVo(Vector3D):
    def __init__(self):
        self.type: int = 0;
        self.name: str = '';
        self.url: str = '';

    def meshDictXml(self, value):
        self.type = value['type']
        self.name = value['name']
        self.url = value['url']
        pass
