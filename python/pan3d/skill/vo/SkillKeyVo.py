class SkillKeyVo:
    def __init__(self):
        self.frame: int = 0;
        self.url: str = None;
        pass

    def setData(self, data: any):
        self.frame = data['frame'];
        self.url = data['url'];
