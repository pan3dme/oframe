import collada
import numpy as np

from ..base.ResGC import ResGC
from ..res.DaeRes import DaeRes


class DaeColladaManager(ResGC):
    def __init__(self, scene):
        super().__init__(scene)
        self.loadDic = {}

    def getDaeScene(self, url, bfun):
        if url in self.dic:
            bfun(self.dic[url])

        elif url in self.loadDic:
            self.loadDic[url].append(bfun);
        else:
            self.loadDic[url] = []
            self.loadDic[url].append(bfun);
            daeres: DaeRes = DaeRes(self.scene3D);

            def fuckBack():
                self.dic[url] = daeres;
                bfun(self.dic[url])
                del self.loadDic[url];

            daeres.load(url, fuckBack);
