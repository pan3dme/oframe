// places/picker/picker.js - 地图选坐标
const { gcj02ToWgs84 } = require('../../utils/coord-transform.js')

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
    const wgs = gcj02ToWgs84(lng, lat)
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

})
