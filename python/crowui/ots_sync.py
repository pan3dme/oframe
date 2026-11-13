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
