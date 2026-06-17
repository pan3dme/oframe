from ..mateial.ConstItem import ConstItem


class DynamicBaseConstItem:
    def __init__(self, scene):
        self.scene3D = scene;
        self.target: ConstItem;
        self.paramName: str;
        self.currentValue:list;
        self.targetOffset: int;
        self.type: int;

        pass

    def setType(self,value:int):
        self.type=value;
    def setTargetInfo(self, target: ConstItem, paramName: str, type: int):
        self.target = target;
        self.paramName = paramName;
        self.setType(type);
        if self.target:
            self.target.setDynamicOffset(self);

        self.currentValue = [0 for _ in range(type)]

        pass
