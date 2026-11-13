# -*- coding: utf-8 -*-
"""云端日志(devicelog001) → 本机 SQLite 同步
- 首次全量：OTS get_range 整表直扫（无 Search 翻页 1 万条上限，20 万行约 2~4 分钟）
- 日常增量：HTTP 云函数 getlastlog（auto_id 降序翻页，碰到本机水位线即停，秒级）
- 本地库 res/local_cache/devicelog001.db，主键 (deviceId, auto_id) + INSERT OR IGNORE 去重，
  全量与增量可混用，重复数据自动跳过
"""

import json
import os
import sqlite3
import time

from crowui.wx_api import DEVICE_LOG_TABLE, _row_to_dict, _to_int

_FIXED_COLS = ('lorastr', 'rssi', 'snr', 'time', 'type', 'upDateDevice')
_PAGE_LIMIT = 100     # getlastlog 每页条数
_OTS_PAGE = 1000      # OTS get_range 每页条数

SYNC_DONE_CALLBACKS = []   # 同步完成回调（工作线程中调用，UI 方自行派发主线程）


class DeviceLogSyncer:
    """devicelog001 本机缓存：同步 + 本地查询"""

    def __init__(self, api):
        self.api = api
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.db_path = os.path.join(root, 'res', 'local_cache', 'devicelog001.db')
        os.makedirs(os.path.dirname(self.db_path), exist_ok=True)
        self._init_db()

    # ---------- 本地库 ----------

    def _connect(self):
        conn = sqlite3.connect(self.db_path)
        conn.execute('PRAGMA journal_mode=WAL')
        return conn

    def _init_db(self):
        with self._connect() as conn:
            conn.execute('''CREATE TABLE IF NOT EXISTS devicelog (
                deviceId     TEXT NOT NULL,
                auto_id      INTEGER NOT NULL,
                lorastr      TEXT, rssi INTEGER, snr INTEGER,
                time         TEXT, type INTEGER, upDateDevice TEXT,
                extra        TEXT,
                PRIMARY KEY (deviceId, auto_id))''')
            conn.execute('''CREATE TABLE IF NOT EXISTS meta (
                key TEXT PRIMARY KEY, value TEXT)''')

    def stats(self):
        """本地行数 / 设备数 / 最近一次同步时间"""
        with self._connect() as conn:
            total = conn.execute('SELECT COUNT(*) FROM devicelog').fetchone()[0]
            devices = conn.execute('SELECT COUNT(DISTINCT deviceId) FROM devicelog'
                                   ).fetchone()[0]
            last = conn.execute("SELECT value FROM meta WHERE key='last_sync'"
                                ).fetchone()
        return {'total': total, 'devices': devices,
                'last_sync': last[0] if last else ''}

    def query_device(self, device_id, limit=100, log_type=None, backward=True):
        """本地查某设备日志（新→旧），替代 getDeviceLogbyId 走云端"""
        sql = 'SELECT * FROM devicelog WHERE deviceId=?'
        args = [device_id]
        if log_type is not None:
            sql += ' AND type=?'
            args.append(_to_int(log_type, -1))
        sql += ' ORDER BY auto_id %s LIMIT ?' % ('DESC' if backward else 'ASC')
        args.append(limit)
        with self._connect() as conn:
            conn.row_factory = sqlite3.Row
            return [dict(r) for r in conn.execute(sql, args)]

    # ---------- 落库 ----------

    def _upsert_rows(self, conn, rows):
        """行 dict → 插入本地（OTS 行或 HTTP 扁平行均可），返回实际新增条数"""
        payloads = []
        for d in rows:
            if not d or 'deviceId' not in d or 'auto_id' not in d:
                continue
            extra = {k: v for k, v in d.items()
                     if k not in ('deviceId', 'auto_id') and k not in _FIXED_COLS}
            payloads.append((
                str(d['deviceId']), _to_int(d['auto_id'], 0),
                d.get('lorastr'), _to_int(d.get('rssi'), 0),
                _to_int(d.get('snr'), 0), str(d.get('time') or ''),
                _to_int(d.get('type'), 0), str(d.get('upDateDevice') or ''),
                json.dumps(extra, ensure_ascii=False) if extra else None))
        if not payloads:
            return 0
        before = conn.total_changes
        conn.executemany(
            'INSERT OR IGNORE INTO devicelog '
            '(deviceId,auto_id,lorastr,rssi,snr,time,type,upDateDevice,extra) '
            'VALUES (?,?,?,?,?,?,?,?,?)', payloads)
        return conn.total_changes - before

    # ---------- 分析查询 ----------

    def gps_devices(self):
        """有定位/跟踪记录的设备 → [(deviceId, 点数)] 按点数降序"""
        with self._connect() as conn:
            return conn.execute(
                'SELECT deviceId, COUNT(*) FROM devicelog '
                'WHERE type IN (1,5) GROUP BY deviceId ORDER BY 2 DESC'
            ).fetchall()

    def device_gps_points(self, device_id, days=None, max_points=4000):
        """某设备定位/跟踪坐标（时间正序，已抽稀到 max_points 内）
        → [(lat, lng, time_str)]；days=N 只取最近 N 天
        坐标过滤：丢弃测试垃圾值（限中国范围）"""
        with self._connect() as conn:
            rows = conn.execute(
                'SELECT lorastr, time FROM devicelog '
                'WHERE deviceId=? AND type IN (1,5) '
                'ORDER BY auto_id DESC LIMIT 20000',
                (device_id,)).fetchall()
        from crowui import wx_codec
        pts = []
        min_ts = time.time() - days * 86400 if days else None
        for lorastr, t in rows:
            segs = str(lorastr).split('|')
            if len(segs) < 3 or not segs[2]:
                continue
            parts = segs[2].replace('，', ',').split(',')
            if len(parts) < 2:
                continue
            try:
                lat, lng = float(parts[0]), float(parts[1])
            except ValueError:
                continue
            if not (3.0 <= lat <= 54.0 and 73.0 <= lng <= 136.0):
                continue     # 中国范围外的视为无效/测试数据
            if min_ts is not None:
                try:
                    ts = wx_codec.parse_time_to_ts(str(t or ''))
                except (ValueError, OSError):
                    ts = None
                if ts is None or ts < min_ts:
                    continue
            pts.append((lat, lng, str(t or '')))
        pts.reverse()     # 时间正序
        if len(pts) > max_points:      # 均匀抽稀
            step = -(-len(pts) // max_points)
            pts = pts[::step]
        return pts

    # ---------- 数据分析聚合 ----------

    # type 含义（据数据实测与小程序端）
    TYPE_NAMES = {2: '心跳/电量', 1: '定位', 5: '跟踪', 8: '设备参数',
                  6: '配置上报', 9: '模式配置', 4: '杂讯'}
    _ONLINE_SEC = 2 * 3600      # 2h 内有心跳=在线
    _LAG_SEC = 24 * 3600        # 24h 内=迟滞，更久=失联

    def analyze(self, days=None):
        """全库一次扫描聚合：设备健康、数据构成、每日趋势。
        days=N 时只统计最近 N 天（过老数据不参与分析）。
        返回 {'types', 'daily', 'devices', 'summary', 'days'}"""
        import datetime
        from crowui import wx_codec
        now = time.time()
        today = datetime.date.today()
        min_ts = now - days * 86400 if days else None
        trend_days = min(14, days) if days else 14
        with self._connect() as conn:
            rows_all = conn.execute(
                'SELECT deviceId, type, lorastr, time FROM devicelog '
                'ORDER BY auto_id ASC').fetchall()
            daily = []
            for i in range(trend_days - 1, -1, -1):
                d = today - datetime.timedelta(days=i)
                c = conn.execute(
                    'SELECT COUNT(*) FROM devicelog WHERE time LIKE ?',
                    (f'{d.year}/{d.month}/{d.day} %',)).fetchone()[0]
                daily.append((f'{d.month}/{d.day}', c))

        type_counts = {}
        dev = {}
        for did, typ, lorastr, tm in rows_all:
            try:
                ts = wx_codec.parse_time_to_ts(str(tm or ''))
            except (ValueError, OSError):
                ts = None
            valid_ts = ts is not None and 1577836800 < ts <= now + 86400
            if min_ts is not None and (not valid_ts or ts < min_ts):
                continue     # 时间窗过滤：过老/无时间的数据不参与分析
            type_counts[typ] = type_counts.get(typ, 0) + 1
            d = dev.setdefault(did, {'count': 0, 'last_ts': None,
                                     'battery': None, 'gps_ok': 0,
                                     'gps_total': 0, 'tmap': {}})
            d['count'] += 1
            d['tmap'][typ] = d['tmap'].get(typ, 0) + 1
            if valid_ts and (d['last_ts'] is None or ts > d['last_ts']):
                d['last_ts'] = ts
            segs = str(lorastr).split('|')
            if typ == 2 and len(segs) > 3:
                try:
                    bv = int(segs[3])
                except ValueError:
                    bv = None
                if bv is not None and 0 <= bv <= 100:
                    d['battery'] = bv          # 最新心跳的电量
            elif typ in (1, 5):
                d['gps_total'] += 1
                ok = False
                if len(segs) >= 3 and segs[2]:
                    p = segs[2].replace('，', ',').split(',')
                    if len(p) >= 2:
                        try:
                            la, ln = float(p[0]), float(p[1])
                            ok = 3.0 <= la <= 54.0 and 73.0 <= ln <= 136.0
                        except ValueError:
                            pass
                if ok:
                    d['gps_ok'] += 1

        devices = []
        for did, v in dev.items():
            dd = dict(v)
            dd['id'] = did
            devices.append(dd)
        devices.sort(key=lambda x: -(x['last_ts'] or 0))

        summary = {
            'online': sum(1 for d in devices if d['last_ts'] and
                          now - d['last_ts'] <= self._ONLINE_SEC),
            'lag': sum(1 for d in devices if d['last_ts'] and
                       self._ONLINE_SEC < now - d['last_ts'] <= self._LAG_SEC),
            'offline': sum(1 for d in devices if not d['last_ts'] or
                           now - d['last_ts'] > self._LAG_SEC),
            'lowbat': sum(1 for d in devices if d['battery'] is not None and
                          d['battery'] < 20),
            'gps_total': sum(d['gps_total'] for d in devices),
            'gps_ok': sum(d['gps_ok'] for d in devices),
        }
        return {'types': type_counts, 'daily': daily,
                'devices': devices, 'summary': summary, 'days': days}

    def battery_devices(self):
        """有心跳电量(type=2)记录的设备 → [(deviceId, 次数)] 按次数降序"""
        with self._connect() as conn:
            return conn.execute(
                'SELECT deviceId, COUNT(*) FROM devicelog WHERE type=2 '
                'GROUP BY deviceId ORDER BY 2 DESC').fetchall()

    def device_battery(self, device_id, days=None):
        """某设备心跳电量序列（type=2，时间正序）
        → [(time_str, ts, battery%)]；已剔除解析失败/0-100 外的值"""
        with self._connect() as conn:
            rows = conn.execute(
                'SELECT lorastr, time FROM devicelog '
                'WHERE deviceId=? AND type=2 ORDER BY auto_id DESC LIMIT 30000',
                (device_id,)).fetchall()
        from crowui import wx_codec
        min_ts = time.time() - days * 86400 if days else None
        out = []
        for lorastr, tm in rows:
            segs = str(lorastr).split('|')
            if len(segs) <= 3:
                continue
            try:
                bv = int(segs[3])
            except ValueError:
                continue
            if not 0 <= bv <= 100:
                continue
            try:
                ts = wx_codec.parse_time_to_ts(str(tm or ''))
            except Exception:      # 脏时间格式一律剔除，不外抛
                ts = None
            if ts is None or not 1577836800 < ts <= time.time() + 86400:
                continue
            if min_ts is not None and ts < min_ts:
                continue
            out.append((str(tm), ts, bv))
        out.reverse()
        return out

    def device_status(self, d, now=None):
        """单个设备聚合结果 → (状态文本, 是否在线, 是否低电量)"""
        now = now or time.time()
        if not d['last_ts']:
            return ('无上报', False, d['battery'] is not None and d['battery'] < 20)
        gap = now - d['last_ts']
        if gap <= self._ONLINE_SEC:
            st = '在线'
        elif gap <= self._LAG_SEC:
            st = '迟滞'
        else:
            st = '失联'
        return (st, st == '在线',
                d['battery'] is not None and d['battery'] < 20)

    # ---------- 同步入口 ----------

    def sync(self, progress=None, full=False):
        """同步（阻塞，放工作线程跑）。
        full=True 或本地为空 → OTS 整表直扫全量；否则 getlastlog 增量。
        progress(info): info={'mode','cur','new','reached','page'}
        """
        t0 = time.time()
        with self._connect() as conn:
            empty = conn.execute('SELECT 1 FROM devicelog LIMIT 1'
                                 ).fetchone() is None
            if full or empty:
                result = self._sync_full_ots(conn, progress)
            else:
                result = self._sync_http(conn, progress)
            conn.execute("INSERT OR REPLACE INTO meta VALUES ('last_sync', ?)",
                         (time.strftime('%Y-%m-%d %H:%M:%S'),))
        result['total'] = self.stats()['total']
        result['elapsed'] = round(time.time() - t0, 1)
        for cb in list(SYNC_DONE_CALLBACKS):     # 通知分析页自动刷新
            try:
                cb(result)
            except Exception:
                pass
        return result

    # ---------- 全量：OTS 整表直扫 ----------

    def _sync_full_ots(self, conn, progress):
        from tablestore import INF_MIN, INF_MAX, Direction
        client = self.api._client()
        start = [('deviceId', INF_MIN), ('auto_id', INF_MIN)]
        end = [('deviceId', INF_MAX), ('auto_id', INF_MAX)]
        cur = new = 0
        while True:
            consumed, next_pk, rows, _ = client.get_range(
                DEVICE_LOG_TABLE, Direction.FORWARD, start, end,
                limit=_OTS_PAGE)
            cur += len(rows)
            new += self._upsert_rows(conn, [_row_to_dict(r) for r in rows])
            conn.commit()
            if progress:
                progress({'mode': 'full', 'cur': cur, 'new': new,
                          'reached': '', 'page': 0})
            if next_pk is None:
                break
            start = list(next_pk)
        devices = conn.execute('SELECT COUNT(DISTINCT deviceId) FROM devicelog'
                               ).fetchone()[0]
        return {'mode': 'full', 'new': new, 'scanned': cur,
                'reached': '', 'capped': False, 'devices': devices}

    # ---------- 增量：getlastlog 全局水位线单流翻页 ----------

    def _sync_http(self, conn, progress):
        """全局单流增量：auto_id 全表递增，服务器前缀传空串 → 全局 auto_id
        降序流。水位线 = 本机全局 MAX(auto_id)，从最新往回翻，
        页内最旧 <= 水位线即停。0 新增时仅 1 个请求。"""
        mark = conn.execute('SELECT MAX(auto_id) FROM devicelog'
                            ).fetchone()[0] or 0
        new = scanned = pages = 0
        reached = ''
        capped = False
        page0 = 0
        while True:
            try:
                rows = self.api.getlastlog_page(page0, _PAGE_LIMIT, prefix='')
            except Exception:
                if page0 == 0:
                    raise
                capped = True   # 翻页中途报错（多为 offset 超服务器上限）
                break
            if not rows:
                break
            scanned += len(rows)
            new += self._upsert_rows(conn, rows)
            conn.commit()
            pages += 1
            reached = str(rows[-1].get('time') or reached)   # 页内最旧一条
            if progress:
                progress({'mode': 'http', 'cur': scanned, 'new': new,
                          'reached': reached, 'page': pages, 'prefix': ''})
            oldest_id = min(_to_int(r.get('auto_id'), 0) for r in rows)
            if len(rows) < _PAGE_LIMIT or oldest_id <= mark:
                break   # 翻完 / 已盖到本机水位线
            page0 += 1
        return {'mode': 'http', 'new': new, 'scanned': scanned,
                'reached': reached, 'capped': capped, 'devices': 0}
