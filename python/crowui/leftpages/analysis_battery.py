# -*- coding: utf-8 -*-
"""分析技能：电池电量分析——心跳电量序列收敛分析（小时中位数滤波+线性回归），
输出电池状态/消耗速率/预计可用天数。采样异常不直接影响结论。"""

import statistics
import time

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel,
                             QPushButton, QComboBox, QFrame, QSizePolicy,
                             QWidget)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QPainter, QPen, QBrush, QFont, QPainterPath

from crowui import theme
from crowui.ots_sync import DeviceLogSyncer

_RANGE_OPTIONS = [('全部', None), ('近90天', 90), ('近30天', 30), ('近7天', 7)]


def _converge(points):
    """原始电量点 → 每小时中位数（单点异常被同小时其它采样淹没）"""
    buckets = {}
    for ts, bv in points:
        buckets.setdefault(ts // 3600, []).append(bv)
    return sorted((k * 3600, statistics.median(v))
                  for k, v in buckets.items())


def _slope_per_day(med):
    """小时中位数序列线性回归斜率（%/天），只用最近7天；数据不足 None"""
    med = med[-24 * 7:]
    if len(med) < 6:
        return None
    n = len(med)
    mx = sum(t for t, _ in med) / n
    my = sum(v for _, v in med) / n
    num = sum((t - mx) * (v - my) for t, v in med)
    den = sum((t - mx) ** 2 for t, _ in med)
    if den == 0:
        return None
    return num / den * 86400.0


def analyze_battery(raw):
    """收敛分析：raw=[(time_str, ts, bv)]（时间正序）→ 结论 dict
    无效数据（非数字/越界/脏时间）全部剔除后再算"""
    pts = []
    for item in raw or []:
        try:
            ts, bv = float(item[1]), float(item[2])
        except (TypeError, ValueError, IndexError):
            continue
        if ts <= 0 or not 0 <= bv <= 100:
            continue
        pts.append((ts, bv))
    med = _converge(pts)
    slope = _slope_per_day(med)
    cur = int(med[-1][1]) if med else (pts[-1][1] if pts else None)

    # 异常跳动：15分钟内变化>=25%（重启/读数故障特征）
    jumps = 0
    for i in range(1, len(pts)):
        dt = pts[i][0] - pts[i - 1][0]
        if 0 <= dt <= 900 and abs(pts[i][1] - pts[i - 1][1]) >= 25:
            jumps += 1
    # 充电事件：中位数相邻抬升>=8
    charges = sum(1 for i in range(1, len(med))
                  if med[i][1] - med[i - 1][1] >= 8)

    # 换电事件：电量大幅跳升(>=30)且跳升前处于低位(<=25)=换新电池；
    # 低位但>25 视为大充电。统计换电周期 → 预计下次换电时间
    swaps = []                     # [(ts, 跳升前, 跳升后)]
    for i in range(1, len(med)):
        rise = med[i][1] - med[i - 1][1]
        if rise >= 30 and med[i - 1][1] <= 25:
            swaps.append((med[i][0], med[i - 1][1], med[i][1]))
    cycle_days = None
    next_swap_ts = None
    if len(swaps) >= 2:
        gaps = [(swaps[i][0] - swaps[i - 1][0]) / 86400.0
                for i in range(1, len(swaps))]
        cycle_days = statistics.median(gaps)
        next_swap_ts = swaps[-1][0] + cycle_days * 86400
    deep_zero = any(v <= 5 for _t, v in med)   # 曾电量耗尽读 0

    if cur is None:
        status, left_days = '无有效电量数据', None
    elif slope is None:
        status, left_days = '数据不足（采样太少）', None
    elif slope > 2:
        status, left_days = '充电中 / 补电', None
    elif slope < -0.8:
        status = '放电中'
        left_days = max(0.0, cur / -slope) if cur > 0 else 0.0
    else:
        status, left_days = '平稳（自耗电很慢）', None

    return {'cur': cur, 'slope': slope, 'status': status,
            'left_days': left_days, 'jumps': jumps, 'charges': charges,
            'swaps': swaps, 'cycle_days': cycle_days,
            'next_swap_ts': next_swap_ts, 'deep_zero': deep_zero,
            'raw_n': len(pts), 'med_n': len(med), 'med': med,
            'span': f'{raw[0][0]} ～ {raw[-1][0]}' if raw else ''}

class BatteryChart(QWidget):
    """电量曲线：灰点=原始采样，绿线=小时中位数收敛曲线"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._raw = []
        self._med = []
        self.setMinimumHeight(180)
        self.setSizePolicy(QSizePolicy.Policy.Expanding,
                           QSizePolicy.Policy.Expanding)

    def set_data(self, raw, med):
        self._raw = raw or []
        self._med = med or []
        self.update()

    def paintEvent(self, ev):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), QColor('#FFFFFF'))
        pad_l, pad_r, pad_t, pad_b = 34, 12, 12, 20
        w = self.width() - pad_l - pad_r
        h = self.height() - pad_t - pad_b
        p.setFont(QFont('Microsoft YaHei', 7))
        try:
            raw = self._sanitize(self._raw)
            med = self._sanitize(self._med)
            for v, lbl in ((100, '100'), (50, '50'), (0, '0')):
                y = pad_t + h * (1 - v / 100)
                p.setPen(QPen(QColor('#F0F0F0'), 1))
                p.drawLine(pad_l, int(y), pad_l + w, int(y))
                p.setPen(QColor('#999999'))
                p.drawText(4, int(y) + 4, lbl)
            if len(raw) < 2 or w <= 10 or h <= 10:
                p.setPen(QColor('#BBBBBB'))
                p.drawText(pad_l, pad_t + h // 2, '暂无数据')
                return
            t0, t1 = raw[0][0], raw[-1][0]
            if t1 <= t0:
                t1 = t0 + 1

            def xy(ts, bv):
                x = pad_l + w * (ts - t0) / (t1 - t0)
                y = pad_t + h * (1 - max(0, min(100, bv)) / 100)
                return max(-50, min(self.width() + 50, x)), \
                    max(-50, min(self.height() + 50, y))

            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(QBrush(QColor(180, 180, 180, 110)))
            for ts, bv, _lbl in raw:
                x, y = xy(ts, bv)
                p.drawEllipse(int(x) - 1, int(y) - 1, 2, 2)
            if len(med) >= 2:
                path = QPainterPath()
                first = True
                for ts, bv, _lbl in med:
                    x, y = xy(ts, bv)
                    if first:
                        path.moveTo(x, y)
                        first = False
                    else:
                        path.lineTo(x, y)
                p.setPen(QPen(QColor(7, 193, 96), 2))
                p.setBrush(Qt.BrushStyle.NoBrush)
                p.drawPath(path)
            p.setPen(QColor('#999999'))
            p.drawText(pad_l, self.height() - 6, str(raw[0][2]))
            p.drawText(pad_l + w - 130, self.height() - 6, str(raw[-1][2]))
        except Exception:
            # 数据异常绝不能让进程崩（paintEvent 未捕获异常 = qFatal 退出）
            import traceback
            traceback.print_exc()      # 打到控制台便于定位
            try:
                p.setPen(QColor('#BBBBBB'))
                p.drawText(pad_l, pad_t + h // 2, '数据异常，无法绘制')
            except Exception:
                pass

    @staticmethod
    def _sanitize(points):
        """只留 (数字ts, 数字电量)，并记录原时间文本做轴标注"""
        out = []
        for item in points or []:
            try:
                if len(item) == 3:      # (time_str, ts, bv)
                    ts, bv = float(item[1]), float(item[2])
                    label = str(item[0])
                else:                   # (ts, bv)
                    ts, bv = float(item[0]), float(item[1])
                    label = str(item[0])
                if not (0 <= bv <= 100) or ts <= 0:
                    continue
                out.append((ts, bv, label))
            except (TypeError, ValueError, IndexError):
                continue                # 无效数据直接剔除
        out.sort(key=lambda it: it[0])
        return out


class BatteryDialog(QDialog):
    """电池电量分析独立窗口"""

    def __init__(self, api, notify=None, parent=None):
        super().__init__(parent)
        self.api = api
        self.notify = notify or (lambda msg: print('[电池分析]', msg))
        self._syncer = DeviceLogSyncer(api)
        self._raw = []
        self.setWindowTitle('分析技能 · 电池电量分析')
        self.setMinimumSize(560, 480)
        self._setup_ui()
        self._load_devices()

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(18, 16, 18, 14)
        lay.setSpacing(10)

        title = QLabel('🔋 电池电量分析')
        title.setStyleSheet('font-size:15px; font-weight:600; color:#333;')
        lay.addWidget(title)
        tip = QLabel('心跳电量做收敛处理（小时中位数滤波），单次读数异常不影响'
                     '结论；消耗速率取最近7天回归。纯本地查询。')
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
        row.addWidget(QLabel('范围:'))
        self.range_combo = QComboBox()
        for label, d in _RANGE_OPTIONS:
            self.range_combo.addItem(label, d)
        self.range_combo.currentIndexChanged.connect(self._analyze_async)
        row.addWidget(self.range_combo)
        lay.addLayout(row)

        stat_card = QFrame()
        stat_card.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        stat_card.setStyleSheet('background:#F7FBF8; border:1px solid #dcefe2;'
                                'border-radius:8px;')
        sv = QVBoxLayout(stat_card)
        sv.setContentsMargins(14, 10, 14, 10)
        self.status_label = QLabel('-')
        self.status_label.setStyleSheet('font-size:16px; font-weight:600;'
                                        'color:#222; border:none;')
        sv.addWidget(self.status_label)
        self.detail_label = QLabel('')
        self.detail_label.setWordWrap(True)
        self.detail_label.setStyleSheet('color:#555; font-size:11px;'
                                        'border:none;')
        sv.addWidget(self.detail_label)
        lay.addWidget(stat_card)

        self.chart = BatteryChart()
        lay.addWidget(self.chart, stretch=1)

        btn_row = QHBoxLayout()
        btn_row.addStretch()
        close_btn = QPushButton('关闭')
        close_btn.setFixedSize(70, 32)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet(theme.GRAY_BUTTON_QSS)
        close_btn.clicked.connect(self.close)
        btn_row.addWidget(close_btn)
        lay.addLayout(btn_row)

    def _load_devices(self):
        devices = self._syncer.battery_devices()
        self.device_combo.blockSignals(True)
        self.device_combo.clear()
        if not devices:
            self.device_combo.addItem('（本机缓存无心跳电量数据）', None)
        for did, cnt in devices:
            self.device_combo.addItem(f'{did}（{cnt:,} 次心跳）', did)
        self.device_combo.blockSignals(False)
        self._analyze_async()

    def _analyze_async(self, *_):
        did = self.device_combo.currentData()
        if not did:
            self.status_label.setText('无设备可选')
            self.detail_label.setText('')
            self.chart.set_data([], [])
            return
        days = self.range_combo.currentData()
        syncer = self._syncer

        def work():
            raw = syncer.device_battery(did, days=days)
            return raw, analyze_battery(raw)

        self.status_label.setText('分析中…')
        self.api.run_async(work, self._apply, self._on_err)

    def _apply(self, pack):
        raw, r = pack
        self._raw = raw
        cur = r['cur'] if r['cur'] is not None else '—'
        self.status_label.setText(f'当前电量 {cur}% · {r["status"]}')
        bits = []
        if r['slope'] is not None:
            bits.append(f"消耗速率 {r['slope']:+.1f}%/天")
        # ---- 换电预期（提示用）----
        swap_hint = ''
        if r['next_swap_ts'] and r['cycle_days']:
            dts = (r['next_swap_ts'] - time.time()) / 86400
            when = time.strftime('%m/%d', time.localtime(r['next_swap_ts']))
            swap_hint = (f"换电周期约 {r['cycle_days']:.0f} 天，"
                         f"预计下次换电 {when}（{'已到期' if dts <= 0 else f'{dts:.0f} 天后'}）")
        elif r['left_days'] is not None:
            swap_hint = f"按当前放电速率，预计 {r['left_days']:.0f} 天后需换电/充电"
        if swap_hint:
            bits.append('🔋 ' + swap_hint)
        if r['swaps']:
            last = time.strftime('%m/%d', time.localtime(r['swaps'][-1][0]))
            bits.append(f"换电 {len(r['swaps'])} 次（上次 {last}）")
        if r['charges']:
            bits.append(f"充电/补电 {r['charges']} 次")
        if r['deep_zero']:
            bits.append('⚠ 曾电量耗尽（读数 0）')
        bits.append(f"采样收敛 {r['raw_n']:,} → {r['med_n']:,} 点")
        if r['jumps']:
            bits.append(f"⚠ 异常跳动 {r['jumps']} 次（已剔除影响）")
        if r['span']:
            bits.append(f"区间 {r['span']}")
        self.detail_label.setText(' · '.join(bits))
        self.chart.set_data(self._raw, r['med'])

    def _on_err(self, err):
        self.status_label.setText(f'分析失败：{err}')
