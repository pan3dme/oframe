from ..mateial.Curve import Curve
from ..mateial.DynamicBaseConstItem import DynamicBaseConstItem


class DynamicConstItem(DynamicBaseConstItem):
    def __init__(self, scene):
        super().__init__(scene);
        self.curve: Curve;

    def update(self, t: int):
        self.currentValue = self.curve.getValue(t);
        self.target.setDynamic(self);
        pass

    def setType(self, value: int):
        super().setType(value)
        self.curve = Curve(self.scene3D);
        self.curve.type = value;
