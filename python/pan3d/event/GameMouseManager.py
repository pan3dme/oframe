import pygame
import pygame as pg
import matplotlib.pyplot as plt

from ..core.Vector2D import Vector2D
from ..event.UIStage import UIStage
from ..event.InteractiveEvent import InteractiveEvent

import pynput.mouse as mouse
from pynput.mouse import Listener


class GameMouseManager:
    def foo(self):
        pass

    def __init__(self):
        self.resetPos = Vector2D();
        self.bindPos = Vector2D();
        self.lastMouseState = False
        self.uiBlankStage = UIStage()

    def mouseToEvent(self, value, pos_x, pos_y, pos_dy=0, button=None):
        evt = InteractiveEvent(value);
        evt.x = pos_x;
        evt.y = pos_y;
        evt.wheelNum = pos_dy;
        evt.button = button;
        self.uiBlankStage.interactiveEvent(evt);
        pass

    skipNum = 0;

    def run(self):
        pass


GameMouseManagerGetInstance = GameMouseManager()


def on_move(x, y):
    # print('鼠标移动到 ({0}, {1})'.format(x, y))
    GameMouseManagerGetInstance.mouseToEvent(InteractiveEvent.Move, x, y)


def on_click(x, y, button, pressed):
    if pressed:
        # print('鼠标点击在 ({0}, {1})'.format(x, y))
        GameMouseManagerGetInstance.mouseToEvent(InteractiveEvent.Down, x, y, button=button)
    else:
        GameMouseManagerGetInstance.mouseToEvent(InteractiveEvent.Up, x, y, button=button)


def on_scroll(x, y, dx, dy):
    # print('鼠标滚轮滚动 {0} 滚动单位'.format(dy))
    GameMouseManagerGetInstance.mouseToEvent(InteractiveEvent.WheelEvent, x, y, dy)


listener = mouse.Listener(on_move=on_move, on_click=on_click, on_scroll=on_scroll)
# 启动监听器
listener.start()
