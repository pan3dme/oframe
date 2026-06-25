// road/record/record.js - 路径录制页
const { gcj02ToWgs84, calcDistance } = require('../../utils/coord-transform.js')
const app = getApp()

Page({
  data: {
    mapCenter: { lat: 26.529413, lng: 109.390511 },
    scale: 16,
    recording: false,
    stopped: false,
    pointCount: 0,
    elapsed: 0,
    totalDist: 0,
    polylines: [],
    markers: []
  },

  // ========== 生命周期 ==========
  onLoad() {
    this._recordedWgs = []        // WGS-84 坐标（用于返回）
    this._recordedGcj = []        // GCJ-02 坐标（用于地图显示）
    this._lastRecordLng = null
    this._lastRecordLat = null
    this._lastRecordTime = 0
    this._totalDist = 0
    this._recordingTimer = null
    this._elapsedTimer = null
    this._elapsedSec = 0
    this._lastLocation = null     // 最近一次定位

    // 定位到用户当前位置
    wx.getLocation({
      type: 'gcj02',
      success: (res) => {
        this.setData({
          mapCenter: { lat: res.latitude, lng: res.longitude }
        })
      },
      fail: () => {
        // 使用默认中心
      }
    })
  },

  onUnload() {
    this._stopTracking()
    this._clearTimers()
  },

  // ========== 地图缩放 ==========
  onRegionChange(e) {
    if (e.type === 'end' && e.detail && e.detail.scale) {
      this.setData({ scale: e.detail.scale })
    }
  },

  // ========== 开始记录 ==========
  onStartRecord() {
    const that = this

    // 重新获取一次精确定位作为起点
    wx.getLocation({
      type: 'gcj02',
      success: () => {}
    })

    // 启动持续定位
    wx.startLocationUpdate({
      success: () => {
        console.log('startLocationUpdate success')
      },
      fail: (err) => {
        console.error('startLocationUpdate fail:', err)
        wx.showToast({ title: '定位启动失败，请检查权限', icon: 'none' })
        return
      }
    })

    // 监听位置变化
    wx.onLocationChange((res) => {
      that._onLocationChange(res)
    })

    // 监听后台定位（如果切到后台）
    if (wx.offLocationChangeBackground) {
      wx.offLocationChangeBackground()
    }
    if (wx.onLocationChangeBackground) {
      wx.onLocationChangeBackground((res) => {
        that._onLocationChange(res)
      })
    }

    // 尝试启动后台定位
    wx.startLocationUpdateBackground && wx.startLocationUpdateBackground({
      success: () => console.log('background location started'),
      fail: () => console.log('background location not supported')
    })

    that.setData({ recording: true, stopped: false, pointCount: 0, elapsed: 0, totalDist: 0 })
    that._recordedWgs = []
    that._recordedGcj = []
    that._lastRecordLng = null
    that._lastRecordLat = null
    that._lastRecordTime = 0
    that._totalDist = 0
    that._elapsedSec = 0
    that._lastLocation = null

    // 计时器：每秒更新一次 UI（elapsed 和检查是否需要记录）
    that._elapsedTimer = setInterval(() => {
      that._elapsedSec++
      that.setData({ elapsed: that._elapsedSec })
      // 每3秒检查：如果与上次记录间隔 >= 3s 且有新位置，强制记录
      if (that._lastLocation && that.data.recording) {
        const now = Date.now()
        if (now - that._lastRecordTime >= 3000 && that._lastLocation !== that._lastRecordedRef) {
          that._recordPoint(that._lastLocation.latitude, that._lastLocation.longitude, now)
          that._lastRecordedRef = that._lastLocation
        }
      }
    }, 1000)

    wx.vibrateShort({ type: 'medium' })
  },

  // ========== 停止记录 ==========
  onStopRecord() {
    this._stopTracking()
    this._clearTimers()
    this.setData({
      recording: false,
      stopped: true,
      totalDist: Math.round(this._totalDist)
    })
    wx.vibrateShort({ type: 'medium' })
  },

  // ========== 重新开始 ==========
  onRestart() {
    // 清除数据
    this._recordedWgs = []
    this._recordedGcj = []
    this._lastRecordLng = null
    this._lastRecordLat = null
    this._lastRecordTime = 0
    this._totalDist = 0
    this._elapsedSec = 0
    this._lastLocation = null
    this._lastRecordedRef = null
    this.setData({
      stopped: false,
      recording: false,
      pointCount: 0,
      elapsed: 0,
      totalDist: 0,
      polylines: [],
      markers: []
    })
  },

  // ========== 确定返回 ==========
  onConfirm() {
    if (this._recordedWgs.length < 2) {
      wx.showToast({ title: '至少需要2个坐标点', icon: 'none' })
      return
    }

    // 格式: lat,lng,lat,lng,...（WGS-84）
    const pointsStr = this._recordedWgs
      .map(p => p.lat.toFixed(6) + ',' + p.lng.toFixed(6))
      .join(',')

    app.globalData._roadRecordedPath = pointsStr
    wx.navigateBack()
  },

  // ========== GPS 回调 ==========
  _onLocationChange(res) {
    if (!res || !res.latitude || !res.longitude) return

    const gcjLat = res.latitude
    const gcjLng = res.longitude
    const now = Date.now()

    // 保存最新位置引用
    const locRef = { latitude: gcjLat, longitude: gcjLng }
    this._lastLocation = locRef

    // 更新地图中心到当前位置
    this.setData({
      mapCenter: { lat: gcjLat, lng: gcjLng }
    })

    // 第一个点，直接记录
    if (this._lastRecordLat === null) {
      this._recordPoint(gcjLat, gcjLng, now)
      this._lastRecordedRef = locRef
      return
    }

    // 计算距离（米）
    const dist = calcDistance(
      this._lastRecordLat, this._lastRecordLng,
      gcjLat, gcjLng
    )

    // 移动超过 10 米就记录
    if (dist >= 10) {
      this._recordPoint(gcjLat, gcjLng, now)
      this._lastRecordedRef = locRef
    }
  },

  // ========== 记录一个坐标点 ==========
  _recordPoint(gcjLat, gcjLng, timestamp) {
    // 转换为 WGS-84
    const wgs = gcj02ToWgs84(gcjLng, gcjLat)

    this._recordedGcj.push({ latitude: gcjLat, longitude: gcjLng })
    this._recordedWgs.push({ lat: wgs.lat, lng: wgs.lng })

    // 更新总距离
    if (this._recordedWgs.length >= 2) {
      const prev = this._recordedWgs[this._recordedWgs.length - 2]
      this._totalDist += calcDistance(
        prev.lat, prev.lng,
        wgs.lat, wgs.lng
      )
    }

    this._lastRecordLat = gcjLat
    this._lastRecordLng = gcjLng
    this._lastRecordTime = timestamp

    // 更新 UI
    this._updatePolyline()
    this.setData({ pointCount: this._recordedGcj.length })

    // 更新起终点标记
    this._updateMarkers()
  },

  // ========== 更新地图折线 ==========
  _updatePolyline() {
    if (this._recordedGcj.length < 2) {
      this.setData({ polylines: [] })
      return
    }
    this.setData({
      polylines: [{
        points: this._recordedGcj,
        color: '#FF4444DD',
        width: 6,
        borderColor: '#990000',
        borderWidth: 2,
        arrowLine: false,
        dottedLine: false
      }]
    })
  },

  // ========== 更新起终点标记 ==========
  _updateMarkers() {
    const markers = []
    if (this._recordedGcj.length > 0) {
      const start = this._recordedGcj[0]
      markers.push({
        id: 1,
        latitude: start.latitude,
        longitude: start.longitude,
        width: 32,
        height: 32,
        callout: { content: '起点', fontSize: 12, borderRadius: 8, padding: 4, display: 'ALWAYS' }
      })
    }
    if (this._recordedGcj.length > 1) {
      const end = this._recordedGcj[this._recordedGcj.length - 1]
      markers.push({
        id: 2,
        latitude: end.latitude,
        longitude: end.longitude,
        width: 32,
        height: 32,
        callout: { content: '终点', fontSize: 12, borderRadius: 8, padding: 4, display: 'ALWAYS' }
      })
    }
    this.setData({ markers })
  },

  // ========== 停止 GPS 追踪 ==========
  _stopTracking() {
    wx.stopLocationUpdate && wx.stopLocationUpdate()
    wx.offLocationChange && wx.offLocationChange()
    // 停止后台定位
    if (wx.stopLocationUpdateBackground) {
      try { wx.stopLocationUpdateBackground() } catch (e) {}
    }
    if (wx.offLocationChangeBackground) {
      try { wx.offLocationChangeBackground() } catch (e) {}
    }
  },

  // ========== 清除计时器 ==========
  _clearTimers() {
    if (this._elapsedTimer) {
      clearInterval(this._elapsedTimer)
      this._elapsedTimer = null
    }
  },

})
