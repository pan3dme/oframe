# -*- coding: utf-8 -*-
"""
数据层：函数名与小程序 config/data-cache.js 一一对应，底层用阿里云 TableStore(OTS) 直连
（与原 Python 项目获取方式一致）。仅 DTU 指令下发无 OTS 等价物，走 HTTP 云函数 sendtodtucmd。

表结构与主键（实测确认）：
- sys_user        PK username                  属性: password/wechatid/level/mapcenter
- devicetable001  PK deviceId                  属性: rename/ProductKey/DeviceName/DeviceSecret/visible/picurl/link_cowsheep_id
- device_time     PK deviceId                  属性: lorastr/time/upDateDevice      ← getDeviceSyncAll
- device_config   PK deviceId                  属性: lorastr/time/upDateDevice      ← getDeviceConfigAll
- device_gps      PK deviceId                  属性: lorastr/time/upDateDevice      ← getDeviceGpsAll
- devicelog001    PK deviceId + auto_id(自增)   属性: deviceId/lorastr/time/upDateDevice/rssi/snr
- cowsheep002     PK cowsheep_id               属性: rename/birthday/gender/avatar
- cowsheep_video003 PK ossurl                  属性: cowsheep_id/time/video_id
- routetabel001   PK route_id                  属性: roadname/roadinfo/level/wechatid
- placetable001   PK placeid                   属性: name/gps/level/wechatid
"""

import os
import time
import json
import threading

import requests
from PyQt6.QtCore import QObject, Qt, pyqtSignal

# ==================== 表名与地址 ====================

OTS_ENDPOINT = "https://tabel001.cn-shanghai.ots.aliyuncs.com"
OTS_INSTANCE = "tabel001"

SYS_USER_TABLE = 'sys_user'
DEVICE_TABLE = 'devicetable001'
DEVICE_TIME_TABLE = 'device_time'
DEVICE_CONFIG_TABLE = 'device_config'
DEVICE_GPS_TABLE = 'device_gps'
DEVICE_LOG_TABLE = 'devicelog001'
COWSHEEP_TABLE = 'cowsheep002'
COWSHEEP_VIDEO_TABLE = 'cowsheep_video003'
ROAD_TABLE = 'routetabel001'
PLACE_TABLE = 'placetable001'

API_SEND_DTU_CMD_URL = "https://gpsmoveinfo.cn/fc/sendtodtucmd"
SEND_CMD_TIMEOUT = 15


def _row_to_dict(row):
    """TableStore Row 对象 → dict（主键列与属性列合并）"""
    if row is None:
        return None
    result = {}
    for item in (row.primary_key or []):
        result[item[0]] = item[1]
    for col in (row.attribute_columns or []):
        result[col[0]] = col[1]
    return result


def _to_int(value, default=0):
    try:
        return int(float(str(value).strip()))
    except (TypeError, ValueError):
        return default


def _visible_str(value):
    """visible 列归一化为 '1'/'0'（表里存过 True/'1' 等多种形态）"""
    return '1' if str(value).strip().lower() in ('1', 'true') else '0'


