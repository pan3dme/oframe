// road-optimize.js - 道路优化（两条道路对比 + 合并）
const dataCache = require('../../config/data-cache.js')
const { wgs84ToGcj02, gcj02ToWgs84, parseRoadPoints, calcDistance } = require('../../utils/coord-transform.js')
const app = getApp()
const API_URL = app.globalData.api_route_place_Url

Page({
  data: {
    // 道路列表
    roadList: [],
    roadNamesA: ['请选择道路A'],
    roadNamesB: ['请选择道路B'],
    roadIndexA: 0,
    roadIndexB: 0,
    selectedRoadA: null,
    selectedRoadB: null,

    // 地图
    mapCenter: { lat: 26.529950, lng: 109.390224 },
    scale: 14,
    polylines: [],

    // 合并状态
    merged: false,
    mergedPointCount: 0,

    loading: true
  },

  onLoad() {
    this._loadRoadData()
  },

  _loadRoadData() {
    dataCache.getRoadListFromCache((cachedData) => {
      const roadList = (cachedData.roadList || []).map(item => ({
        roadId: item.route_id,
        name: item.roadname,
        points: item.roadinfo,
        level: item.level || '1'
      }))

      const names = roadList.map(r => r.name || r.roadId || '未命名')
      const namesA = ['请选择道路A', ...names]
      const namesB = ['请选择道路B', ...names]

      this.setData({
        roadList,
        roadNamesA: namesA,
        roadNamesB: namesB,
        loading: false
      })
    })
  },

  // ========== 选取道路A ==========
  onRoadAChange(e) {
    const index = parseInt(e.detail.value)
    this.setData({ roadIndexA: index })
    if (index === 0) {
      this.setData({ selectedRoadA: null })
    } else {
      const road = this.data.roadList[index - 1]
      this.setData({ selectedRoadA: road })
    }
    this._refreshPolylines()
  },

  // ========== 选取道路B ==========
  onRoadBChange(e) {
    const index = parseInt(e.detail.value)
    this.setData({ roadIndexB: index })
    if (index === 0) {
      this.setData({ selectedRoadB: null })
    } else {
      const road = this.data.roadList[index - 1]
      this.setData({ selectedRoadB: road })
    }
    this._refreshPolylines()
  },

  // ========== 合并按钮 ==========
  onMerge() {
    if (!this.data.selectedRoadA || !this.data.selectedRoadB) {
      wx.showToast({ title: '请先选择两条道路', icon: 'none' })
      return
    }

    const roadA = this.data.selectedRoadA
    const roadB = this.data.selectedRoadB

    // 解析原始 WGS-84 坐标
    const rawA = parseRoadPoints(roadA.points || '')
    const rawB = parseRoadPoints(roadB.points || '')

    if (rawA.length < 2 || rawB.length < 2) {
      wx.showToast({ title: '两条道路都需要至少2个坐标点', icon: 'none' })
      return
    }

    // WGS-84 → GCJ-02（微信地图用 GCJ-02），统一字段名为 latitude/longitude
    const toLatLng = (gcj) => ({ latitude: gcj.lat, longitude: gcj.lng })
    const gcjA = rawA.map(p => toLatLng(wgs84ToGcj02(p.lng, p.lat)))
    const gcjB = rawB.map(p => toLatLng(wgs84ToGcj02(p.lng, p.lat)))

    // 判断两路方向是否一致（起点靠近 → 同向；起点靠近终点 → 反向）
    const dAA = calcDistance(gcjA[0].latitude, gcjA[0].longitude, gcjB[0].latitude, gcjB[0].longitude)
    const dAB = calcDistance(gcjA[0].latitude, gcjA[0].longitude, gcjB[gcjB.length - 1].latitude, gcjB[gcjB.length - 1].longitude)
    const trackBordered = dAA <= dAB ? gcjB : [...gcjB].reverse()

    // 执行合并
    const mergedGcj = this._mergeTracks(gcjA, trackBordered)

    // 合并后的 WGS-84（用于后续提交）
    const mergedWgs = mergedGcj.map(p => {
      const wgs = gcj02ToWgs84(p.longitude, p.latitude)
      return { lat: wgs.lat, lng: wgs.lng }
    })

    // 保存合并结果
    this._mergedGcj = mergedGcj
    this._mergedWgs = mergedWgs

    // 刷新地图显示（包含合并线）
    this._refreshPolylines()
    this.setData({ merged: true, mergedPointCount: mergedGcj.length })

    const aLen = rawA.length, bLen = rawB.length
    wx.showToast({ title: `拼接完成：${aLen}+${bLen}→${mergedGcj.length}点`, icon: 'success', duration: 2000 })
  },

  // ========== 核心合并算法：找重叠段 + 拼接 ==========
  // 思路：道路1 = A→B→C→D→E，道路2 = C→D→E→F→...→K
  // 重叠段 = CDE，只在重叠段做GPS偏移校正（平均），其余保持原样
  _mergeTracks(trackA, trackB) {
    const OVERLAP_THRESHOLD = 15 // 米，两点距离小于此值视为"匹配"

    // 为 trackA 每个点找到 trackB 中最近的点
    const matchAtoB = trackA.map((ptA) => {
      let minDist = Infinity, minIdx = -1
      for (let iB = 0; iB < trackB.length; iB++) {
        const d = calcDistance(ptA.latitude, ptA.longitude, trackB[iB].latitude, trackB[iB].longitude)
        if (d < minDist) { minDist = d; minIdx = iB }
      }
      return { dist: minDist, idxB: minIdx }
    })

    // 在 trackA 中找到最长连续匹配段（距离 < 阈值）
    let best = { len: 0, startA: -1, endA: -1, startB: -1, endB: -1 }
    let curStart = -1, curLen = 0

    for (let i = 0; i < matchAtoB.length; i++) {
      if (matchAtoB[i].dist < OVERLAP_THRESHOLD) {
        if (curStart === -1) curStart = i
        curLen++
      } else {
        if (curLen > best.len) {
          best.len = curLen
          best.startA = curStart
          best.endA = i - 1
          best.startB = matchAtoB[curStart].idxB
          best.endB = matchAtoB[i - 1].idxB
        }
        curStart = -1; curLen = 0
      }
    }
    // 末尾段
    if (curLen > best.len) {
      best.len = curLen
      best.startA = curStart
      best.endA = matchAtoB.length - 1
      best.startB = matchAtoB[curStart].idxB
      best.endB = matchAtoB[matchAtoB.length - 1].idxB
    }

    // 无足够重叠 → 直接拼接
    if (best.len < 2) {
      wx.showToast({ title: '未找到重叠路段，将首尾拼接', icon: 'none', duration: 2000 })
      return [...trackA, ...trackB]
    }

    // 判断 trackB 在重叠段的方向（匹配索引递增=同向，递减=反向）
    const bReversed = best.startB > best.endB
    const trackBordered = bReversed ? [...trackB].reverse() : [...trackB]

    // 反向时重新计算 trackB 的重叠起止索引
    if (bReversed) {
      const nB = trackB.length
      best.startB = nB - 1 - best.startB
      best.endB = nB - 1 - best.endB
      // 现在 startB < endB
    }

    // 三段拆分：前缀(重叠之前)、重叠段、后缀(重叠之后)
    const prefixA = trackA.slice(0, best.startA)                           // A→B（A的非重叠前缀）
    const prefixB = trackBordered.slice(0, best.startB)                    // B的非重叠前缀
    const overlapA = trackA.slice(best.startA, best.endA + 1)              // C→D→E（A视角）
    const overlapB = trackBordered.slice(best.startB, best.endB + 1)       // C→D→E（B视角）
    const suffixA = trackA.slice(best.endA + 1)                            // A的非重叠后缀 F→...→K
    const suffixB = trackBordered.slice(best.endB + 1)                     // B的非重叠后缀

    // 只对重叠段做GPS偏移校正（重采样 + 平均）
    const mergedOverlap = this._mergeOverlap(overlapA, overlapB)

    // 拼接策略：前缀/后缀取较长者（同一段路走了两次，保留更长的那次记录）
    // 重叠段用两次平均值（多次行走平均后GPS更稳定）
    const prefix  = prefixA.length >= prefixB.length ? prefixA : prefixB
    const suffix  = suffixA.length >= suffixB.length ? suffixA : suffixB
    const raw = [...prefix, ...mergedOverlap, ...suffix]
    const result = [raw[0]]
    for (let i = 1; i < raw.length; i++) {
      const prev = result[result.length - 1]
      if (calcDistance(prev.latitude, prev.longitude, raw[i].latitude, raw[i].longitude) >= 1.0) {
        result.push(raw[i])
      }
    }

    return result
  },

  // ========== 重叠段融合：重采样 + 50/50 平均 ==========
  _mergeOverlap(segA, segB) {
    if (segA.length < 2 || segB.length < 2) {
      return segA.length >= segB.length ? [...segA] : [...segB]
    }

    // 累积距离
    const cumDist = (pts) => {
      const d = [0]
      for (let i = 1; i < pts.length; i++) {
        d.push(d[i - 1] + calcDistance(pts[i - 1].latitude, pts[i - 1].longitude, pts[i].latitude, pts[i].longitude))
      }
      return d
    }

    const distA = cumDist(segA)
    const distB = cumDist(segB)
    const totalA = distA[distA.length - 1]
    const totalB = distB[distB.length - 1]
    const mergeDist = Math.max(totalA, totalB) // 取较长者保证全覆盖

    // 沿距离插值
    const interpolate = (pts, cum, t) => {
      if (t <= 0) return { latitude: pts[0].latitude, longitude: pts[0].longitude }
      const last = cum.length - 1
      if (t >= cum[last]) return { latitude: pts[last].latitude, longitude: pts[last].longitude }
      for (let i = 0; i < last; i++) {
        if (cum[i] <= t && cum[i + 1] >= t) {
          const frac = (t - cum[i]) / (cum[i + 1] - cum[i] || 0.001)
          return {
            latitude: pts[i].latitude + (pts[i + 1].latitude - pts[i].latitude) * frac,
            longitude: pts[i].longitude + (pts[i + 1].longitude - pts[i].longitude) * frac
          }
        }
      }
      return { latitude: pts[last].latitude, longitude: pts[last].longitude }
    }

    // ~5米间隔重采样
    const interval = 5
    const n = Math.max(2, Math.ceil(mergeDist / interval))

    const merged = []
    for (let i = 0; i <= n; i++) {
      const d = (i / n) * mergeDist
      const ptA = interpolate(segA, distA, Math.min(d, totalA))
      const ptB = interpolate(segB, distB, Math.min(d, totalB))

      // 偏差过大时降低B的权重（防GPS跳点）
      const dev = calcDistance(ptA.latitude, ptA.longitude, ptB.latitude, ptB.longitude)
      let wB = 0.5
      if (dev > 20) wB = 0.2
      if (dev > 50) wB = 0.05

      merged.push({
        latitude: ptA.latitude * (1 - wB) + ptB.latitude * wB,
        longitude: ptA.longitude * (1 - wB) + ptB.longitude * wB
      })
    }

    return merged
  },

  // ========== 刷新地图标线 ==========
  _refreshPolylines() {
    const roadA = this.data.selectedRoadA
    const roadB = this.data.selectedRoadB

    const polylines = []
    const allPoints = []

    // 道路A — 红色
    if (roadA) {
      const pts = this._roadToPolylinePoints(roadA)
      if (pts.length >= 2) {
        polylines.push({
          points: pts,
          color: '#FF3B30',
          width: 5,
          borderColor: '#C0392B',
          borderWidth: 1,
          arrowLine: false,
          dottedLine: false
        })
        allPoints.push(...pts)
      }
    }

    // 道路B — 蓝色
    if (roadB) {
      const pts = this._roadToPolylinePoints(roadB)
      if (pts.length >= 2) {
        polylines.push({
          points: pts,
          color: '#007AFF',
          width: 5,
          borderColor: '#0055CC',
          borderWidth: 1,
          arrowLine: false,
          dottedLine: false
        })
        allPoints.push(...pts)
      }
    }

    // 合并路 — 绿色（最上层）
    if (this._mergedGcj && this._mergedGcj.length >= 2) {
      polylines.push({
        points: this._mergedGcj,
        color: '#34C759',
        width: 7,
        borderColor: '#1B8C38',
        borderWidth: 2,
        arrowLine: false,
        dottedLine: false
      })
      allPoints.push(...this._mergedGcj)
    }

    // 计算地图中心
    let mapCenter = this.data.mapCenter
    if (allPoints.length > 0) {
      let sumLat = 0, sumLng = 0
      allPoints.forEach(p => { sumLat += p.latitude; sumLng += p.longitude })
      mapCenter = { lat: sumLat / allPoints.length, lng: sumLng / allPoints.length }
    }

    this.setData({ polylines, mapCenter })

    // 自动缩放
    if (allPoints.length > 0) {
      setTimeout(() => {
        const mapCtx = wx.createMapContext('optimizeMap', this)
        mapCtx.includePoints({
          points: allPoints,
          padding: [80, 60, 80, 60]
        })
      }, 400)
    }
  },

  // ========== 道路坐标 → 地图标线点 ==========
  _roadToPolylinePoints(road) {
    const rawPoints = parseRoadPoints(road.points || '')
    if (rawPoints.length < 2) return []
    return rawPoints.map(p => {
      const gcj = wgs84ToGcj02(p.lng, p.lat)
      return { latitude: gcj.lat, longitude: gcj.lng }
    })
  },

  onRegionChange(e) {
    if (e.type === 'end' && e.detail && e.detail.scale) {
      this.setData({ scale: e.detail.scale })
    }
  },

  // ========== 提交合并道路到服务器 ==========
  onSubmitMerged() {
    if (!this._mergedWgs || this._mergedWgs.length < 2) {
      wx.showToast({ title: '暂无合并数据', icon: 'none' })
      return
    }

    const roadA = this.data.selectedRoadA
    const roadB = this.data.selectedRoadB
    const nameA = roadA ? (roadA.name || roadA.roadId || '未命名') : '未知路'
    const nameB = roadB ? (roadB.name || roadB.roadId || '未命名') : '未知路'
    const mergedName = nameA + '+' + nameB

    // 格式: lat,lng,lat,lng,...（WGS-84）
    const pointsStr = this._mergedWgs
      .map(p => p.lat.toFixed(6) + ',' + p.lng.toFixed(6))
      .join(',')

    wx.showLoading({ title: '提交中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      timeout: 15000,
      data: {
        action: 'addRoad',
        info: {
          route_id: 'rd_' + Date.now(),
          roadname: mergedName,
          roadinfo: pointsStr,
          level: '1',
          time: app.formatTime()
        }
      },
      success: (res) => {
        console.log('提交合并道路返回:', JSON.stringify(res.data))
        wx.showToast({ title: '提交成功！', icon: 'success', duration: 2000 })
        // 刷新缓存
        dataCache.refreshRoadList(() => {})
      },
      fail: (err) => {
        console.error('提交合并道路失败:', err)
        wx.showToast({ title: '提交失败，请重试', icon: 'error' })
      },
      complete: () => {
        wx.hideLoading()
      }
    })
  }
})
