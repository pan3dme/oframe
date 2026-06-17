import numpy as np

from ..res.ResCount import ResCount

from OpenGL.GL import *


class ObjData(ResCount):
    def __init__(self, scene):
        super().__init__(scene)
        self.buffArr = []
        self.vertices = []
        self.uvs = []
        self.lightuv = []
        self.nors = []
        self.tangents = []
        self.bitangents = []
        self.indexs = []
        self.vbo = None
        self.eboID = None

        self.vertex_count: int = 0
        self.stride: int = 0;
        self.uvsOffsets: int = 0;
        self.lightuvsOffsets: int = 0;
        self.gpufnish: bool = False;

    def upToGPU(self):
        if self.gpufnish:
            return
        self.gpufnish = True;

        glBindVertexArray(glGenVertexArrays(1));
        self.vbo = glGenBuffers(1);
        glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
        glBufferData(GL_ARRAY_BUFFER, self.buffArr.nbytes, self.buffArr, GL_STATIC_DRAW)





        indiceData = np.array(self.indexs, dtype=np.int32);
        glBindVertexArray(glGenVertexArrays(1));
        self.eboID = glGenBuffers(1)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self.eboID)
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, 4 * len(indiceData), indiceData, GL_STATIC_DRAW)


        # enable arrays
