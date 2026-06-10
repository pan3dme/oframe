// trackmap.js - 轨迹地图页（接收设备详情传入的GPS数据，红点+连线）
Page({
  data: {
    scale: 1,
    baiduMapUrl: '',
    coords: { lng: 109.390224, lat: 26.529950 },
    baiduAK: '',  // ⚠️ 填入百度 AK
    baiduZoom: 16,
    zoomRetries: 0,
    useBaidu: false,
    showNativeMap: false,
    nativeLat: 26.529950,
    nativeLng: 109.390224,
    nativeScale: 15,
    isSatellite: true,
    markers: [],
    polyline: []
  },

  onLoad() {
    const app = getApp()
    const trackData = app.globalData.trackData
    if (trackData && trackData.length > 0) {
      console.log('[轨迹地图] 收到数据:', trackData.length, '条')
      this._trackData = trackData
    } else {
      console.log('[轨迹地图] 暂无轨迹数据')
      this._trackData = null
    }
    app.globalData.trackData = null
    this.loadMap()
  },

  // ==================== GPS 坐标提取（兼容多种格式） ====================

  _extractCoord(item) {
    // 1) 优先用 gps 字段: lat|lng 或 lat,lng
    if (item.gps && item.gps !== '-') {
      const parts = item.gps.split(/[｜|,，]\s*/)
      if (parts.length >= 2) {
        const lat = parseFloat(parts[0])
        const lng = parseFloat(parts[1])
        if (!isNaN(lat) && !isNaN(lng)) return { lat, lng }
      }
    }
    // 2) 回退：从 lorastr 提取第3段
    if (item.lorastr) {
      const segs = item.lorastr.split(/[｜|]/)
      if (segs.length >= 3 && segs[2]) {
        const parts = segs[2].split(/[,，]\s*/)
        if (parts.length >= 2) {
          const lat = parseFloat(parts[0])
          const lng = parseFloat(parts[1])
          if (!isNaN(lat) && !isNaN(lng)) return { lat, lng }
        }
      }
    }
    return null
  },

  // ==================== 坐标转换 ====================

  gcjToBd(lng, lat) {
    const x = +lng, y = +lat
    const z = Math.sqrt(x * x + y * y) + 0.00002 * Math.sin(y * Math.PI)
    const theta = Math.atan2(y, x) + 0.000003 * Math.cos(x * Math.PI)
    return {
      lng: (z * Math.cos(theta) + 0.0065).toFixed(6),
      lat: (z * Math.sin(theta) + 0.006).toFixed(6)
    }
  },

  wgs84ToGcj02(lng, lat) {
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
  },

  // ==================== 加载地图 ====================

  buildBaiduUrl(lng, lat, zoom) {
    const { baiduAK } = this.data
    if (!baiduAK) return ''
    const info = wx.getSystemInfoSync()
    const size = Math.max(info.windowWidth, info.windowHeight)
    const safeZoom = Math.min(zoom, 16)
    return 'https://api.map.baidu.com/staticimage/v2' +
      '?ak=' + baiduAK +
      '&center=' + lng + ',' + lat +
      '&width=' + Math.round(size) +
      '&height=' + Math.round(size) +
      '&zoom=' + safeZoom +
      '&maptype=satellite' +
      '&scale=2'
  },

  loadMap() {
    const that = this
    wx.getLocation({
      type: 'gcj02',
      success: (res) => {
        const lat = res.latitude.toFixed(6)
        const lng = res.longitude.toFixed(6)
        if (that.data.baiduAK) {
          const bd = that.gcjToBd(lng, lat)
          that.setData({
            coords: bd,
            baiduMapUrl: that.buildBaiduUrl(bd.lng, bd.lat, 16),
            baiduZoom: 16,
            zoomRetries: 0,
            useBaidu: true,
            showNativeMap: false
          })
        } else {
          that.setData({
            nativeLat: res.latitude,
            nativeLng: res.longitude,
            nativeScale: 15,
            showNativeMap: true
          }, () => { that.renderMarkers() })
        }
      },
      fail: () => {
        const baseLat = 26.529950
        const baseLon = 109.390224
        if (that.data.baiduAK) {
          const bd = that.gcjToBd(baseLon, baseLat)
          that.setData({
            coords: bd,
            baiduMapUrl: that.buildBaiduUrl(bd.lng, bd.lat, 14),
            useBaidu: true,
            showNativeMap: false
          })
        } else {
          that.setData({
            nativeLat: baseLat,
            nativeLng: baseLon,
            showNativeMap: true
          }, () => { that.renderMarkers() })
        }
      }
    })
  },

  // ==================== 渲染红点标记 + 连线 ====================

  renderMarkers() {
    const trackData = this._trackData
    if (!trackData || trackData.length === 0) {
      console.log('[轨迹地图] 无轨迹数据')
      return
    }

    const markers = []
    trackData.forEach((item, index) => {
      const coord = this._extractCoord(item)
      if (!coord) return
      const gcj = this.wgs84ToGcj02(coord.lng, coord.lat)
      const labelText = (index + 1) + ''
      markers.push({
        id: index,
        latitude: gcj.lat,
        longitude: gcj.lng,
        width: 30,
        height: 30,
        title: item.crow_id || item.deviceId || ('点' + (index + 1)),
        callout: {
          content: (item.crow_id ? 'ID:' + item.crow_id : '设备:' + (item.deviceId || '-')) +
            '\nGPS:' + coord.lat + ',' + coord.lng,
          display: 'BYCLICK',
          textAlign: 'center'
        },
        label: {
          content: labelText,
          color: '#ffffff',
          fontSize: 13,
          anchorX: 0,
          anchorY: 3,
          textAlign: 'center'
        }
      })
    })

    // 按顺序连线
    const points = markers.map(m => ({
      latitude: m.latitude,
      longitude: m.longitude
    }))
    const polyline = points.length >= 2 ? [{
      points,
      color: '#FF4444CC',
      width: 3,
      arrowLine: true
    }] : []

    console.log('[轨迹地图] 红点:', markers.length, '个, 连线:', polyline.length, '条')

    if (markers.length > 0) {
      this.setData({
        markers,
        polyline,
        nativeLat: markers[0].latitude,
        nativeLng: markers[0].longitude
      })
    } else {
      this.setData({ markers, polyline })
    }
  },

  // ==================== 交互 ====================

  onScale(e) {
    this.setData({ scale: e.detail.scale })
  },

  onImageError() {
    const { baiduZoom, zoomRetries, coords } = this.data
    const fallbackZoom = baiduZoom - 2
    if (zoomRetries < 3 && fallbackZoom >= 4) {
      this.setData({
        baiduZoom: fallbackZoom,
        zoomRetries: zoomRetries + 1,
        baiduMapUrl: this.buildBaiduUrl(coords.lng, coords.lat, fallbackZoom)
      })
      wx.showToast({ title: '卫星图降级 (zoom ' + fallbackZoom + ')', icon: 'none', duration: 1200 })
      return
    }
    wx.showToast({ title: '卫星图加载失败', icon: 'none' })
  },

  moveToMyLocation() {
    this.loadMap()
  },

  onToolBtn2() {
    wx.showLoading({ title: '刷新中...' })
    this.setData({ markers: [], polyline: [] })
    this.renderMarkers()
    wx.hideLoading()
  }
})
