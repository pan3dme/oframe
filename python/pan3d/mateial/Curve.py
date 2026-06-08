from ..core.Matrix3D import Matrix3D
from ..scene3D.Scene_data import Scene_data


class Curve:
    def __init__(self, scene):
        self.scene3D = scene;
        self.type: int;
        self.valueVec: list;
        self.valueV3d: list;
        self.begintFrame: int;
        self.maxFrame: int;
        self.posMatrix: Matrix3D;
        self.valueV3d = [1, 1, 1, 1];
        pass

    def getValue(self, t: int):
        if not self.valueVec or self.begintFrame == -1:
            return self.valueV3d;

        flag: int = int(t / Scene_data.frameTime - self.begintFrame)

        if flag < 0:
            flag = 0;
        elif flag > self.maxFrame - self.begintFrame:
            flag = self.maxFrame - self.begintFrame;

        return self.valueVec[flag];

    def setData(self, obj: any):

        self.type = int(obj['type']);
        self.maxFrame = int(obj['maxFrame']);
        if len(obj['items']):
            self.begintFrame = obj['items'][0]['frame'];
        else:
            self.begintFrame = -1;

        lenNum: int = len(obj['values'][0]);
        ary: list = [];

        for i in range(lenNum):
            itemAry: list = []
            obj_values = obj['values'];
            if self.type == 1:
                itemAry.append(obj_values[0][i]);
            elif self.type == 2:
                itemAry.append(obj_values[0][i], obj_values[1][i]);
            elif self.type == 3:
                itemAry.append(obj_values[0][i], obj_values[1][i], obj_values[2][i]);
            elif self.type == 4:
                w: float = obj_values[3][i];
                itemAry.append(obj_values[0][i] * w);
                itemAry.append(obj_values[1][i] * w);
                itemAry.append(obj_values[2][i] * w);
                itemAry.append(w);

            ary.append(itemAry);

        #
        self.valueVec = ary;
        pass
