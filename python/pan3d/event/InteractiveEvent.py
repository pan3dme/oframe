from ..event.BaseEvent import BaseEvent


class InteractiveEvent(BaseEvent):
    Down = "down";
    Up = "up";
    Move = "move";
    WheelEvent = "WheelEvent";

    def __init__(self, value=''):
        super().__init__(value)
        self.x = 0;
        self.y = 0;
        self.wheelNum=0
        self.button = None
        self.data = None;
        pass
