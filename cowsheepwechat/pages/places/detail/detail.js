// places/detail/detail.js - 单个地名地图展示
Page({
  data: {
    placeId: '',
    placeName: '地名详情',
    gpsText: '-',
    mapCenter: { lat: 26.529950, lng: 109.390224 },
    scale: 14,
    markers: []
  },

  onLoad() {
    const item = getApp().globalData._placeDetailItem
    if (!item) {
      wx.showToast({ title: '无地名数据', icon: 'none' })
      setTimeout(() => wx.navigateBack(), 1500)
      return
    }

    const coord = this._parseSingleGPS(item.gps || '')
    if (!coord) {
      wx.showToast({ title: '该地名无有效坐标', icon: 'none' })
      setTimeout(() => wx.navigateBack(), 1500)
      return
    }

    // WGS-84 → GCJ-02
    const gcj = this._wgs84ToGcj02(coord.lng, coord.lat)
    const name = item.name || '未命名地名'

    this.setData({
      placeId: item.placeId || '',
      placeName: name,
      gpsText: coord.lat.toFixed(6) + ', ' + coord.lng.toFixed(6),
      mapCenter: { lat: gcj.lat, lng: gcj.lng },
      markers: [{
        id: 0,
        latitude: gcj.lat,
        longitude: gcj.lng,
        width: 30,
        height: 36,
        title: name,
        callout: {
          content: name + '\n' + coord.lat.toFixed(6) + ', ' + coord.lng.toFixed(6),
          display: 'ALWAYS',
          textAlign: 'center',
          bgColor: '#FFD600',
          color: '#333333',
          borderRadius: 8,
          padding: 8
        },
        label: {
          content: name,
          color: '#ffffff',
          fontSize: 14,
          anchorX: 0,
          anchorY: 5,
          textAlign: 'center'
        }
      }]
    })

    // 用完清除
    getApp().globalData._placeDetailItem = null
  },

  onRegionChange(e) {
    if (e.type === 'end' && e.detail && e.detail.scale) {
      this.setData({ scale: e.detail.scale })
    }
  },

  // ====== GPS 解析（单点） ======
  _parseSingleGPS(gps) {
    if (!gps || gps === '-') return null

    // 尝试 | 分隔
    let parts = gps.split(/[｜|]/)
    if (parts.length >= 2) {
      const lat = parseFloat(parts[0])
      const lng = parseFloat(parts[1])
      if (!isNaN(lat) && !isNaN(lng)) return { lat, lng }
    }

    // 尝试 , 分隔
    parts = gps.split(/[,，]\s*/)
    if (parts.length >= 2) {
      const lat = parseFloat(parts[0])
      const lng = parseFloat(parts[1])
      if (!isNaN(lat) && !isNaN(lng)) return { lat, lng }
    }

    return null
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
