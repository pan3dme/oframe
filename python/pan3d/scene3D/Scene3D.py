from OpenGL.GL import *
from ..display.DispBaseTri import DispBaseTri
from ..display.DispBaseLine import DispBaseLine
from ..display.particle.CombineParticle import CombineParticle
from ..mateial.TextureManager import TextureManager
from ..display.Display3DSprite import Display3DSprite
from ..display.DispBase3dSprite import DispBase3dSprite
from ..scene3D.Camera3D import Camera3D
from ..scene3D.Context3D import Context3D
from ..core.Matrix3D import Matrix3D
from ..res.BaseRes import BaseRes
from ..base.GroupItem import GroupItem
from ..event.BaseEvent import BaseEvent
from ..res.GroupRes import GroupRes
from ..res.DaeRes import DaeRes
from ..skill.SkillManager import SkillManager
from ..skill.Skill import Skill
from ..base.ObjDataManager import ObjDataManager
from ..filemode.GroupDataManager import GroupDataManager
from ..filemode.MeshDataManager import MeshDataManager
from ..filemode.DaeColladaManager import DaeColladaManager
from ..filemode.ParticleManager import ParticleManager
from ..filemode.ResManager import ResManager
from ..mateial.MaterialManager import MaterialManager
from ..filemode.AnimManager import AnimManager
from ..program.ProgrmaManager import ProgrmaManager
from ..role.Display3dMovie import Display3dMovie
from ..res.SceneRes import SceneRes
from ..core.TimeUtil import TimeUtil
from ..core.TimeUtil import TimeUtilInter
from ..mateial.MaterialVo import MaterialVo
from ..core.Vector3D import Vector3D
from random import random

