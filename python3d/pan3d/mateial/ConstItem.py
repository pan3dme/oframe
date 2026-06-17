from ..core.Vector3D import Vector3D


class ConstItem:
    def __init__(self):
        self.name: str;
        self.value: Vector3D;
        self.vecNum: list=[];

        self.paramName0: str;
        self.param0Type: int;
        self.param0Index: int;

        self.paramName1: str;
        self.param1Type: int;
        self.param1Index: int;

        self.paramName2: str;
        self.param2Type: int;
        self.param2Index: int;

        self.paramName3: str;
        self.param3Type: int;
        self.param3Index: int;

        self.isDynamic: bool;

        self.offset: int = 0;

    def creat(self, value: list):
        self.vecNum = value;
        self.vecNum[0 + self.offset] = self.value.x;
        self.vecNum[1 + self.offset] = self.value.y;
        self.vecNum[2 + self.offset] = self.value.z;
        self.vecNum[3 + self.offset] = self.value.w;
        pass

    def setDynamicOffset(self,dynamic: any):
        if self.paramName0 == dynamic.paramName:
            dynamic.targetOffset = self.param0Index + self.offset;
        elif self.paramName1 == dynamic.paramName:
            dynamic.targetOffset = self.param1Index + self.offset;
        elif self.paramName2 == dynamic.paramName:
            dynamic.targetOffset = self.param2Index + self.offset;
        elif self.paramName3 == dynamic.paramName:
            dynamic.targetOffset = self.param3Index + self.offset;


        pass

    def setDynamic(self,dynamic: any):
        for i in range(len(dynamic.currentValue)):
            self.vecNum[dynamic.targetOffset +i]= dynamic.currentValue[i]

        pass
