from pan3d.core.Vector3D import Vector3D


class _Config:
    DEBUG = True
    DATABASE_URL = "mysql://localhost:3306/db"
    API_KEY = "12345"
    LOG_TABLE_NAME = 'devicelog001'
    DEVICETTABLE_NAME = 'devicetable001'
    ROUTETABLE_NAME = 'routetabel001'
    PLACETABLE_NAME = 'placetable001'

    scaleScene = 17500
    gps_bounds = {
        'top_left': (26.542654, 109.358596),  # (latitude, longitude)
        'bottom_right': (26.504617, 109.440105)  # (latitude, longitude)
    }
    centenGps = (26.525115, 109.394106)
    # ccav = (gps_bounds['bottom_right'][0]-gps_bounds['top_left'][0], gps_bounds['bottom_right'][1]-gps_bounds['top_left'][1])
    def gps_to_world_pos(self,gps):
        latitude,longitude=gps
        pos = Vector3D(-1 * (latitude - settings.centenGps[0]) * settings.scaleScene, 0,
                       (longitude - settings.centenGps[1]) * settings.scaleScene)
        pos.x = -pos.x
        pos.z = -pos.z
        return  pos

    def world_pos_to_gps(self, pos):
        latitude = settings.centenGps[0] + pos.x / settings.scaleScene
        longitude = settings.centenGps[1] - pos.z / settings.scaleScene
        return round(latitude, 6), round(longitude, 6)

settings = _Config()