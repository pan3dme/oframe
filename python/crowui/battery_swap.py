# -*- coding: utf-8 -*-
"""
换电时间分析（对标小程序 config/battery-swap.js，逻辑一致）
- 只有对时记录（lorastr type=2：2|device|unix秒|电量|...）含电量，电量在 parts[3]
- 每台设备持久缓存"上次换电"记录（电量最高点+时间）与当前最低点
- 换电判定：新记录电量回到满电水平（>=原最高电量），
  且中间最低点比原最高电量低 >=20 个百分点 → 视为换电/重新上电
- 无缓存时取扫描窗口内电量最高的对时记录作为基准（新电池上电电量最高）
"""

import json
import os
import threading

PAGE_SIZE = 100          # 每页拉取记录数
MAX_PAGES_FIRST = 15     # 无缓存时首次扫描最多页数（1500条）
MAX_PAGES_UPDATE = 2     # 已有缓存后只扫描最近几页
MIN_VALID_BATTERY = 0.05 # 忽略过低无效读数
SWAP_MAX_LEVEL = 0.85    # 电量>=85% 视为满电级别（疑似错过换电时加深扫描）
SWAP_DROP_DELTA = 0.20   # 最低点比原最高电量低 >=20个百分点才判定换电
SWAP_GAP_MS = 24 * 3600 * 1000
# 2025-01-01 UTC 前的记录视为历史脏数据
MIN_VALID_SWAP_TIME = 1420070400000

_CACHE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           'battery_swap_cache.json')
_cache_lock = threading.Lock()


# ==================== 永久缓存 ====================

def _load_cache():
    try:
        with open(_CACHE_PATH, 'r', encoding='utf-8') as f:
            data = json.load(f)
        if isinstance(data, dict) and 'devices' in data:
            return data
    except (OSError, ValueError):
        pass
    return {'version': 5, 'devices': {}}


def _save_cache(cache):
    with _cache_lock:
        cache['updatedAt'] = int(_now_ms())
        try:
            with open(_CACHE_PATH, 'w', encoding='utf-8') as f:
                json.dump(cache, f, ensure_ascii=False)
        except OSError:
            pass


def _now_ms():
    import time
    return time.time() * 1000


def get_cached_swap(device_id):
    """同步读缓存（界面可先展示）→ {time, timeStr, battery} 或 None"""
    dev = _load_cache()['devices'].get(device_id)
    if dev and dev.get('highest'):
        return dev['highest']
    return None


def clear_swap_cache():
    try:
        os.remove(_CACHE_PATH)
    except OSError:
        pass


# ==================== 数据解析 ====================

def _parse_battery_value(v):
    """0~1 小数或 0~100 百分比 → 0~1；无效返回 None"""
    if v is None or v == '':
        return None
    try:
        n = float(v)
    except (TypeError, ValueError):
        return None
    if n <= 0:
        return None
    return n / 100 if n > 1 else n


def extract_battery_samples(rows, device_id):
    """从日志行提取该设备电量样本 [{t(毫秒), b(0~1)}]，只认 type=2 对时记录"""
    samples = []
    for row in rows:
        lorastr = str(row.get('lorastr', '') or '')
        parts = lorastr.split('|')
        if len(parts) < 4 or parts[0] != '2':
            continue
        # type=2 记录的真实设备ID在 lorastr parts[1]（中继转发场景外层是中继）
        actual = parts[1].strip() if len(parts) >= 2 else ''
        if device_id and actual and actual != device_id:
            continue
        # 设备真实时间在 parts[2]（Unix 秒/毫秒）
        t = 0
        try:
            ts = int(parts[2].strip())
            if ts > 0:
                t = ts if ts >= 1e10 else ts * 1000
        except ValueError:
            t = 0
        if not t:
            from crowui import wx_codec
            t = wx_codec.parse_time_to_ts(row.get('time', ''))
            if isinstance(t, (int, float)):
                t = t * 1000 if 0 < t < 1e10 else t
            else:
                t = 0
        if t <= 0:
            continue
        b = _parse_battery_value(parts[3])
        if b is None:
            b = _parse_battery_value(row.get('battery'))
        if b is None:
            continue
        samples.append({'t': t, 'b': b})
    return samples


