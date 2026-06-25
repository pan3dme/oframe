// road/detail/detail.js - 单条道路地图展示
const { wgs84ToGcj02, parseRoadPoints } = require('../../utils/coord-transform.js')

Page({
  data: {
    roadId: '',
    roadName: '道路详情',
    pointCount: 0,
    mapCenter: { lat: 26.529950, lng: 109.390224 },
    scale: 14,
    polylines: []
  },

  onLoad() {
    const item = getApp().globalData._roadDetailItem
    if (!item) {
      wx.showToast({ title: '无道路数据', icon: 'none' })
      setTimeout(() => wx.navigateBack(), 1500)
      return
    }

    const points = parseRoadPoints(item.points || '')
    if (points.length < 2) {
      wx.showToast({ title: '该道路无有效坐标', icon: 'none' })
      setTimeout(() => wx.navigateBack(), 1500)
      return
    }

    // WGS-84 → GCJ-02
    const gcjPoints = points.map(p => {
      const gcj = wgs84ToGcj02(p.lng, p.lat)
      return { latitude: gcj.lat, longitude: gcj.lng }
    })

    // 计算地图中心点（所有点的均值）
    let sumLat = 0, sumLng = 0
    gcjPoints.forEach(p => { sumLat += p.latitude; sumLng += p.longitude })
    const mapCenter = {
      lat: sumLat / gcjPoints.length,
      lng: sumLng / gcjPoints.length
    }

    this.setData({
      roadId: item.roadId || '',
      roadName: item.name || '未命名道路',
      pointCount: points.length,
      mapCenter,
      polylines: [{
        points: gcjPoints,
        color: '#C8C8C8',
        width: 4,
        borderColor: '#808080',
        borderWidth: 1.5,
        arrowLine: false,
        dottedLine: false
      }]
    })

    // 自动缩放到包含所有点
    setTimeout(() => {
      const mapCtx = wx.createMapContext('roadMap', this)
      mapCtx.includePoints({
        points: gcjPoints,
        padding: [60, 40, 60, 40]
      })
    }, 400)

    // 用完清除
    getApp().globalData._roadDetailItem = null
  },

  onRegionChange(e) {
    if (e.type === 'end' && e.detail && e.detail.scale) {
      this.setData({ scale: e.detail.scale })
    }
  },

})
