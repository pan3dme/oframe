// road-optimize.js - 道路优化（两条道路对比 + 合并）
const dataCache = require('../../config/data-cache.js')
const { wgs84ToGcj02, parseRoadPoints } = require('../../utils/coord-transform.js')

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

  // ========== 合并按钮（暂留） ==========
  onMerge() {
    if (!this.data.selectedRoadA || !this.data.selectedRoadB) {
      wx.showToast({ title: '请先选择两条道路', icon: 'none' })
      return
    }
    wx.showToast({ title: '合并功能开发中', icon: 'none' })
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
  }
})
