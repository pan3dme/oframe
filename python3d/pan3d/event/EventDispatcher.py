from ..event.BaseEvent import BaseEvent


class EventDispatcher:
    def __init__(self):
        self.eventsMap = {};

    def addEventListener(self, types, listener, thisObject):

        if not (types in self.eventsMap):
            self.eventsMap[types] = [];

        arr = self.eventsMap[types];
        eventBin = {'listener': listener, 'thisObject': thisObject};
        for i in range(len(arr)):
            bin = arr[i];
            if bin['listener'] == listener and bin['thisObject'] == thisObject:
                print('重复添加', types)
                return;
        arr.append(eventBin)

    def dispatchEvent(self, event: BaseEvent):
        if event.type in self.eventsMap:
            arrlist: list = self.eventsMap[event.type];
            event.target = self;
            for i in range(len(arrlist)):
                eventBin: any = arrlist[i];
                eventBin['listener'](eventBin['thisObject'], event);



# module Pan3d {
#     export class EventDispatcher {
#
#         protected _eventsMap: Object = null;
#
#         public addEventListener(types: string, listener: Function, thisObject: any): void {
#             if (!this._eventsMap) {
#                 this._eventsMap = new Object;
#             }
#
#             var list: Array<any> = this._eventsMap[types];
#             if (!list) {
#                 list = this._eventsMap[types] = [];
#             }
#
#             var eventBin: any = { listener: listener, thisObject: thisObject };
#
#             for (var i: number = 0; i < list.length; i++) {
#                 var bin: any = list[i];
#                 if (bin.listener == listener && bin.thisObject == thisObject) {
#                     return;
#                 }
#             }
#
#             list.push(eventBin);
#
#         }
#
#         public removeEventListener(type: string, listener: Function, thisObject: any): void {
#             if (this._eventsMap == null) {
#                 return;
#             }
#             var list: Array<any> = this._eventsMap[type];
#             for (var i: number = 0; list && i < list.length; i++) {
#                 var bin: any = list[i];
#                 if (bin.listener == listener && bin.thisObject == thisObject) {
#                     list.splice(i, 1);
#                     return;
#                 }
#             }
#         }
#
#         public dispatchEvent(event: BaseEvent): boolean {
#             var eventMap: Object = this._eventsMap;
#             if (!eventMap) {
#                 return true;
#             }
#             var list: Array<any> = eventMap[event.type];
#
#             if (!list) {
#                 return true;
#             }
#             var length: number = list.length;
#             if (length == 0) {
#                 return true;
#             }
#
#             event.target = this;
#
#             for (var i: number = 0; i < length; i++) {
#                 var eventBin: any = list[i];
#                 eventBin.listener.call(eventBin.thisObject, event);
#             }
#         }
#
#     }
# }
