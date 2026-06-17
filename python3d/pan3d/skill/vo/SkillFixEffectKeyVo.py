from ...skill.vo.SkillKeyVo import SkillKeyVo
from ...core.Vector3D import Vector3D


class SkillFixEffectKeyVo(SkillKeyVo):
    def __init__(self):
        super().__init__()
        self.pos: Vector3D = None;
        self.rotation: Vector3D = None;
        self.hasSocket: bool = None;
        self.socket: str = None;

    def setData(self, data: any):
        super().setData(data)
        self.hasSocket = data['hasSocket']
        if self.hasSocket:
            self.socket = data['socket'];
        else:
            self.pos = Vector3D(data['pos'].x, data['pos'].y, data['pos'].z);
            self.rotation = Vector3D(data['rotation'].x, data['rotation'].y, data['rotation'].z);
