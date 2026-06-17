import string

from ..event.EventDispatcher import EventDispatcher
from ..event.InteractiveEvent import InteractiveEvent


class UIStage(EventDispatcher):

    def interactiveEvent(self, value=InteractiveEvent()):

        evtType = value.type;

        if not (evtType in self.eventsMap):
            return False

        arr = self.eventsMap[evtType];
        if len(arr) == 0:
            return False;

        for i in range(len(arr)):
            eventBin = arr[i];
            eventBin['listener'](value)

        return True
