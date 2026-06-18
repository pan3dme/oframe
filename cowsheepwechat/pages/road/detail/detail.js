// road/detail/detail.js - 单条道路地图展示
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

    const points = this._parseRoadPoints(item.points || '')
    if (points.length < 2) {
      wx.showToast({ title: '该道路无有效坐标', icon: 'none' })
      setTimeout(() => wx.navigateBack(), 1500)
      return
    }

    // WGS-84 → GCJ-02
    const gcjPoints = points.map(p => {
      const gcj = this._wgs84ToGcj02(p.lng, p.lat)
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

  // ====== GPS 解析（与 map 页一致） ======

  _parseRoadPoints(roadinfo) {
    if (!roadinfo || roadinfo === '-') return []

    // Strategy 0: flat lat, lng, lat, lng, ... (逗号交替平铺)
    const commaAll = roadinfo.split(/[,，]\s*/)
    if (commaAll.length >= 4 && commaAll.length % 2 === 0) {
      const points = []
      let allValid = true
      for (let i = 0; i + 1 < commaAll.length; i += 2) {
        const lat = parseFloat(commaAll[i])
        const lng = parseFloat(commaAll[i + 1])
        if (isNaN(lat) || isNaN(lng)) { allValid = false; break }
        points.push({ lat, lng })
      }
      if (allValid && points.length >= 2) return points
    }

    // Strategy 1: split by | → for each segment try lat,lng
    const segs = roadinfo.split(/[｜|]/)
    if (segs.length >= 2) {
      const points = []
      for (const seg of segs) {
        const parts = seg.split(/[,，]\s*/)
        if (parts.length >= 2) {
          const lat = parseFloat(parts[0])
          const lng = parseFloat(parts[1])
          if (!isNaN(lat) && !isNaN(lng)) {
            points.push({ lat, lng })
          }
        }
      }
      if (points.length >= 2) return points
    }

    // Strategy 2: split by | → alternating lat|lng|lat|lng...
    if (segs.length >= 4) {
      const points = []
      for (let i = 0; i + 1 < segs.length; i += 2) {
        const lat = parseFloat(segs[i])
        const lng = parseFloat(segs[i + 1])
        if (!isNaN(lat) && !isNaN(lng)) {
          points.push({ lat, lng })
        }
      }
      if (points.length >= 2) return points
    }

    // Strategy 3: split by ; for point groups
    const semicolonSegs = roadinfo.split(';')
    if (semicolonSegs.length >= 2) {
      const points = []
      for (const seg of semicolonSegs) {
        const parts = seg.split(/[,，]\s*/)
        if (parts.length >= 2) {
          const lat = parseFloat(parts[0])
          const lng = parseFloat(parts[1])
          if (!isNaN(lat) && !isNaN(lng)) {
            points.push({ lat, lng })
          }
        }
      }
      if (points.length >= 2) return points
    }

    return []
  },

  // ====== WGS-84 → GCJ-02 ======

  _wgs84ToGcj02(lng, lat) {
    const a = 6378245.0
    const ee = 0.00669342162296594323
    const x = +lng - 105.0
    const y = +lat - 35.0
    let dLat = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * Math.sqrt(Math.abs(x))
    dLat += (20.0 * Math.sin(6.0 * x * Math.PI) + 20.0 * Math.sin(2.0 * x * Math.PI)) * 2.0 / 3.0
    dLat += (20.0 * Math.sin(y * Math.PI) + 40.0 * Math.sin(y / 3.0 * Math.PI)) * 2.0 / 3.0
    dLat += (160.0 * Math.sin(y / 12.0 * Math.PI) + 320.0 * Math.sin(y * Math.PI / 30.0)) * 2.0 / 3.0
    let dLng = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * Math.sqrt(Math.abs(x))
    dLng += (20.0 * Math.sin(6.0 * x * Math.PI) + 20.0 * Math.sin(2.0 * x * Math.PI)) * 2.0 / 3.0
    dLng += (20.0 * Math.sin(x * Math.PI) + 40.0 * Math.sin(x / 3.0 * Math.PI)) * 2.0 / 3.0
    dLng += (150.0 * Math.sin(x / 12.0 * Math.PI) + 300.0 * Math.sin(x / 30.0 * Math.PI)) * 2.0 / 3.0
    const radLat = +lat / 180.0 * Math.PI
    let magic = Math.sin(radLat)
    magic = 1 - ee * magic * magic
    const sqrtMagic = Math.sqrt(magic)
    const dLatFinal = (dLat * 180.0) / ((a * (1 - ee)) / (magic * sqrtMagic) * Math.PI)
    const dLngFinal = (dLng * 180.0) / (a / sqrtMagic * Math.cos(radLat) * Math.PI)
    return { lat: +lat + dLatFinal, lng: +lng + dLngFinal }
  }
})
