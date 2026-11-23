# -*- coding: utf-8 -*-
"""分析技能：对时校验——检查某台设备最近 20 条对时(type=2)上报是否正确。

固件机制（STM32_ONLY_SEND main.c）：
- 对时帧 2|设备名|时间|电量|计数|；第3段固件发"当天秒数"（北京时间基准，
  getCurrentTimestamp 为北京时间戳），经中继(PAN3029_F030_MID)上云时
  dtu_replace_time_segment 把 <1e9 的当天秒补齐成完整 Unix 时间戳
  （北京时间基准，%86400 即北京当天秒）→ 本地库两种形态都存在；
- 时隙公式 getSlotOffsetSec：offset = (编号 % 组台数) * 周期秒 / 组台数；
- calcNextTxTime 把上报锚定到当天 00:00 起的周期网格 → 正常上报时刻的
  天内秒 % 周期 == offset（误差仅发送耗时几秒 + RTC 晶振补偿残差 ±15s）。

两个校验维度：
① 帧内时间是否正确：设备钟偏差 = 帧内时间 - 云端time当天秒（均换算成北京
   当天秒，跨午夜 wrap ±43200；正=设备快，负=设备慢）；
   |偏差|<=120s 正确，<=600s 轻微漂移（LSI 晶振未收敛量级），更大=疑似未对时
   （未对时时帧内是开机秒数/1970 基准时间戳）；多条完整时间戳完全相同=疑似
   RTC 冻结。
② 上报点是否落在指定时隙：天内秒 % 周期 vs offset，wrap ±周期/2；
   |偏差|<=90s 准点；距上一条 45s~周期×0.6 视为开机快速轮（fastStartRounds=3，
   周期被强制成 60s，脱离正常网格）不计入准点统计；距上条 <45s 多为中继
   重复接收（同一上报点收到两遍），按正常上报判定。

参数取本地库最新**可解析**记录：周期 = type=6 第3段首数；组台数 = type=8
readinfo 的 NT（15T=15台，缺省按固件默认 30）。纯本地查询。
"""

import re

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel,
                             QPushButton, QComboBox, QFrame, QTableWidget,
                             QTableWidgetItem, QHeaderView, QAbstractItemView)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor

from crowui import theme
from crowui.ots_sync import DeviceLogSyncer
from crowui.wx_codec import parse_time_to_ts

RECORDS = 20        # 最近 N 条对时记录（用户定版 20）
_CLOCK_OK = 120     # 时钟偏差容差（秒）
_CLOCK_WARN = 600   # 超过视为时钟错乱（疑似未对时）
_SLOT_TOL = 90      # 时隙偏差容差（秒）

_COLORS = {'ok': '#07c160', 'warn': '#E6A23C', 'bad': '#EF5350',
           'fast': '#E6A23C', 'dirty': '#999999'}


def _dev_sort_key(s):
    """设备名自然排序：v3-2 排在 v3-10 前"""
    return tuple((1, int(p)) if p.isdigit() else (0, p)
                 for p in re.split(r'(\d+)', str(s)))


