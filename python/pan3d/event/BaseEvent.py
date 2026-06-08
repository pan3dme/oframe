class BaseEvent:
    COMPLETE = 'complete';

    def __init__(self, value=''):
        self.type = value
        self.target = None;
        self.data = None;