class Scene3D:

    def __init__(self):
        self.displayList = [];
        self.displayRoleList = [];
        self.time = 0
        self.mainChar: Display3dMovie = None
        self.skillPlayNum: int = 0

        self.groupDataManager: GroupDataManager = GroupDataManager(self)
        self.daeColladaManager: DaeColladaManager = DaeColladaManager(self)
        self.meshDataManager: MeshDataManager = MeshDataManager(self)
        self.materialManager: MaterialManager = MaterialManager(self)
        self.particleManager: ParticleManager = ParticleManager(self)
        self.progrmaManager: ProgrmaManager = ProgrmaManager(self)
        self.animManager: AnimManager = AnimManager(self)
        self.resManager: ResManager = ResManager(self)
        self.skillManager: SkillManager = SkillManager(self)
        self.objDataManager = ObjDataManager(self)
        self.textureManager = TextureManager(self)
        self.camera3D = Camera3D()
        self.context3D = Context3D()

        # self.makeTest()
        self.addGridLine()

        # self.addTempRit()



        # self.loadModelSprite('res/abc.txt',0.4)
        # self.loadModelSprite('res/fengche001.txt', 0.05)
        # self.loadRoleSprite('role/x_tds.txt',Vector3D())
        # self.loadRoleSprite('role/x_ssqx.txt')
        # self.loadRoleSprite('role/houzi.txt')
        # self.loadRoleSprite('role/yezhu.txt',Vector3D(10,0,0))
        # self.loadRoleSprite('role/50011.txt')
        # self.loadSceneByUrl('res/map/2021001.txt')
        # self.loadSceneByUrl('res/map/5555.txt')
        # self.loadDaeByUrl('res/dae/luomu.dae')
        # self.loadDaeByUrl('D:/yolov8/blander_model/blander/ofarm/luomu.dae')
        # self.loadDaeByUrl('C:/Users/34235/Desktop/eeeee/videobg.dae')
        # self.loadDaeByUrl('C:/Users/34235/Desktop/eeeee/videobg.dae')
        # self.loadDaeByUrl('res/dae/three.dae')

        # self.playParticle('res/model/ccav_lyf.txt')
        # self.playSkill();

        pass

    def playSkill(self):

        nameArr: list = ['skill_01', 'skill_02', 'skill_03', 'm_skill_01', 'm_skill_02', 'm_skill_03']

        if self.skillPlayNum < (len(nameArr) - 1):
            self.skillPlayNum = self.skillPlayNum + 1;
        else:
            self.skillPlayNum = 0

        skipName = nameArr[self.skillPlayNum]
        print(self.skillPlayNum, skipName)

        if not self.mainChar:
            self.loadSceneByUrl('res/map/2021001.txt')

        skill: Skill = self.skillManager.getSkill("res/skill/jichu_1_byte.txt", skipName);
        skill.reset();
        skill.configFixEffect(self.mainChar, None, None);
        self.mainChar.playSkill(skill);

    def doubelClikEvent(self):
        print('双击')

        def bfun():
            # self.playParticle('res/model/levelup_lyf.txt')
            # self.addYezhu()
            # self.playParticle('res/model/10018_lyf.txt')
            # self.playParticle('res/model/csm001_lyf.txt')
            # self.playParticle('res/model/ccav_lyf.txt')
            # self.playSkill();
            pass

        TimeUtilInter.addTimeOut(1, bfun);

        pass
    def addYezhu(self):


        self.loadRoleSprite('role/yezhu.txt', Vector3D(random() * 100-120, 0, random() * 100))
    def playParticle(self, url):
        def bfun(groupRes: GroupRes):
            for i in range(len(groupRes.dataAry)):
                item: GroupItem = groupRes.dataAry[i];
                if item.types == BaseRes.SCENE_PARTICLE_TYPE:
                    particle: CombineParticle = self.particleManager.getParticleByte(item.particleUrl);
                    self.particleManager.addParticle(particle);

                    def bfun(value, eventtype):
                        self.particleManager.removeParticle(value);
                        pass

                    particle.addEventListener(BaseEvent.COMPLETE, bfun, particle)
                else:
                    print('不是特效')

            pass

        self.groupDataManager.getGroupData(url, bfun)

        pass

    def loadDaeByUrl(self, url):
        def back(daeRes: DaeRes):
            for daeDisplaySprite in daeRes.daeSpriteItem:
                self.addDisplay(daeDisplaySprite)

        self.daeColladaManager.getDaeScene(url, back);

    def loadSceneByUrl(self, url):
        def back():
            self.buildSceneMoDel(sceneRes.sceneData['buildItem'])
            pass

        sceneRes: SceneRes = SceneRes(self);
        sceneRes.load(url, back)

        pass

    def buildSceneMoDel(self, arr):
        skipNum = 0;
        for i in range(len(arr)):
            itemObj = arr[i]
            if itemObj['type'] == BaseRes.PREFAB_TYPE:
                dis = self.getBuildSprite(itemObj);
                self.addDisplay(dis)
                pass
            elif itemObj['type'] == BaseRes.SCENE_PARTICLE_TYPE:
                if skipNum == 0:
                    particle: CombineParticle = self.getParticleSprite(itemObj)
                    self.particleManager.addParticle(particle);
                    pass
                skipNum = skipNum + 1;

                pass

                # getParticleSprite

    def getParticleSprite(self, itemObj):
        particle = self.particleManager.getParticleByte(itemObj['url']);
        particle.scaleX = itemObj['scaleX'];
        particle.scaleY = itemObj['scaleY'];
        particle.scaleZ = itemObj['scaleZ'];

        particle.setBindVecter3d(itemObj['x'], itemObj['y'], itemObj['z'])
        particle.setBindVecter3d(0, 0, 0);

        particle.rotationX = itemObj['rotationX'];
        particle.rotationY = itemObj['rotationY'];
        particle.rotationZ = itemObj['rotationZ'];
        particle.type = 0;
        return particle

    def makeTxtArrToMatelInfoArr(self, materialInfoArr):
        infoArr = [];
        for i in range(len(materialInfoArr)):
            materialVo: MaterialVo = MaterialVo()
            materialVo.meshDictXml(materialInfoArr[i])
            infoArr.append(materialVo);
        return infoArr

    def getBuildSprite(self, value):
        itemDisplay: Display3DSprite = Display3DSprite(self);
        if 'lighturl' in value:
            itemDisplay.setLighturl(value['lighturl']);

        itemDisplay.scaleX = value['scaleX'];
        itemDisplay.scaleY = value['scaleY'];
        itemDisplay.scaleZ = value['scaleZ'];
        itemDisplay.x = value['x'];
        itemDisplay.y = value['y'];
        itemDisplay.z = value['z'];
        itemDisplay.rotationX = value['rotationX'];
        itemDisplay.rotationY = value['rotationY'];
        itemDisplay.rotationZ = value['rotationZ'];

        itemDisplay.setObjUrl(value['objsurl']);

        if 'materialInfoArr' in value:
            itemDisplay.setMaterialUrl(value['materialurl'], self.makeTxtArrToMatelInfoArr(value['materialInfoArr']));
        else:
            print('需要补充')

        return itemDisplay;

    def makeTest(self):

        matrix3D: Matrix3D = Matrix3D()
        matrix3D.appendScale(-2, 3, 4);
        matrix3D.outStr();
        pass

    def addGridLine(self):
        dispBaseLine = DispBaseLine(self);

        self.displayList.append(dispBaseLine)
        pass

    def addTempRit(self):
        trianglea = DispBaseTri(self);
        trianglea.y = 1;
        self.displayList.append(trianglea)

    def loadRoleSprite(self, url,pos:Vector3D):
        role = Display3dMovie(self)
        role.setRoleUrl(url)
        role.scaleX = 0.5;
        role.scaleY = 0.5;
        role.scaleZ = 0.5;
        role.x=pos.x;
        role.y=5;
        role.z=pos.z;
        role.rotationY=random()*360
        if len(self.displayRoleList)%2==0:
            role.curentAction: str = 'walk'

        self.displayRoleList.append(role);
        self.mainChar = role;
        pass

    def loadModelSprite(self, url, scale: float = 1.0):
        dis = Display3DSprite(self);
        dis.scaleX = scale;
        dis.scaleY = scale;
        dis.scaleZ = scale;
        dis.setModelUrl(url)
        self.addDisplay(dis);
        pass

    def removeDisplay(self, value):
        if value in self.displayList:
            self.displayList.remove(value)
        if value in self.displayRoleList:
            self.displayRoleList.remove(value)
    def addDisplay(self, value):
        self.displayList.append(value);

    def updateFrameRole(self):
        delayt = TimeUtilInter.getTimer() - self.time;
        self.time = TimeUtilInter.getTimer();
        for temp in self.displayRoleList:
            temp.updateFrame(delayt)
        pass

    def update(self):

        TimeUtilInter.update()
        self.camera3D.upData()
        self.updateFrameRole()
        self.context3D.setBaseRender();
        self.context3D.setWriteDepth(True);
        self.context3D.setBlendParticleFactors(0);

        for temp in self.displayList:
            temp.upData()
        for temp in self.displayRoleList:
            temp.upData()

        self.particleManager.upFrame()
        self.skillManager.update();

        # t=TimeUtilInter.getTimer()-self.time;
        # print(t)
        # self.time=TimeUtilInter.getTimer();
