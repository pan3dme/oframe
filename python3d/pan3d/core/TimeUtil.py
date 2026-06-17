import time


class TimeFunTick:
    def __init__(self):
        self.alltime: int = 0;
        self.time: int = 0;
        self.fun: any = None;

    def update(self, t: int):
        self.time += t;
        if self.time >= self.alltime:
            self.fun();
            self.time = 0;


class TimeFunOut:
    def __init__(self):
        self.alltime: int = 0;
        self.time: int = 0;
        self.bfun: any = None;
        pass

    def update(self, t: int):
        self.time += t;
        if self.time >= self.alltime:
            self.bfun();
            return True;
        else:
            return False;


class TimeUtil:
    def __init__(self):

        self.lastTime = time.time()
        self.time = self.getTimer();
        self.outTimeFunAry: list = [];
        self.funAry:list =[];

        pass

    def update(self):
        dtime: int = self.getTimer() - self.time;

        for  i in range(len(self.outTimeFunAry)):
            idx=len(self.outTimeFunAry)-i-1;
            if   self.outTimeFunAry[idx].update(dtime):
                 del self.outTimeFunAry[idx]

        self.time = self.getTimer();

        pass

    def getTimer(self):
        current_time = time.time() - self.lastTime  # 获取当前时间的浮点数
        milliseconds = int(current_time * 1000)  # 获取毫秒部分
        return milliseconds;

    def hasTimeOut(self, fun):
        for i in range(len(self.outTimeFunAry)):
            if self.outTimeFunAry[i].fun is fun:
                return True
        return False;

    def addTimeOut(self, time: int, bfun: any):
        if self.hasTimeOut(bfun):
            return;
        timeFunTick: TimeFunOut = TimeFunOut();
        timeFunTick.alltime = time;
        timeFunTick.bfun = bfun;
        timeFunTick.time = 0;
        self.outTimeFunAry.append(timeFunTick);




TimeUtilInter = TimeUtil();