def _log_err(title, detail):
    """错误落盘，便于排查 GUI 环境下看不到控制台的问题"""
    try:
        with open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                               'wx_api_err.log'), 'a', encoding='utf-8') as f:
            f.write(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {title}: {detail}\n")
    except OSError:
        pass


class _CallbackHub(QObject):
    """
    异步回调派发中心（WxApi 持有、随程序存活，主线程亲和）。
    工作线程发出 _done 信号（显式 QueuedConnection），事件进入主线程事件循环，
    _dispatch 在主线程执行真正的页面回调 —— 保证 UI 操作永远在主线程。
    不用 QRunnable：PyQt 6.12 会把 QRunnable 所有权移交给 C++ 线程池，
    run() 结束即删 C++ 对象，Python 侧保留引用再 GC 会二次释放直接崩溃。
    """

    _done = pyqtSignal(object)

    def __init__(self):
        super().__init__()
        self._done.connect(self._dispatch, Qt.ConnectionType.QueuedConnection)

    def _dispatch(self, payload):
        kind, callback, arg = payload
        try:
            if callback:
                callback(arg)
            else:
                print(f"[API{kind}]", arg)
        except Exception:
            import traceback
            _log_err(f'异步回调{kind}异常', traceback.format_exc())


class WxApi:
    """
    全局数据层（单实例共享）。
    函数名与小程序 data-cache.js 相同；实现为 OTS 直连。
    """

    def __init__(self):
        self.wechatid = ''
        self.level = 99
        self.mapcenter = ''
        self.username = ''
        self.password = ''
        self.logged_in = False
        self._ots = None
        self._hub = _CallbackHub()   # 异步回调派发中心（主线程亲和，随程序存活）
        self._road_cache = None   # getRoadListFromCache 的永久缓存
        self._place_cache = None  # getPlaceListFromCache 的永久缓存

    # ==================== OTS 基础 ====================

    def _client(self):
        if self._ots is None:
            from tablestore import OTSClient
            access_key_id = os.getenv("ALIYUN_ACCESS_KEY_ID")
            access_key_secret = os.getenv("ALIYUN_ACCESS_KEY_SECRET")
            if not access_key_id or not access_key_secret:
                raise RuntimeError('未配置阿里云访问凭证（ALIYUN_ACCESS_KEY_ID/SECRET）')
            self._ots = OTSClient(OTS_ENDPOINT, access_key_id, access_key_secret, OTS_INSTANCE)
        return self._ots

    def _scan_all(self, table, pk_names, limit=1000):
        """全表扫描 → [dict]"""
        from tablestore import INF_MIN, INF_MAX, Direction
        start = [(n, INF_MIN) for n in pk_names]
        end = [(n, INF_MAX) for n in pk_names]
        ret = self._client().get_range(table, Direction.FORWARD, start, end, limit=limit)
        return [_row_to_dict(r) for r in ret[2]]

    def _scan_device(self, device_id, direction, limit=100):
        """按 deviceId 扫 devicelog001（主键 deviceId+auto_id 自增）→ [dict]；direction: Direction.FORWARD/BACKWARD"""
        from tablestore import INF_MIN, INF_MAX
        backward = (direction == 1)  # 兼容传入 1/2 数字
        start = [('deviceId', device_id), ('auto_id', INF_MAX if backward else INF_MIN)]
        end = [('deviceId', device_id), ('auto_id', INF_MIN if backward else INF_MAX)]
        from tablestore import Direction as _D
        ret = self._client().get_range(DEVICE_LOG_TABLE, _D.BACKWARD if backward else _D.FORWARD,
                                       start, end, limit=limit)
        return [_row_to_dict(r) for r in ret[2]]

    def run_async(self, fn, on_ok=None, on_err=None):
        """
        数据操作在后台 Python 线程执行（不卡界面）；
        on_ok/on_err 通过 _CallbackHub 的队列信号回到主线程执行（UI 操作安全）。
        """
        hub = self._hub

        def worker():
            try:
                res = fn()
            except Exception as e:
                import traceback
                _log_err('异步任务执行异常', traceback.format_exc())
                hub._done.emit(('err', on_err, str(e)))
            else:
                hub._done.emit(('ok', on_ok, res))

        threading.Thread(target=worker, daemon=True).start()

    def require_login(self):
        if not self.logged_in:
            raise RuntimeError('请先登录')

    def _mine(self, row):
        """按 wechatid 列过滤用户数据（表里没有 wechatid 列的行视为公共数据）"""
        wid = row.get('wechatid')
        if wid is None or str(wid) == '' or str(wid) == self.wechatid:
            return True
        return False

    def _owned_device(self, device_id):
        """
        设备归属判断：设备表没有 wechatid 列，服务器按 deviceId 前缀
        `{wechatid}-` 过滤（实测 getDeviceTaleAll 对 v4 只返回 v4-* 共17台/全表46台）。
        """
        wid = self.wechatid
        if not wid:
            return True
        did = str(device_id)
        return did.startswith(wid + '-') or did == wid

    # ==================== 登录（login） ====================

    def login(self, username, password):
        """登录：直查 sys_user 验证（主键 username，属性 password/wechatid/level/mapcenter）"""
        from tablestore import INF_MIN, INF_MAX
        consumed, row, _ = self._client().get_row(SYS_USER_TABLE, [('username', username)],
                                                  columns_to_get=None)
        user = _row_to_dict(row)
        if not user:
            raise RuntimeError('用户不存在')
        if str(user.get('password', '')) != str(password):
            raise RuntimeError('密码错误')
        wechatid = str(user.get('wechatid', '') or username)
        self.wechatid = wechatid
        self.level = _to_int(user.get('level', 99), 99)
        self.mapcenter = str(user.get('mapcenter', '') or '').strip()
        self.username = username
        self.password = password
        self.logged_in = True
        return user

    def logout(self):
        """注销登入：重置运行时登录状态并清空本地缓存数据"""
        self.wechatid = ''
        self.level = 99
        self.mapcenter = ''
        self.username = ''
        self.password = ''
        self.logged_in = False
        self._ots = None
        self._road_cache = None
        self._place_cache = None
        return True

    # ==================== 设备相关 ====================

    def getDeviceList(self, limit=30):
        """当前用户的设备 → [{deviceId,rename,lorastr,link_cowsheep_id,picurl,ProductKey,DeviceName,DeviceSecret,visible,time}]"""
        rows = []
        for row in self._scan_all(DEVICE_TABLE, ['deviceId'], limit=max(limit, 500)):
            if not self._owned_device(row.get('deviceId', '')):
                continue
            rows.append({
                'deviceId': str(row.get('deviceId', '')),
                'rename': str(row.get('rename', '') or ''),
                'lorastr': str(row.get('lorastr', '') or ''),
                'link_cowsheep_id': str(row.get('link_cowsheep_id', '') or ''),
                'picurl': str(row.get('picurl', '') or ''),
                'ProductKey': str(row.get('ProductKey', '') or ''),
                'DeviceName': str(row.get('DeviceName', '') or ''),
                'DeviceSecret': str(row.get('DeviceSecret', '') or ''),
                'visible': _visible_str(row.get('visible', '1')),
                'time': '',
            })
        return rows

    def getDeviceSyncAll(self, limit=30):
        """对时/状态表 → {deviceId: {rawTime,battery,lorastr,upDateDevice,rssi,snr,ts}}"""
        from crowui import wx_codec
        sync_map = {}
        for row in self._scan_all(DEVICE_TIME_TABLE, ['deviceId'], limit=max(limit, 500)):
            device_id = str(row.get('deviceId', ''))
            if not device_id or not self._owned_device(device_id):
                continue
            lorastr = str(row.get('lorastr', '') or '')
            ts = wx_codec.parse_time_to_ts(row.get('time', ''))
            existing = sync_map.get(device_id)
            if existing and ts <= existing.get('ts', 0):
                continue
            rssi = row.get('rssi', '')
            sync_map[device_id] = {
                'deviceId': device_id, 'lorastr': lorastr,
                'rawTime': str(row.get('time', '') or ''),
                'upDateDevice': str(row.get('upDateDevice', '') or ''),
                'battery': wx_codec.lorastr_battery(lorastr),
                'rssi': _to_int(rssi) if str(rssi) != '' else 0,
                'snr': _to_int(row.get('snr', 0)),
                'ts': ts,
            }
        return sync_map

    def getDeviceConfigAll(self):
        """配置表 → {deviceId: {lorastr, reportInterval, mainPeriodMin, powerWin, gpsWin, time}}"""
        from crowui import wx_codec
        config_map = {}
        for row in self._scan_all(DEVICE_CONFIG_TABLE, ['deviceId'], limit=500):
            device_id = str(row.get('deviceId', ''))
            if not device_id or not self._owned_device(device_id):
                continue
            lorastr = str(row.get('lorastr', '') or '')
            cfg = wx_codec.parse_device_config_lorastr(lorastr)
            cfg.update({'lorastr': lorastr, 'time': str(row.get('time', '') or ''),
                        'upDateDevice': str(row.get('upDateDevice', '') or '')})
            config_map[device_id] = cfg
        return config_map

    def getDeviceGpsAll(self):
        """device_gps 表 → [{deviceId,lorastr,rawTime,upDateDevice,lat,lng,ts}]"""
        from crowui import wx_codec
        gps_map = {}
        for row in self._scan_all(DEVICE_GPS_TABLE, ['deviceId'], limit=500):
            device_id = str(row.get('deviceId', ''))
            if not device_id or not self._owned_device(device_id):
                continue
            lorastr = str(row.get('lorastr', '') or '')
            coord = wx_codec.lorastr_coord(lorastr)
            ts = wx_codec.parse_time_to_ts(row.get('time', ''))
            if not coord:
                continue
            existing = gps_map.get(device_id)
            if existing and ts <= existing.get('ts', 0):
                continue
            lat, lng = coord
            gps_map[device_id] = {
                'deviceId': device_id, 'lorastr': lorastr,
                'rawTime': str(row.get('time', '') or ''),
                'upDateDevice': str(row.get('upDateDevice', '') or ''),
                'lat': lat, 'lng': lng, 'ts': ts,
            }
        return list(gps_map.values())

    def getDeviceLogbyId(self, device_id, limit=10, offset=0, log_type=None):
        """
        设备日志（最新在前）。
        log_type: 1=定位 2=对时 None=全部；offset 跳过前 N 条（类型过滤后）。
        """
        rows = self._scan_device(device_id, 1, limit=min(offset + limit, 500))  # 1=BACKWARD
        if log_type:
            from crowui import wx_codec
            rows = [r for r in rows if wx_codec.lorastr_type(r.get('lorastr', '')) == log_type]
        return rows[offset:offset + limit]

    def getDeviceLogGpsbyId(self, device_id, curdate, limit=99):
        """当天 GPS 轨迹 → [{deviceId,gps("lat,lng"),time,lorastr}]（按时间正序）"""
        from datetime import datetime
        from crowui import wx_codec
        try:
            target_date = datetime.strptime(curdate, '%Y-%m-%d').date()
        except ValueError:
            return []
        result = []
        for row in self._scan_device(device_id, 2, limit=500):  # 2=FORWARD 旧→新
            lorastr = str(row.get('lorastr', '') or '')
            if wx_codec.lorastr_type(lorastr) != 1:
                continue
            coord = wx_codec.lorastr_coord(lorastr)
            if not coord:
                continue
            ts = wx_codec.parse_time_to_ts(row.get('time', ''))
            if not ts or datetime.fromtimestamp(ts).date() != target_date:
                continue
            result.append({
                'deviceId': device_id,
                'gps': f"{coord[0]},{coord[1]}",
                'time': row.get('time', ''),
                'ts': ts,
                'lorastr': lorastr,
            })
            if len(result) >= limit:
                break
        return result

    def getlastlog(self, page=1, limit=10, log_type=None):
        """云端记录（全部设备最新日志，最新在前）"""
        from crowui import wx_codec
        fetch = min(page * limit, 500)
        rows = self._scan_all(DEVICE_LOG_TABLE, ['deviceId', 'auto_id'], limit=fetch)
        rows.sort(key=lambda r: wx_codec.parse_time_to_ts(r.get('time', '')), reverse=True)
        if log_type:
            rows = [r for r in rows if wx_codec.lorastr_type(r.get('lorastr', '')) == log_type]
        return rows[(page - 1) * limit: page * limit]

    def getDeviceBestRssibyId(self, device_id, limit=10):
        """找 RSSI 最优的中继（最近日志的 upDateDevice）→ [{upDateDevice,rssi,time,deviceId}]"""
        best = {}
        for row in self._scan_device(device_id, 1, limit=100):  # 最新在前
            relay = str(row.get('upDateDevice', '') or '').strip()
            if not relay or relay == 'wechat':
                continue
            rssi = _to_int(row.get('rssi', 0))
            if relay not in best or rssi > best[relay]['rssi']:
                best[relay] = {
                    'upDateDevice': relay, 'rssi': rssi,
                    'time': str(row.get('time', '') or ''), 'deviceId': device_id,
                }
        ranked = sorted(best.values(), key=lambda x: x['rssi'], reverse=True)
        return ranked[:limit]

    def getDeviceConfigById(self, device_id):
        """单条配置 → dict（含 lorastr）"""
        consumed, row, _ = self._client().get_row(DEVICE_CONFIG_TABLE, [('deviceId', device_id)],
                                                  columns_to_get=None)
        return _row_to_dict(row) or {}

    def insertlog(self, device_id, lorastr, up_date_device='wechat', rssi='0', snr='0'):
        """插入一条日志（主键 deviceId+auto_id 自增）"""
        from tablestore import Row, PrimaryKeyValue, Condition, RowExistenceExpectation
        pk = [('deviceId', device_id), ('auto_id', PrimaryKeyValue.AUTO_INCR)]
        attrs = [
            ('deviceId', device_id),
            ('lorastr', lorastr),
            ('upDateDevice', up_date_device),
            ('time', time.strftime('%Y/%m/%d %H:%M:%S')),
            ('rssi', _to_int(rssi)),
            ('snr', _to_int(snr)),
        ]
        self._client().put_row(DEVICE_LOG_TABLE, Row(pk, attrs),
                               Condition(RowExistenceExpectation.IGNORE))
        return {'status': 'success'}

    def addDevice(self, device_id):
        """新增设备（不存在则建，存在则不动）"""
        from tablestore import Row, Condition, RowExistenceExpectation, UpdateType
        pk = [('deviceId', device_id)]
        attrs = [('visible', '1', UpdateType.PUT)]
        self._client().update_row(DEVICE_TABLE, Row(pk, attrs),
                                  Condition(RowExistenceExpectation.IGNORE))
        return {'status': 'success'}

    def updateDevice(self, device_id, rename='', product_key='', device_name='',
                     device_secret='', visible='1', picurl=''):
        """更新设备信息（UPDATE 合并写入，不覆盖未提供列）"""
        from tablestore import Row, Condition, RowExistenceExpectation, UpdateType
        pk = [('deviceId', device_id)]
        attrs = [
            ('rename', rename, UpdateType.PUT),
            ('ProductKey', product_key, UpdateType.PUT),
            ('DeviceName', device_name, UpdateType.PUT),
            ('DeviceSecret', device_secret, UpdateType.PUT),
            ('visible', _visible_str(visible), UpdateType.PUT),
            ('picurl', picurl, UpdateType.PUT),
        ]
        self._client().update_row(DEVICE_TABLE, Row(pk, attrs),
                                  Condition(RowExistenceExpectation.IGNORE))
        return {'status': 'success'}

    # ==================== 牛羊相关 ====================

    def getLivestockList(self):
        """→ [{cowsheep_id,rename,birthday,gender,avatar}]"""
        rows = []
        for row in self._scan_all(COWSHEEP_TABLE, ['cowsheep_id'], limit=500):
            if not self._mine(row):
                continue
            rows.append({
                'cowsheep_id': str(row.get('cowsheep_id', '')),
                'rename': str(row.get('rename', '') or ''),
                'birthday': str(row.get('birthday', '') or ''),
                'gender': row.get('gender', ''),
                'avatar': str(row.get('avatar', '') or ''),
            })
        return rows

    def addLivestock(self, cowsheep_id, birthday='', gender='0'):
        from tablestore import Row, Condition, RowExistenceExpectation, UpdateType
        pk = [('cowsheep_id', cowsheep_id)]
        attrs = [
            ('birthday', birthday, UpdateType.PUT),
            ('gender', gender, UpdateType.PUT),
            ('wechatid', self.wechatid, UpdateType.PUT),
        ]
        self._client().update_row(COWSHEEP_TABLE, Row(pk, attrs),
                                  Condition(RowExistenceExpectation.IGNORE))
        return {'status': 'success'}

    def updateLivestock(self, cowsheep_id, rename='', birthday='', gender='0', avatar=''):
        from tablestore import Row, Condition, RowExistenceExpectation, UpdateType
        pk = [('cowsheep_id', cowsheep_id)]
        attrs = [
            ('rename', rename, UpdateType.PUT),
            ('birthday', birthday, UpdateType.PUT),
            ('gender', gender, UpdateType.PUT),
            ('avatar', avatar, UpdateType.PUT),
        ]
        self._client().update_row(COWSHEEP_TABLE, Row(pk, attrs),
                                  Condition(RowExistenceExpectation.IGNORE))
        return {'status': 'success'}

    def bindDeviceCow(self, device_id, cowsheep_id):
        """绑定：写 devicetable001.link_cowsheep_id"""
        from tablestore import Row, Condition, RowExistenceExpectation, UpdateType
        pk = [('deviceId', device_id)]
        attrs = [('link_cowsheep_id', cowsheep_id, UpdateType.PUT)]
        self._client().update_row(DEVICE_TABLE, Row(pk, attrs),
                                  Condition(RowExistenceExpectation.IGNORE))
        return {'status': 'success'}

    def getcowsheepvideo(self, cowsheep_id):
        """媒体记录 → [{ossurl,time,video_id}]"""
        rows = []
        for row in self._scan_all(COWSHEEP_VIDEO_TABLE, ['ossurl'], limit=200):
            if str(row.get('cowsheep_id', '')) != str(cowsheep_id):
                continue
            rows.append({
                'ossurl': str(row.get('ossurl', '')),
                'time': str(row.get('time', '') or ''),
                'video_id': str(row.get('video_id', '') or ''),
            })
        return rows

    def deletecowsheepvideo(self, oss_urls):
        from tablestore import Row, Condition, RowExistenceExpectation
        for ossurl in oss_urls:
            self._client().delete_row(COWSHEEP_VIDEO_TABLE, Row([('ossurl', ossurl)]),
                                      Condition(RowExistenceExpectation.IGNORE))
        return {'status': 'success'}

    def uploadLivestockPhoto(self, cowsheep_id, oss_url):
        """上传媒体记录（文件本身需先直传 OSS）"""
        from tablestore import Row, Condition, RowExistenceExpectation, UpdateType
        pk = [('ossurl', oss_url)]
        attrs = [
            ('cowsheep_id', cowsheep_id, UpdateType.PUT),
            ('time', time.strftime('%Y-%m-%d %H:%M:%S'), UpdateType.PUT),
            ('video_id', '', UpdateType.PUT),
        ]
        self._client().update_row(COWSHEEP_VIDEO_TABLE, Row(pk, attrs),
                                  Condition(RowExistenceExpectation.IGNORE))
        return {'status': 'success'}

    # ==================== 道路/地名 ====================

    def getRoadListFromCache(self, force_refresh=False):
        """道路列表（内存永久缓存，对标小程序同名函数）"""
        if not force_refresh and self._road_cache is not None:
            return self._road_cache
        roads = []
        for row in self._scan_all(ROAD_TABLE, ['route_id'], limit=500):
            if not self._mine(row):
                continue
            roads.append({
                'route_id': str(row.get('route_id', '')),
                'roadname': str(row.get('roadname', '') or ''),
                'roadinfo': str(row.get('roadinfo', '') or ''),
                'level': row.get('level', 1),
            })
        self._road_cache = roads
        return roads

    def addRoad(self, roadname, roadinfo, level='1'):
        from tablestore import Row, Condition, RowExistenceExpectation, UpdateType
        route_id = 'rd_' + str(int(time.time() * 1000))
        pk = [('route_id', route_id)]
        attrs = [
            ('roadname', roadname, UpdateType.PUT),
            ('roadinfo', roadinfo, UpdateType.PUT),
            ('level', _to_int(level, 1), UpdateType.PUT),
            ('wechatid', self.wechatid, UpdateType.PUT),
        ]
        self._client().update_row(ROAD_TABLE, Row(pk, attrs),
                                  Condition(RowExistenceExpectation.IGNORE))
        self._road_cache = None
        return {'status': 'success', 'route_id': route_id}

    def updateRoad(self, route_id, roadname, roadinfo, level='1'):
        from tablestore import Row, Condition, RowExistenceExpectation, UpdateType
        pk = [('route_id', route_id)]
        attrs = [
            ('roadname', roadname, UpdateType.PUT),
            ('roadinfo', roadinfo, UpdateType.PUT),
            ('level', _to_int(level, 1), UpdateType.PUT),
        ]
        self._client().update_row(ROAD_TABLE, Row(pk, attrs),
                                  Condition(RowExistenceExpectation.IGNORE))
        self._road_cache = None
        return {'status': 'success'}

    def deleteRoad(self, route_id):
        from tablestore import Row, Condition, RowExistenceExpectation
        self._client().delete_row(ROAD_TABLE, Row([('route_id', route_id)]),
                                  Condition(RowExistenceExpectation.IGNORE))
        self._road_cache = None
        return {'status': 'success'}

    def getPlaceListFromCache(self, force_refresh=False):
        """地名列表（内存永久缓存，对标小程序同名函数）"""
        if not force_refresh and self._place_cache is not None:
            return self._place_cache
        places = []
        for row in self._scan_all(PLACE_TABLE, ['placeid'], limit=500):
            if not self._mine(row):
                continue
            places.append({
                'placeid': str(row.get('placeid', '')),
                'name': str(row.get('name', '') or ''),
                'gps': str(row.get('gps', '') or ''),
                'level': row.get('level', 1),
            })
        self._place_cache = places
        return places

    def addPlace(self, name, gps, level='1'):
        from tablestore import Row, Condition, RowExistenceExpectation, UpdateType
        place_id = 'pl_' + str(int(time.time() * 1000))
        pk = [('placeid', place_id)]
        attrs = [
            ('name', name, UpdateType.PUT),
            ('gps', gps, UpdateType.PUT),
            ('level', _to_int(level, 1), UpdateType.PUT),
            ('wechatid', self.wechatid, UpdateType.PUT),
        ]
        self._client().update_row(PLACE_TABLE, Row(pk, attrs),
                                  Condition(RowExistenceExpectation.IGNORE))
        self._place_cache = None
        return {'status': 'success', 'placeid': place_id}

    def updatePlace(self, place_id, name, gps, level='1'):
        from tablestore import Row, Condition, RowExistenceExpectation, UpdateType
        pk = [('placeid', place_id)]
        attrs = [
            ('name', name, UpdateType.PUT),
            ('gps', gps, UpdateType.PUT),
            ('level', _to_int(level, 1), UpdateType.PUT),
        ]
        self._client().update_row(PLACE_TABLE, Row(pk, attrs),
                                  Condition(RowExistenceExpectation.IGNORE))
        self._place_cache = None
        return {'status': 'success'}

    def deletePlace(self, place_id):
        from tablestore import Row, Condition, RowExistenceExpectation
        self._client().delete_row(PLACE_TABLE, Row([('placeid', place_id)]),
                                  Condition(RowExistenceExpectation.IGNORE))
        self._place_cache = None
        return {'status': 'success'}

    # ==================== DTU 指令下发（HTTP 云函数，OTS 无法替代） ====================

    def sendtodtucmd(self, device_name, product_key, cmd_dict):
        """
        下发 DTU 指令到阿里云 IoT。cmd_dict 例 {"cmd":"upgps","value":30,"deviceId":"v4-2"}
        自动注入 deviceId（若无）与 time 字段。
        """
        self.require_login()
        cmd = dict(cmd_dict)
        cmd.setdefault('time', time.strftime('%Y-%m-%d %H:%M:%S'))
        body = {
            'action': 'com',
            'deviceName': device_name,
            'productKey': product_key,
            'msg': json.dumps(cmd, ensure_ascii=False),
            'timestamp': int(time.time() * 1000),
            'info': {'wechatid': self.wechatid},
        }
        resp = requests.post(API_SEND_DTU_CMD_URL, json=body, timeout=SEND_CMD_TIMEOUT)
        resp.raise_for_status()
        return resp.json()
