import zlib

import struct
from io import BytesIO
from ..core.Vector3D import Vector3D


class Pan3dByteArray:
    def __init__(self):
        self.file:BytesIO = None
        # seek
        pass

    def readUTF(self):
        file = self.file
        lenNum = file.read(2);
        lenNum = int.from_bytes(lenNum, byteorder='big')
        byte = file.read(lenNum)
        return byte.decode('utf-8')

    def temp(self):
        file = self.file
        data = file.read(4);

        a = int.from_bytes(data, byteorder='big')

        return a;

    def readByte(self):
        file = self.file
        data = file.read(1);
        int_value = int.from_bytes(data)
        return int_value;

    def readFloatOneByte(self):
        file = self.file
        data = file.read(1);
        int_value = int.from_bytes(data)
        if int_value >= 128:
            int_value = int_value - 255;

        return (int_value + 128.0) / 255.0;

    def readVector3D(self, hasW:bool):
        p: Vector3D = Vector3D()
        p.x = self.readFloat()
        p.y = self.readFloat()
        p.z = self.readFloat()
        if hasW:
            p.w = self.readFloat()
        return p


    def readFloatTwoByte(self, scaleNum):
        return self.readShort() / scaleNum;

    def readShort(self):
        file = self.file
        data = file.read(2);
        short_value = struct.unpack('!h', data)[0]
        return short_value;

    def readFloat(self):
        file = self.file
        data = file.read(4);
        float_value = struct.unpack('!f', data)[0]

        return float_value

    def readBoolean(self):
        file = self.file
        data = file.read(1);
        return bool.from_bytes(data)

    def getZipByte(self):
        zipLen: int = self.readInt();
        file = self.file
        temp = Pan3dByteArray()
        temp.file = BytesIO(zlib.decompress(file.read(zipLen)));
        return temp

    def readUnsignedInt(self):
        file = self.file
        data = file.read(4);
        a = int.from_bytes(data, byteorder='big')
        return a

    def readInt(self):
        file = self.file
        data = file.read(4);
        return int.from_bytes(data, byteorder='big', signed=True);

    def getInt(self):
        file = self.file
        pos=file.tell()
        data = file.read(4);
        file.seek(pos)
        return int.from_bytes(data, byteorder='big', signed=True);

    def readUTFBytes(self, value: int):
        file = self.file
        byte = file.read(value)
        return byte.decode("utf-8");