def _sec_to_hms(sec):
    sec = int(sec) % 86400
    return '%02d:%02d:%02d' % (sec // 3600, sec % 3600 // 60, sec % 60)


def _dedupe_rows(rows, keep=RECORDS):
    """多中继会把同一条上报重复上云（同 lorastr，时间可能差几秒）。
    按帧内容去重，保留最新一次接收，取前 keep 条（新→旧）"""
    seen = set()
    uniq = []
    for r in rows or []:                      # query_device 结果本身新→旧
        key = str(r.get('lorastr') or '')
        if key in seen:
            continue
        seen.add(key)
        uniq.append(r)
        if len(uniq) >= keep:
            break
    return uniq


def _beijing_tod(raw_time, ts):
    """云端 time → 北京当天秒（与机器时区无关）。
    优先直接取字符串末尾的 HH:MM:SS（"2026/11/24 00:57:06"→3426）；
    纯 Unix 时间字符串按北京时间基准 +8h 换算。失败返回 None"""
    s = str(raw_time or '').replace('-', '/').strip()
    m = re.search(r'(\d{1,2}):(\d{2})(?::(\d{2}))?\s*$', s)
    if m:
        h, mi, sec = int(m.group(1)), int(m.group(2)), int(m.group(3) or 0)
        if h < 24 and mi < 60 and sec < 60:
            return h * 3600 + mi * 60 + sec
    if ts > 0:
        return (ts + 8 * 3600) % 86400
    return None


def analyze_timesync(rows, device_id, interval_min, groups):
    """对时记录校验（纯函数，便于单测）。

    rows: 本地库 type=2 记录（query_device 结果，新→旧，dict 含 lorastr/time）
    device_id: 设备ID（编号取 '-' 后的数字，如 v3-12 → 12）
    interval_min: 上报周期分钟（None=未知）；groups: 组台数（None=按默认 30）
    → {'recs': [旧→新逐条判定], 'summary': {...}}
    """
    m = re.search(r'(\d+)\s*$', str(device_id))
    dev_index = int(m.group(1)) if m else 0
    groups_known = bool(groups and groups > 0)
    cnt = groups if groups_known else 30
    interval_sec = int(round(interval_min * 60)) if interval_min else None
    offset = ((dev_index % cnt) * interval_sec // cnt) if interval_sec else None

    recs = []
    prev_ts = None
    for r in reversed(rows or []):            # 新→旧 反转成 旧→新，算距上条间隔
        lorastr = str(r.get('lorastr') or '')
        tm = str(r.get('time') or '')
        ts = parse_time_to_ts(tm)
        rec = {'lorastr': lorastr, 'time': tm, 'ts': ts,
               'frame_raw': None, 'frame_tod': None, 'clock_diff': None,
               'slot_diff': None, 'slot_offset': None,
               'clock_v': None, 'slot_v': None, 'gap': None}
        segs = lorastr.split('|')
        s = segs[2].strip() if len(segs) >= 3 else ''
        if ts > 0 and s.isdigit():
            v = int(s)
            if v >= 1000000000:               # 完整 Unix 时间戳（北京时间基准，
                rec['frame_raw'] = v          # 中继补齐或旧固件直发）
                rec['frame_tod'] = v % 86400
            elif v <= 86399:                  # 当天秒数（固件原始形态）
                rec['frame_raw'] = v
                rec['frame_tod'] = v
        if prev_ts is not None and ts > 0:
            rec['gap'] = ts - prev_ts
        if ts > 0:
            prev_ts = ts
        recs.append(rec)

    for rec in recs:
        if rec['frame_tod'] is None:
            continue                          # 脏数据/解析失败，其余列保持 '—'
        cloud_tod = _beijing_tod(rec['time'], rec['ts'])
        if cloud_tod is None:
            rec['frame_tod'] = None           # 云端时间也不可信 → 按脏数据处理
            continue
        d = ((rec['frame_tod'] - cloud_tod + 43200) % 86400) - 43200
        rec['clock_diff'] = d                 # 正=设备快，负=设备慢
        rec['clock_v'] = ('ok' if abs(d) <= _CLOCK_OK
                          else 'warn' if abs(d) <= _CLOCK_WARN else 'bad')
        if offset is None or rec['clock_v'] == 'bad':
            continue                          # 无周期配置 / 时钟已错，时隙无意义
        actual = cloud_tod % interval_sec
        sd = ((actual - offset + interval_sec // 2) % interval_sec) \
            - interval_sec // 2
        rec['slot_diff'] = sd
        rec['slot_offset'] = offset
        # 开机快速轮：间隔≈60s（fastStartRounds），落在 45s~周期*0.6 窗口；
        # <45s 视为中继重复接收，不算快速轮（上报点本身仍应准点）
        if rec['gap'] is not None and \
                45 <= rec['gap'] < interval_sec * 0.6:
            rec['slot_v'] = 'fast'            # 开机快速轮，脱离正常网格
        else:
            rec['slot_v'] = 'ok' if abs(sd) <= _SLOT_TOL else 'bad'

    clock = [r['clock_v'] for r in recs if r['clock_v']]
    diffs = [r['clock_diff'] for r in recs if r['clock_v'] in ('ok', 'warn')]
    slot = [r['slot_v'] for r in recs if r['slot_v']]
    bad_sd = [r['slot_diff'] for r in recs if r['slot_v'] == 'bad']
    n = len(recs)
    n_dirty = sum(1 for r in recs if r['frame_tod'] is None)
    summary = {
        'index': dev_index, 'offset': offset, 'interval_sec': interval_sec,
        'groups': cnt, 'groups_known': groups_known,
        'n': n, 'dirty': n_dirty,
        'clock_ok': clock.count('ok'), 'clock_warn': clock.count('warn'),
        'clock_bad': clock.count('bad'),
        'avg_clock': (round(sum(diffs) / len(diffs)) if diffs else None),
        'slot_ok': slot.count('ok'), 'slot_fast': slot.count('fast'),
        'slot_bad': slot.count('bad'),
        'hint': '',
    }
    hints = []
    bad_recs = [r for r in recs if r['clock_v'] == 'bad']
    if len(bad_recs) >= 3 and len({r['frame_raw'] for r in bad_recs}) == 1:
        hints.append(f"⚠ {len(bad_recs)} 条时钟错乱且帧内时间戳完全相同"
                     f"（{bad_recs[0]['frame_raw']}）：疑似设备 RTC 冻结/停走，"
                     "建议远程下发对时，无效则重启")
    if summary['clock_bad'] and not hints:
        hints.append(f"⚠ {summary['clock_bad']} 条时钟错乱：典型原因是设备未对时"
                     "（帧内=开机秒数/1970 基准时间戳）或时钟跳变，"
                     "建议远程下发对时/重启后观察")
    if len(bad_sd) >= 3 and max(bad_sd) - min(bad_sd) <= 20:
        avg_sd = round(sum(bad_sd) / len(bad_sd))
        hints.append(f"⚠ {len(bad_sd)} 条偏离时隙的幅度一致（≈{avg_sd:+d} 秒）："
                     "大概率是设备中途下发过新上报周期（旧记录按新周期算所致），"
                     "不是设备故障")
    if hints:
        summary['hint'] = '\n'.join(hints)
    return {'recs': recs, 'summary': summary}


class TimeSyncDialog(QDialog):
    """对时校验独立窗口：设备下拉 + 摘要 + 最近 20 条明细表"""

    def __init__(self, api, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[对时校验]', msg))
        self._syncer = DeviceLogSyncer(api)
        self.setWindowTitle('分析技能 · 对时校验')
        self.setMinimumSize(780, 560)
        self._setup_ui()
        self._load_devices()

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(18, 16, 18, 14)
        lay.setSpacing(10)

        title = QLabel('🕐 对时校验')
        title.setStyleSheet('font-size:15px; font-weight:600; color:#333;')
        lay.addWidget(title)
        tip = QLabel('取该设备最近 20 条对时(type=2)上报（多中继重复上云的同一条'
                     '已按帧内容去重）：① 帧内时间与云端接收时间比对（时钟是否'
                     '正确）；② 按固件时隙公式（编号/组台数×周期，锚定当天 00:00）'
                     '核对上报点是否准点。周期/组台数取本地最新配置记录，'
                     '纯本地查询。')
        tip.setStyleSheet('color:#888; font-size:11px;')
        tip.setWordWrap(True)
        lay.addWidget(tip)

        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        line.setStyleSheet('color:#eee;')
        lay.addWidget(line)

        row = QHBoxLayout()
        row.addWidget(QLabel('设备:'))
        self.device_combo = QComboBox()
        self.device_combo.currentIndexChanged.connect(self._analyze_async)
        row.addWidget(self.device_combo, stretch=1)
        refresh = QPushButton('刷新')
        refresh.setFixedSize(64, 28)
        refresh.setCursor(Qt.CursorShape.PointingHandCursor)
        refresh.setStyleSheet(theme.GREEN_BUTTON_QSS)
        refresh.clicked.connect(self._load_devices)
        row.addWidget(refresh)
        close = QPushButton('关闭')
        close.setFixedSize(64, 28)
        close.setCursor(Qt.CursorShape.PointingHandCursor)
        close.setStyleSheet(theme.GRAY_BUTTON_QSS)
        close.clicked.connect(self.close)
        row.addWidget(close)
        lay.addLayout(row)

        # ---- 摘要 ----
        card = QFrame()
        card.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        card.setStyleSheet('background:#F7FBF8; border:1px solid #dcefe2;'
                           'border-radius:8px;')
        cv = QVBoxLayout(card)
        cv.setContentsMargins(14, 10, 14, 10)
        cv.setSpacing(4)
        self.status_label = QLabel('-')
        self.status_label.setStyleSheet('font-size:13px; font-weight:600;'
                                        'color:#222; border:none;')
        self.status_label.setWordWrap(True)
        cv.addWidget(self.status_label)
        self.detail_label = QLabel('')
        self.detail_label.setWordWrap(True)
        self.detail_label.setStyleSheet('color:#555; font-size:11px;'
                                        'border:none;')
        cv.addWidget(self.detail_label)
        self.hint_label = QLabel('')
        self.hint_label.setWordWrap(True)
        self.hint_label.setStyleSheet('color:#E6A23C; font-size:11px;'
                                      'border:none;')
        cv.addWidget(self.hint_label)
        lay.addWidget(card)

        # ---- 明细表 ----
        self.table = QTableWidget(0, 8)
        self.table.setHorizontalHeaderLabels(
            ['#', '云端时间', '帧内时间', '时钟偏差', '距上条(分)',
             '预期时隙', '时隙偏差', '判定'])
        self.table.horizontalHeader().setDefaultAlignment(
            Qt.AlignmentFlag.AlignCenter)
        self.table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.ResizeToContents)
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(
            QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setShowGrid(False)
        self.table.setAlternatingRowColors(True)
        self.table.setStyleSheet(
            'QTableWidget { background:#FFFFFF; border:1px solid #eee;'
            ' border-radius:6px; font-size:12px; }'
            'QHeaderView::section { background:#F7F7F7; border:none;'
            ' padding:5px; color:#666; }')
        lay.addWidget(self.table, stretch=1)

    def _load_devices(self):
        devs = self._syncer.timesync_devices()
        devs.sort(key=lambda it: _dev_sort_key(it[0]))
        self.device_combo.blockSignals(True)
        self.device_combo.clear()
        if not devs:
            self.device_combo.addItem('（本机缓存无对时数据）', None)
        for did, cnt in devs:
            self.device_combo.addItem(f'{did}（{cnt:,} 条对时）', did)
        self.device_combo.blockSignals(False)
        self._analyze_async()

    def _analyze_async(self, *_):
        did = self.device_combo.currentData()
        if not did:
            self.status_label.setText('无设备可选')
            self.detail_label.setText('')
            self.hint_label.setText('')
            self.table.setRowCount(0)
            return
        syncer = self._syncer

        def work():
            # 多取一些，供按帧内容去重（多中继重复上云同一条上报）
            raw = syncer.query_device(did, limit=RECORDS * 4, log_type=2)
            rows = _dedupe_rows(raw, RECORDS)
            interval = syncer.device_interval(did)
            rf = syncer.device_rf_params(did)
            return did, rows, interval, rf

        self.status_label.setText('分析中…')
        self.api.run_async(work, self._apply, self._on_err)

    def _on_err(self, err):
        self.status_label.setText(f'分析失败：{err}')
        self.notify(f'对时校验分析失败：{err}')

    def _apply(self, pack):
        did, rows, interval, rf = pack
        if did != self.device_combo.currentData():
            return                            # 用户已切到别的设备，丢弃旧结果
        groups = rf['groups'] if rf else None
        r = analyze_timesync(rows, did, interval, groups)
        s = r['summary']
        param_txt = f'周期 {interval:g} 分' if interval else '周期未知'
        cnt_txt = (f"组台数 {s['groups']}"
                   + ('' if s['groups_known'] else '（未知，按默认 30）'))
        slot_txt = (f"预期时隙 {_sec_to_hms(s['offset'])}"
                    if s['offset'] is not None else '预期时隙未知')
        self.status_label.setText(
            f'{did} · 编号 {s["index"]} · {param_txt} · {cnt_txt} · {slot_txt}')
        bits = [f"时钟：{s['clock_ok']} 正确 / {s['clock_warn']} 漂移 / "
                f"{s['clock_bad']} 错乱"
                + (f"（平均偏差 {s['avg_clock']:+d} 秒）"
                   if s['avg_clock'] is not None else '')]
        if s['offset'] is not None:
            bits.append(f"时隙：{s['slot_ok']} 准点 / {s['slot_fast']} 快速轮 / "
                        f"{s['slot_bad']} 偏离（容差 ±{_SLOT_TOL} 秒）")
        bits.append(f"去重后共 {s['n']} 条上报"
                    + (f"，{s['dirty']} 条解析失败" if s['dirty'] else ''))
        self.detail_label.setText(' · '.join(bits))
        self.hint_label.setText(s['hint'])
        self._fill_table(r['recs'])

    def _verdict_of(self, rec):
        """→ (判定文本, 颜色)"""
        clock_v, slot_v = rec['clock_v'], rec['slot_v']
        if rec['frame_tod'] is None:
            return '🧹 解析失败/脏数据', _COLORS['dirty']
        if clock_v == 'bad':
            return '✗ 时间错乱（疑似未对时）', _COLORS['bad']
        if slot_v == 'fast':
            return (f"⚡ 时钟{rec['clock_diff']:+d}s · 开机快速轮",
                    _COLORS['fast'])
        if slot_v == 'bad':
            return (f"✗ 偏离时隙 {rec['slot_diff']:+d}s"
                    f"（时钟{rec['clock_diff']:+d}s）", _COLORS['bad'])
        if clock_v == 'warn':
            return f'⚠ 时钟漂移 {rec["clock_diff"]:+d}s', _COLORS['warn']
        if slot_v is None:
            return '✓ 时间正确（无周期配置）', _COLORS['ok']
        return '✓ 时间正确 · 准点', _COLORS['ok']

    def _fill_table(self, recs):
        t = self.table
        t.setRowCount(len(recs))
        for row_i, rec in enumerate(reversed(recs)):   # 最新在最上
            vtxt, vcol = self._verdict_of(rec)
            vals = [
                str(row_i + 1),
                rec['time'],
                _sec_to_hms(rec['frame_tod']) if rec['frame_tod'] is not None
                else '—',
                (f'{rec["clock_diff"]:+d}s'
                 if rec['clock_diff'] is not None else '—'),
                (f'{rec["gap"] / 60:g}' if rec['gap'] is not None else '—'),
                (_sec_to_hms(rec['slot_offset'])
                 if rec['slot_offset'] is not None else '—'),
                (f'{rec["slot_diff"]:+d}s'
                 if rec['slot_diff'] is not None else '—'),
                vtxt,
            ]
            for col, v in enumerate(vals):
                it = QTableWidgetItem(v)
                it.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                if col == 7:
                    it.setForeground(QColor(vcol))
                it.setToolTip(f'{rec["lorastr"]}\n云端时间: {rec["time"]}')
                t.setItem(row_i, col, it)