# ==================== 换电分析 ====================

def analyze_samples(samples, cached_highest):
    """
    按时间升序跟踪"上次换电"（电量最高点）与当前最低点。
    cached_highest: 缓存 {time, timeStr, battery} 或 None
    返回 {'highest': {time,timeStr,battery}|None, 'lowest': {...}|None}
    """
    asc = [s for s in samples
           if s['b'] >= MIN_VALID_BATTERY and s['t'] >= MIN_VALID_SWAP_TIME]
    asc.sort(key=lambda s: s['t'])
    if not asc:
        return {'highest': cached_highest, 'lowest': None}

    highest = None
    if cached_highest:
        highest = {'t': cached_highest.get('time', 0),
                   'b': cached_highest.get('battery', 0)}
    if not highest or not highest['t']:
        highest = asc[0]
        for s in asc[1:]:
            if s['b'] > highest['b'] or (s['b'] == highest['b'] and s['t'] < highest['t']):
                highest = s
    lowest = None

    for s in asc:
        if s['t'] <= highest['t']:
            continue
        # 换电判定：电量回到满电水平且中间确实用掉一大截
        if s['b'] >= highest['b'] and lowest and (highest['b'] - lowest['b']) >= SWAP_DROP_DELTA:
            highest = s
            lowest = None
            continue
        if s['b'] < highest['b']:
            if not lowest or s['b'] < lowest['b']:
                lowest = s

    if not lowest:
        lowest = asc[-1]

    fmt = lambda s: {'time': s['t'], 'timeStr': '', 'battery': s['b']}
    return {'highest': fmt(highest), 'lowest': fmt(lowest)}


# ==================== 拉取与分析主流程 ====================

def _fetch_and_analyze(api, device_id, cached_highest):
    """工作线程内执行：顺序拉页 → 分析（含疑似错过换电的加深扫描）"""
    scan_pages = MAX_PAGES_UPDATE if cached_highest else MAX_PAGES_FIRST
    all_samples = []
    page = 0
    while page < scan_pages:
        rows = api.getDeviceLogbyId(device_id, limit=PAGE_SIZE, offset=page * PAGE_SIZE)
        all_samples.extend(extract_battery_samples(rows, device_id))
        no_more = len(rows) < PAGE_SIZE
        # 疑似错过换电：最新页最早样本接近满电且晚于缓存换电 → 加深扫描
        if page == 0 and cached_highest and not no_more and scan_pages < MAX_PAGES_FIRST:
            asc = sorted(all_samples, key=lambda s: s['t'])
            if asc and asc[0]['b'] >= SWAP_MAX_LEVEL \
                    and (asc[0]['t'] - cached_highest.get('time', 0)) >= SWAP_GAP_MS:
                scan_pages = MAX_PAGES_FIRST
        if no_more or page + 1 >= scan_pages:
            break
        page += 1
    return analyze_samples(all_samples, cached_highest)


def format_relative_time(ts_ms):
    """相对时间：刚刚 / x分钟前 / x小时前 / x天前"""
    if not ts_ms:
        return ''
    diff = _now_ms() - ts_ms
    if diff < 0:
        return ''
    minute, hour, day = 60 * 1000, 3600 * 1000, 86400 * 1000
    if diff < minute:
        return '刚刚'
    if diff < hour:
        return f"{int(diff // minute)}分钟前"
    if diff < day:
        return f"{int(diff // hour)}小时前"
    return f"{int(diff // day)}天前"


def get_last_swap(api, device_id, on_done):
    """获取设备最近换电时间（异步）。on_done(swap_rel:str) 回调相对时间字符串"""
    cached = get_cached_swap(device_id)

    def job():
        return _fetch_and_analyze(api, device_id, cached)

    def on_ok(summary):
        highest = summary.get('highest')
        # 缓存换电时间只前进不回退
        if cached and highest and highest.get('time', 0) < cached.get('time', 0):
            highest = cached
        cache = _load_cache()
        cache.setdefault('devices', {})[device_id] = {
            'highest': highest, 'lowest': summary.get('lowest')}
        _save_cache(cache)
        rel = format_relative_time(highest['time']) if highest else ''
        if on_done:
            on_done(rel)

    api.run_async(job, on_ok, lambda m: on_done(''))
