import struct

from ..base.ResGC import ResGC
from ..res.GroupRes import GroupRes


class GroupDataManager(ResGC):
    def __init__(self, scene):
        super().__init__(scene)
        self.loadDic = {}

    def getGroupData(self, url, bfun):
        if url in self.dic:
            bfun(self.dic[url])

        elif url in self.loadDic:
            self.loadDic[url].append(bfun);
        else:
            self.loadDic[url] = []
            self.loadDic[url].append(bfun);
            group: GroupRes = GroupRes(self.scene3D);

            def fuckBack():
                self.dic[url] = group;
                group.initReg();
                ary = self.loadDic[url];
                for i in range(len(ary)):
                    ary[i](self.dic[url])
                del self.loadDic[url];


            group.load(url, fuckBack);
