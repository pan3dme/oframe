// pages/battery-chart/battery-chart.js — 设备电量走势图
// 数据来源：对时记录（lorastr 以 "2|" 开头），格式 2|device|时间戳|电量|...
//   - parts[2] 为设备真实时间（Unix 秒或毫秒），缺失时回退 record.time（服务器接收时间）
//   - parts[3] 为电量（0~1 小数或 0~100 百分比），统一归一为百分比
// 拉取方式：action=getDeviceLogbyId，每页 100 条翻页，直到凑够目标电量点数或没有更多数据

const dataCache = require('../../config/data-cache.js')

const LIMIT_OPTIONS = [10, 20, 30, 50, 80, 100] // 可选展示点数
const PAGE_SIZE = 100                // 每次请求最多 100 条
const MAX_PAGES = 8                  // 最多翻 8 页（800 条原始记录），防止请求过多
const JUMP_DELTA = 15                // 电量回升 ≥15 个百分点，计为一次换电/重新上电
const MIN_VALID = 0.05               // 忽略过低无效读数（<5%）

// ============ 安全取全局配置（不在模块顶层调用 getApp） ============
function _app() {
  try {
    return (typeof getApp === 'function') ? getApp() : null
  } catch (e) {
    return null
  }
}

function _apiUrl() {
  const a = _app()
  return (a && a.globalData && a.globalData.api_device_Url) || ''
}

function _wechatId() {
  const a = _app()
  return (a && typeof a.getWechatId === 'function') ? a.getWechatId() : ''
}

function _now() {
  const a = _app()
  return (a && typeof a.formatTime === 'function') ? a.formatTime() : ''
}

// 电量归一：0~1 小数或 0~100 百分比 → 0~1，无效返回 null
function parseBatteryValue(v) {
  if (v === null || v === undefined || v === '') return null
  const n = parseFloat(v)
  if (isNaN(n) || n <= 0) return null
  return n > 1 ? n / 100 : n
}

// 从接口返回中取出原始记录数组
function rawList(data) {
  if (data && data.data && Array.isArray(data.data)) return data.data
  if (Array.isArray(data)) return data
  return []
}

