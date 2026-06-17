class BaseAnim:
    def __init__(self):
        self.baseNum: int = 0;
        self.num: int = 0;
        self.time: int = 0;
        self.speed: float = 0;
        self.aSpeed: float = 0;
        self.beginTime: int = 0;
        self.lastTime: int = 0;
        self.baseTime: int = 0;
        self.isActiva = False;
        self.isDeath = False;

    def reset(self):
        self.isActiva = False;
        self.isDeath = False;
        self.time = 0;
        self.num = 0;

    def coreCalculate(self):
        self.num = self.speed * self.time + self.aSpeed * self.time * self.time + self.baseNum;
        pass

    def update(self, t: int):
        if self.isDeath:
            return;
        self.time = t - self.baseTime;
        if self.isActiva:
            self.time = self.time - self.beginTime;
            if self.time > self.lastTime:
                self.time = self.lastTime - self.beginTime;
                self.isDeath = True;
            self.coreCalculate();
        elif self.time >= self.beginTime:
            if self.time >= self.lastTime:
                self.time = self.lastTime - self.beginTime;
                self.coreCalculate();
                self.isDeath = True;
            else:
                self.time = self.time - self.beginTime;
                self.coreCalculate();

            self.isActiva = True;
