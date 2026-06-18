// places/picker/picker.js - 地图选坐标
Page({
  data: {
    centerLat: 26.529950,
    centerLng: 109.390224,
    scale: 14,
    gpsText: '—'
  },

  onLoad() {
    // 尝试定位到用户当前位置
    const that = this
    wx.getLocation({
      type: 'gcj02',
      success: (res) => {
        that.setData({
          centerLat: res.latitude,
          centerLng: res.longitude
        })
        that._updateGpsText(res.longitude, res.latitude)
      },
      fail: () => {
        // 使用默认中心点
        that._updateGpsText(109.390224, 26.529950)
      }
    })
  },

  onRegionChange(e) {
    if (e.type !== 'end' || !e.detail || !e.detail.centerLocation) return
    const { longitude, latitude } = e.detail.centerLocation
    this._updateGpsText(longitude, latitude)
  },

  // GCJ-02 → WGS-84 显示 + 存储
  _updateGpsText(lng, lat) {
    const wgs = this._gcj02ToWgs84(lng, lat)
    const text = wgs.lat.toFixed(6) + ',' + wgs.lng.toFixed(6)
    this.setData({ gpsText: text })
    this._wgsCoords = wgs
  },

  onConfirm() {
    if (this._wgsCoords) {
      const wgs = this._wgsCoords
      getApp().globalData._placePickedGps = wgs.lat.toFixed(6) + ',' + wgs.lng.toFixed(6)
    }
    wx.navigateBack()
  },

  // ====== GCJ-02 → WGS-84 (逆转换) ======
  _gcj02ToWgs84(lng, lat) {
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
    return { lat: +lat - dLatFinal, lng: +lng - dLngFinal }
  }
})