Page({
  data: {
    // ---- 设备选择 ----
    deviceOptions: [],       // [{ id, label }]
    deviceLabels: [],        // picker 显示用
    selectedDeviceIndex: 0,
    selectedDeviceId: '',

    // ---- 点数选择 ----
    limitOptions: LIMIT_OPTIONS,
    selectedLimit: 100,

    // ---- 状态 ----
    loading: false,
    errorMsg: '',
    scannedRaw: 0,           // 扫描到的原始记录条数

    // ---- 图表数据 ----
    points: [],              // [{ t, pct, timeStr }] 时间升序
    summary: null,           // { count, latest, max, min, spanText, jumpCount, firstTime, lastTime }
    hoverText: '',           // 触摸图表时显示的点信息
    hoverIndex: -1
  },

  onLoad() {
    this._loadDevices()
  },

  // ============ 设备列表 ============
  _loadDevices() {
    dataCache.getDeviceList((deviceData) => {
      const list = (deviceData && deviceData.recordList) ? deviceData.recordList : []
      const seen = {}
      const options = []
      list.forEach(r => {
        if (!r.deviceId || r.deviceId === '-' || seen[r.deviceId]) return
        seen[r.deviceId] = true
        options.push({
          id: r.deviceId,
          label: r.rename ? (r.deviceId + '（' + r.rename + '）') : r.deviceId
        })
      })
      options.sort((a, b) => (a.id > b.id ? 1 : (a.id < b.id ? -1 : 0)))

      if (!options.length) {
        this.setData({ errorMsg: '未获取到设备列表，请检查网络' })
        return
      }

      // 默认选中：优先首页当前选中的设备
      let idx = 0
      try {
        const cur = dataCache.getHomeSelectedDevice()
        if (cur && typeof cur === 'string') {
          const i = options.findIndex(o => o.id === cur)
          if (i >= 0) idx = i
        }
      } catch (e) { /* ignore */ }

      this.setData({
        deviceOptions: options,
        deviceLabels: options.map(o => o.label),
        selectedDeviceIndex: idx,
        selectedDeviceId: options[idx].id
      }, () => {
        this._query()
      })
    })
  },

  onDeviceChange(e) {
    const idx = parseInt(e.detail.value, 10)
    if (isNaN(idx)) return
    this.setData({
      selectedDeviceIndex: idx,
      selectedDeviceId: this.data.deviceOptions[idx].id
    }, () => this._query())
  },

  onLimitTap(e) {
    const n = parseInt(e.currentTarget.dataset.n, 10)
    if (isNaN(n) || n === this.data.selectedLimit) return
    this.setData({ selectedLimit: n }, () => this._query())
  },

  onRefresh() {
    this._query()
  },

  // ============ 数据拉取 ============
  _query() {
    const deviceId = this.data.selectedDeviceId
    if (!deviceId) {
      wx.showToast({ title: '请选择设备', icon: 'none' })
      return
    }
    this.setData({
      loading: true,
      errorMsg: '',
      points: [],
      summary: null,
      hoverText: '',
      hoverIndex: -1,
      scannedRaw: 0
    })
    wx.showLoading({ title: '加载中...' })
    this._fetchPages(deviceId, this.data.selectedLimit, 0, [], 0)
  },

  // 翻页拉取，直到凑够 need 个电量点 / 无更多数据 / 达到页数上限
  _fetchPages(deviceId, need, page, acc, rawCount) {
    const that = this
    if (page >= MAX_PAGES) {
      that._finish(acc, rawCount, need)
      return
    }
    wx.request({
      url: _apiUrl(),
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: {
          limit: PAGE_SIZE,
          deviceId: deviceId,
          offset: page * PAGE_SIZE,
          wechatid: _wechatId()
        },
        time: _now()
      },
      timeout: 10000,
      success: (res) => {
        const raw = rawList(res.data)
        const pts = that._extractPoints(raw, deviceId)
        const merged = acc.concat(pts)
        const totalRaw = rawCount + raw.length
        console.log('[电量走势] 第' + page + '页：原始' + raw.length +
          '条，电量点' + pts.length + '个，累计' + merged.length + '个')

        const noMore = raw.length < PAGE_SIZE
        if (noMore || merged.length >= need || page + 1 >= MAX_PAGES) {
          that._finish(merged, totalRaw, need)
        } else {
          that._fetchPages(deviceId, need, page + 1, merged, totalRaw)
        }
      },
      fail: (err) => {
        console.error('[电量走势] 请求失败:', err)
        wx.hideLoading()
        that.setData({ loading: false, errorMsg: '网络请求失败，请点击刷新重试' })
      }
    })
  },

  // 从原始记录中提取电量点（只认 type=2 对时记录）
  _extractPoints(list, deviceId) {
    const out = []
    list.forEach((record) => {
      const attr = {}
      if (record.attributes && Array.isArray(record.attributes)) {
        record.attributes.forEach(i => { attr[i.columnName] = i.columnValue })
      }
      if (record.primaryKey && Array.isArray(record.primaryKey)) {
        record.primaryKey.forEach(i => { attr[i.name] = i.value })
      }

      const lorastr = attr.lorastr || record.lorastr || ''
      // 只处理对时记录
      if (!lorastr || String(lorastr).indexOf('2|') !== 0) return

      const parts = String(lorastr).split('|')
      // type=2 记录的实际设备ID在 parts[1]（中继转发时 record.deviceId 是中继）
      const innerDevice = parts[1] || ''
      const recDevice = attr.deviceId || attr.deviceid || record.deviceId || record.deviceid || ''
      const matchDevice = innerDevice || recDevice
      if (deviceId && matchDevice && matchDevice !== deviceId) return

      // 时间：优先 lorastr 内的时间戳
      let t = 0
      if (parts.length >= 3) {
        const n = parseInt(parts[2], 10)
        if (!isNaN(n) && n > 0) t = n < 1e10 ? n * 1000 : n
      }
      if (!t) {
        const rt = attr.time || record.time || ''
        const dt = new Date(rt).getTime()
        if (!isNaN(dt) && dt > 0) t = dt
      }
      if (!t || isNaN(t)) return

      // 电量在 parts[3]
      let b = parts.length >= 4 ? parseBatteryValue(parts[3]) : null
      if (b === null) b = parseBatteryValue(attr.battery != null ? attr.battery : record.battery)
      if (b === null || b < MIN_VALID) return

      out.push({ t: t, pct: Math.round(b * 1000) / 10 })
    })
    return out
  },

  // 收尾：去重 → 升序 → 取最新 need 条 → 汇总 → 绘图
  _finish(allPoints, rawCount, need) {
    wx.hideLoading()
    const seen = {}
    let pts = []
    allPoints.forEach(p => {
      const key = String(p.t)
      if (seen[key]) return
      seen[key] = true
      pts.push(p)
    })
    pts.sort((a, b) => a.t - b.t)
    // 只保留最新的 need 个点
    if (pts.length > need) pts = pts.slice(pts.length - need)

    if (!pts.length) {
      this.setData({
        loading: false,
        points: [],
        summary: null,
        scannedRaw: rawCount,
        errorMsg: '未找到该设备的对时电量记录（已扫描 ' + rawCount + ' 条）'
      })
      return
    }

    let maxP = pts[0]
    let minP = pts[0]
    let jumpCount = 0
    for (let i = 0; i < pts.length; i++) {
      const p = pts[i]
      p.timeStr = this._fmtTime(p.t)
      if (p.pct > maxP.pct) maxP = p
      if (p.pct < minP.pct) minP = p
      if (i > 0 && (p.pct - pts[i - 1].pct) >= JUMP_DELTA) jumpCount++
    }

    const first = pts[0]
    const last = pts[pts.length - 1]
    const summary = {
      count: pts.length,
      latest: Math.round(last.pct),
      latestTime: last.timeStr,
      max: Math.round(maxP.pct),
      maxTime: maxP.timeStr,
      min: Math.round(minP.pct),
      minTime: minP.timeStr,
      spanText: this._fmtSpan(last.t - first.t),
      jumpCount: jumpCount,
      firstTime: first.timeStr,
      lastTime: last.timeStr
    }

    this.setData({
      loading: false,
      errorMsg: '',
      points: pts,
      summary: summary,
      scannedRaw: rawCount
    }, () => {
      // 等布局完成后再取 canvas 尺寸
      setTimeout(() => this._drawChart(), 50)
    })
  },

  // ============ 格式化 ============
  _pad2(n) {
    return String(n).padStart(2, '0')
  },

  _fmtTime(t) {
    const d = new Date(t)
    return (d.getMonth() + 1) + '/' + d.getDate() + ' ' +
      this._pad2(d.getHours()) + ':' + this._pad2(d.getMinutes())
  },

  _fmtSpan(ms) {
    if (!ms || ms < 0) return '—'
    const min = ms / 60000
    if (min < 60) return Math.round(min) + '分钟'
    const h = ms / 3600000
    if (h < 24) return (Math.round(h * 10) / 10) + '小时'
    return (Math.round(h / 24 * 10) / 10) + '天'
  },

  // ============ 绘制折线图（canvas 2d） ============
  _drawChart() {
    const pts = this.data.points
    if (!pts || !pts.length) return

    const query = wx.createSelectorQuery()
    query.select('#batteryCanvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) {
        console.warn('[电量走势] canvas 未就绪')
        return
      }
      const canvas = res[0].node
      const ctx = canvas.getContext('2d')
      const dpr = (wx.getSystemInfoSync().pixelRatio) || 2
      const W = res[0].width
      const H = res[0].height
      if (!W || !H) return

      canvas.width = W * dpr
      canvas.height = H * dpr
      ctx.scale(dpr, dpr)
      ctx.clearRect(0, 0, W, H)

      const padL = 42
      const padR = 14
      const padT = 24
      const padB = 30
      const plotW = W - padL - padR
      const plotH = H - padT - padB

      // ---- Y 轴范围自适应（取整到 5%，至少 10 个百分点） ----
      let lo = pts[0].pct
      let hi = pts[0].pct
      pts.forEach(p => {
        if (p.pct < lo) lo = p.pct
        if (p.pct > hi) hi = p.pct
      })
      const padV = Math.max(2, (hi - lo) * 0.15)
      let yMin = Math.floor((lo - padV) / 5) * 5
      let yMax = Math.ceil((hi + padV) / 5) * 5
      if (yMin < 0) yMin = 0
      if (yMax > 100) yMax = 100
      if (yMax - yMin < 10) {
        yMax = Math.min(100, yMin + 10)
        if (yMax - yMin < 10) yMin = Math.max(0, yMax - 10)
      }

      // ---- X 轴时间范围 ----
      const t0 = pts[0].t
      const t1 = pts[pts.length - 1].t
      const tSpan = (t1 - t0) || 1

      const xs = []
      const ys = []
      for (let i = 0; i < pts.length; i++) {
        const p = pts[i]
        const x = padL + (pts.length === 1 ? plotW / 2 : ((p.t - t0) / tSpan) * plotW)
        const y = padT + plotH - ((p.pct - yMin) / (yMax - yMin)) * plotH
        xs.push(x)
        ys.push(y)
      }
      // 缓存坐标，供触摸定位使用
      this._xs = xs
      this._ys = ys
      this._padL = padL
      this._plotW = plotW

      // ---- 网格 + Y 轴标签 ----
      ctx.font = '10px sans-serif'
      ctx.textAlign = 'right'
      ctx.textBaseline = 'middle'
      ctx.lineWidth = 1
      for (let i = 0; i <= 4; i++) {
        const v = yMin + (yMax - yMin) * i / 4
        const y = padT + plotH - (plotH * i / 4)
        ctx.strokeStyle = '#eeeeee'
        ctx.beginPath()
        ctx.moveTo(padL, y)
        ctx.lineTo(padL + plotW, y)
        ctx.stroke()
        ctx.fillStyle = '#999999'
        ctx.fillText(Math.round(v) + '%', padL - 6, y)
      }

      // ---- 折线下方填充 ----
      const grad = ctx.createLinearGradient(0, padT, 0, padT + plotH)
      grad.addColorStop(0, 'rgba(7, 193, 96, 0.28)')
      grad.addColorStop(1, 'rgba(7, 193, 96, 0.02)')
      ctx.beginPath()
      ctx.moveTo(xs[0], padT + plotH)
      for (let i = 0; i < xs.length; i++) ctx.lineTo(xs[i], ys[i])
      ctx.lineTo(xs[xs.length - 1], padT + plotH)
      ctx.closePath()
      ctx.fillStyle = grad
      ctx.fill()

      // ---- 折线 ----
      ctx.beginPath()
      ctx.moveTo(xs[0], ys[0])
      for (let i = 1; i < xs.length; i++) ctx.lineTo(xs[i], ys[i])
      ctx.strokeStyle = '#07c160'
      ctx.lineWidth = 2
      ctx.lineJoin = 'round'
      ctx.stroke()

      // ---- 数据点（点数不多时才画，避免糊成一片） ----
      if (pts.length <= 80) {
        ctx.fillStyle = '#ffffff'
        ctx.strokeStyle = '#07c160'
        ctx.lineWidth = 1.5
        for (let i = 0; i < xs.length; i++) {
          ctx.beginPath()
          ctx.arc(xs[i], ys[i], 2.4, 0, Math.PI * 2)
          ctx.fill()
          ctx.stroke()
        }
      }

      // ---- 换电/回升标记（橙色圆圈） ----
      ctx.strokeStyle = '#ff9500'
      ctx.lineWidth = 1.5
      for (let i = 1; i < pts.length; i++) {
        if ((pts[i].pct - pts[i - 1].pct) >= JUMP_DELTA) {
          ctx.beginPath()
          ctx.arc(xs[i], ys[i], 5, 0, Math.PI * 2)
          ctx.stroke()
        }
      }

      // ---- X 轴时间标签（首、中、尾） ----
      ctx.fillStyle = '#999999'
      ctx.textBaseline = 'top'
      const labelY = padT + plotH + 8
      if (pts.length === 1) {
        ctx.textAlign = 'center'
        ctx.fillText(pts[0].timeStr, xs[0], labelY)
      } else {
        ctx.textAlign = 'left'
        ctx.fillText(pts[0].timeStr, padL, labelY)
        const mid = Math.floor((pts.length - 1) / 2)
        ctx.textAlign = 'center'
        ctx.fillText(pts[mid].timeStr, xs[mid], labelY)
        ctx.textAlign = 'right'
        ctx.fillText(pts[pts.length - 1].timeStr, padL + plotW, labelY)
      }

      // ---- 触摸指示（竖线 + 高亮点） ----
      const hoverIdx = this.data.hoverIndex
      if (hoverIdx >= 0 && hoverIdx < xs.length) {
        ctx.strokeStyle = 'rgba(25, 137, 250, 0.6)'
        ctx.lineWidth = 1
        ctx.setLineDash([3, 3])
        ctx.beginPath()
        ctx.moveTo(xs[hoverIdx], padT)
        ctx.lineTo(xs[hoverIdx], padT + plotH)
        ctx.stroke()
        ctx.setLineDash([])

        ctx.beginPath()
        ctx.arc(xs[hoverIdx], ys[hoverIdx], 4.5, 0, Math.PI * 2)
        ctx.fillStyle = '#1989fa'
        ctx.fill()
        ctx.strokeStyle = '#ffffff'
        ctx.lineWidth = 1.5
        ctx.stroke()
      }
    })
  },

  // ============ 触摸查看某点 ============
  onChartTouch(e) {
    const pts = this.data.points
    if (!pts || !pts.length || !this._xs) return
    const x = (e.touches && e.touches[0]) ? e.touches[0].x : 0
    let best = 0
    let bestD = Infinity
    for (let i = 0; i < this._xs.length; i++) {
      const d = Math.abs(this._xs[i] - x)
      if (d < bestD) {
        bestD = d
        best = i
      }
    }
    const p = pts[best]
    if (this.data.hoverIndex === best) return
    this.setData({
      hoverIndex: best,
      hoverText: p.timeStr + '  ' + p.pct + '%'
    }, () => this._drawChart())
  },

  onChartTouchEnd() {
    if (this.data.hoverIndex < 0) return
    this.setData({ hoverIndex: -1, hoverText: '' }, () => this._drawChart())
  }
})
