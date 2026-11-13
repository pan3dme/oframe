from pan3d.core.Vector3D import Vector3D
from pan3d.scene3D.Camera3D import Camera3D


class _Config:
    DEBUG = True
    DATABASE_URL = "mysql://localhost:3306/db"
    API_KEY = "12345"
    LOG_TABLE_NAME = 'devicelog001'
    # 设备表
    DEVICETTABLE_NAME = 'devicetable001'
    # 设备位置刷新表
    DEVICE_LOC_REFRESH_TABLE_NAME = 'device_lot_refrsh'
    # 牛羊表
    COWSHEEP_TABLE_NAME = 'cowsheep002'
    ROUTETABLE_NAME = 'routetabel001'
    PLACETABLE_NAME = 'placetable001'

    scaleSceneX = 18000
    scaleSceneZ = 15000
    current_mode = "large_2d"
    baseCamDis100 = -100
    camera3D = Camera3D()
    gps_bounds = {
        'top_left': (26.542654, 109.358596),  # (latitude, longitude)
        'bottom_right': (26.504617, 109.440105)  # (latitude, longitude)
    }
    centenGps = (26.525115, 109.394106)
    # Google 摄影测量 3D Tiles（Map Tiles API）：填 API key 后可用，留空则入口提示
    GOOGLE_MAPS_API_KEY = ''
    GOOGLE_3D_RADIUS_DEG = 0.004      # 加载半径（度），约 440m
    GOOGLE_3D_MAX_TILES = 120         # 单次最多加载瓦片数
    GOOGLE_3D_GE_ERROR = 12.0         # 几何误差阈值：越小越精细（瓦片越多）
    # ccav = (gps_bounds['bottom_right'][0]-gps_bounds['top_left'][0], gps_bounds['bottom_right'][1]-gps_bounds['top_left'][1])
    def gps_to_world_pos(self,gps):
        latitude,longitude=gps
        pos = Vector3D(-1 * (latitude - settings.centenGps[0]) * settings.scaleSceneX, 0,
                       (longitude - settings.centenGps[1]) * settings.scaleSceneZ)
        pos.x = -pos.x
        pos.z = -pos.z
        return  pos

    def world_pos_to_gps(self, pos):
        latitude = settings.centenGps[0] + pos.x / settings.scaleSceneX
        longitude = settings.centenGps[1] - pos.z / settings.scaleSceneZ
        return round(latitude, 6), round(longitude, 6)

settings = _Config()