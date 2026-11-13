# -*- coding: utf-8 -*-
"""
DTU 协议编解码模块（移植自小程序 utils/time-window-codec.js + data-cache.js）
- 时间窗两位 base62 代号编解码（与服务器一致）
- 设备配置 lorastr 解析
- upgps 上报间隔计算（工作周期/大周期）
- 道路点差分编码/解码（与小程序 coord-transform.js encodeRoadPoints/parseRoadPoints 一致）
"""

import re
from datetime import datetime

# ==================== 时间窗两位 base62 代号 ====================

TIME_DICT = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'


def time_window_to_index(start, end):
    """start(0-23), end(0-24) → index；非法返回 -1。end>=24 自动转 23（代表23:59）"""
    real_end = 23 if end >= 24 else end
    if start >= real_end:
        return -1
    pre_count = 0
    for s in range(start):
        valid = 23 - (s + 1) + 1
        if valid > 0:
            pre_count += valid
    return pre_count + (real_end - (start + 1))


def index_to_two_char(idx):
    """index → 两位字符（base62）；非法返回 'XX'"""
    if idx < 0 or idx > 275:
        return 'XX'
    return TIME_DICT[idx // 62] + TIME_DICT[idx % 62]


def two_char_to_index(s):
    """两位字符 → index；非法返回 -1"""
    if not s or len(s) != 2:
        return -1
    h = TIME_DICT.find(s[0])
    l = TIME_DICT.find(s[1])
    if h < 0 or l < 0:
        return -1
    return h * 62 + l


def index_to_time_window(idx):
    """index → {start, end}（end=23 业务上代表 23:59）；非法返回 None"""
    if idx < 0:
        return None
    total = 0
    for s in range(24):
        valid = 23 - (s + 1) + 1
        if valid <= 0:
            continue
        if idx < total + valid:
            return {'start': s, 'end': s + 1 + (idx - total)}
        total += valid
    return None


def encode_time_window(start, end):
    """start(0-23), end(0-24) → 两位代号；非法返回 None"""
    idx = time_window_to_index(start, end)
    if idx < 0:
        return None
    return index_to_two_char(idx)


def decode_time_window(code):
    """两位代号 → {start, end}；非法返回 None"""
    if not code or len(code) != 2:
        return None
    idx = two_char_to_index(code)
    if idx < 0:
        return None
    return index_to_time_window(idx)


def parse_time_window(raw):
    """兼容解析：两位代号 或 旧格式 "start-duration"（如 8-6）→ {start, end}；无法解析返回 None"""
    if not raw:
        return None
    s = str(raw).strip()
    win = decode_time_window(s)
    if win:
        return win
    m = re.match(r'^(\d{1,2})-(\d{1,2})$', s)
    if m:
        start = int(m.group(1))
        end = start + int(m.group(2))
        if start < end and end <= 24:
            return {'start': start, 'end': 23 if end == 24 else end}
    return None


def format_time_range(raw):
    """代号或旧格式 → "HH:00-HH:00"（end=23 显示 23:59）；无法解析返回 '-'"""
    win = parse_time_window(raw)
    if not win:
        return '-'
    end_text = '23:59' if win['end'] == 23 else '%02d:00' % win['end']
    return '%02d:00-%s' % (win['start'], end_text)


# ==================== 设备配置 lorastr 解析 ====================
# 配置 lorastr 第3段(按|分)再按,分：
#   [0]上报周期(分钟) [1]开机时间代号 [2]GPS工作时间代号 [,3]主周期参数(1-10 = 10-100分钟)
# 例 "30,0M,3t,6"：工作时间内按 30 分钟，不在工作时间按 6*10=60 分钟

def parse_device_config_lorastr(config_lorastr):
    """解析配置 lorastr → {reportIntervalMin, mainPeriodMin, powerWin, gpsWin}"""
    res = {'reportIntervalMin': 0, 'mainPeriodMin': 0, 'powerWin': None, 'gpsWin': None}
    if not config_lorastr:
        return res
    parts = str(config_lorastr).split('|')
    if len(parts) < 3 or not parts[2]:
        return res
    cfg = parts[2].split(',')
    if not cfg:
        return res
    try:
        interval = int((cfg[0] or '').strip())
        if interval > 0:
            res['reportIntervalMin'] = interval
    except ValueError:
        pass
    if len(cfg) >= 2:
        win = parse_time_window((cfg[1] or '').strip())
        if win:
            res['powerWin'] = win
    if len(cfg) >= 3:
        win = parse_time_window((cfg[2] or '').strip())
        if win:
            res['gpsWin'] = win
    if len(cfg) >= 4:
        try:
            mp = int((cfg[3] or '').strip())
            if 1 <= mp <= 10:
                res['mainPeriodMin'] = mp * 10
        except ValueError:
            pass
    return res


def calc_upgps_value(config_lorastr, now=None):
    """
    当前应生效的 GPS 上报间隔（分钟），供 {"cmd":"upgps","value":X} 使用
    工作周期（开机时间窗内）→ 上报周期；大周期（窗外来）→ 主周期；无配置 → 0
    """
    cfg = parse_device_config_lorastr(config_lorastr)
    if not cfg['reportIntervalMin'] and not cfg['mainPeriodMin']:
        return 0
    d = now or datetime.now()
    current_minutes = d.hour * 60 + d.minute
    if cfg['powerWin']:
        start_minutes = cfg['powerWin']['start'] * 60
        end = cfg['powerWin']['end']
        end_minutes = 23 * 60 + 59 if end == 23 else end * 60
        in_work = start_minutes <= current_minutes < end_minutes
        if not in_work and cfg['mainPeriodMin'] > 0:
            return cfg['mainPeriodMin']
    return cfg['reportIntervalMin'] or 0


# ==================== 道路点差分编码/解码 ====================
# 编码格式："lat0,lon0,dLat1,dLon1,dLat2,dLon2,..."
# 所有值为整数（微度，坐标×100000），首点绝对值，后续为相对前一点的偏移

def parse_road_points(roadinfo):
    """解码道路差分串 → [(lat, lng), ...]；解析失败返回 []"""
    if not roadinfo:
        return []
    try:
        arr = [int(x.strip()) for x in str(roadinfo).split(',')]
    except ValueError:
        return []
    if len(arr) < 2:
        return []
    cur_lat = arr[0] / 100000.0
    cur_lng = arr[1] / 100000.0
    coords = [(cur_lat, cur_lng)]
    for i in range(2, len(arr) - 1, 2):
        cur_lat += arr[i] / 100000.0
        cur_lng += arr[i + 1] / 100000.0
        coords.append((round(cur_lat, 5), round(cur_lng, 5)))
    return coords


def encode_road_points(coords):
    """[(lat, lng), ...] → 差分编码字符串；少于2个点返回 ''"""
    if not coords or len(coords) < 2:
        return ''
    first_lat, first_lng = coords[0]
    parts = [str(int(round(first_lat * 100000))), str(int(round(first_lng * 100000)))]
    prev_lat, prev_lng = first_lat, first_lng
    for lat, lng in coords[1:]:
        parts.append(str(int(round((lat - prev_lat) * 100000))))
        parts.append(str(int(round((lng - prev_lng) * 100000))))
        prev_lat, prev_lng = lat, lng
    return ','.join(parts)


# ==================== lorastr 通用解析 ====================
# lorastr 格式：type|deviceId|data|...，type：1=定位(第3段 lat,lng) 2=对时(第3段时间戳,第4段电量)
# 5=跟踪 6=设置(第3段配置串)

def split_lorastr(lorastr):
    """按 | 或 ｜ 分段"""
    if not lorastr:
        return []
    return [s for s in re.split(r'[｜|]', str(lorastr))]


def lorastr_type(lorastr):
    """返回记录类型 int（解析失败返回 0）"""
    segs = split_lorastr(lorastr)
    if not segs:
        return 0
    try:
        return int(segs[0].strip())
    except ValueError:
        return 0


def lorastr_coord(lorastr):
    """定位记录 → (lat, lng)；非法返回 None"""
    segs = split_lorastr(lorastr)
    coord_seg = segs[2] if len(segs) >= 3 else str(lorastr or '')
    parts = [p for p in re.split(r'[,，]\s*', coord_seg) if p != '']
    if len(parts) < 2:
        return None
    try:
        lat = float(parts[0])
        lng = float(parts[1])
    except ValueError:
        return None
    if not (-90 <= lat <= 90) or not (-180 <= lng <= 180) or (lat == 0 and lng == 0):
        return None
    return lat, lng


def lorastr_battery(lorastr):
    """对时记录(type=2) 第4段电量 → 百分比 0-100；解析失败返回 None"""
    if lorastr_type(lorastr) != 2:
        return None
    segs = split_lorastr(lorastr)
    if len(segs) < 4:
        return None
    try:
        v = float(segs[3].strip())
    except ValueError:
        return None
    if v <= 1:  # 归一化 0~1
        v *= 100
    return max(0, min(100, v))


def parse_time_to_ts(raw_time):
    """
    时间字符串 → 秒级时间戳
    兼容 "2026/8/10 23:13:33" / "2026-8-10 23:13:33" / 10位或13位Unix
    解析失败返回 0
    """
    if not raw_time:
        return 0
    s = str(raw_time).replace('-', '/').strip()
    for fmt in ('%Y/%m/%d %H:%M:%S', '%Y/%m/%d %H:%M', '%Y/%m/%d'):
        try:
            return int(datetime.strptime(s, fmt).timestamp())
        except ValueError:
            continue
    try:
        n = int(str(raw_time).strip())
        if n > 0:
            return n // 1000 if n > 1e11 else n  # 13位毫秒转秒
    except ValueError:
        pass
    return 0
