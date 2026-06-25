// trackmap.js - 轨迹地图页（接收设备详情传入的GPS数据，红点+连线）
const { wgs84ToGcj02 } = require('../../utils/coord-transform.js')

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

  // ========== 高德瓦片叠加（按地图中心方式实现） ==========
  _latLngToTile(lat, lng, zoom) {
    const n = Math.pow(2, zoom)
    const x = Math.floor((lng + 180) / 360 * n)
    const latRad = lat * Math.PI / 180
    const y = Math.floor((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2 * n)
    return { x, y }
  },

  _tileToBounds(tx, ty, zoom) {
    const n = Math.pow(2, zoom)
    return {
      southwest: { longitude: tx / n * 360 - 180, latitude: Math.atan(Math.sinh(Math.PI * (1 - 2 * (ty + 1) / n))) * 180 / Math.PI },
      northeast: { longitude: (tx + 1) / n * 360 - 180, latitude: Math.atan(Math.sinh(Math.PI * (1 - 2 * ty / n))) * 180 / Math.PI }
    }
  },

  _buildAmapUrl(x, y, z) {
    const s = ((x + y) % 4) + 1
    return 'https://webst0' + s + '.is.autonavi.com/appmaptile?style=6&x=' + x + '&y=' + y + '&z=' + z
  },

  _tileOverlayId(EZ, x, y) {
    return 8000 + ((EZ * 1000000 + x * 10000 + y) % 98000)
  },

  _tileKey(EZ, x, y) {
    return EZ + '_' + x + '_' + y
  },

  _loadOverlayTile(lat, lng, zoom) {
    const EZ = Math.max(8, Math.min(16, Math.floor(zoom)))
    const center = this._latLngToTile(lat, lng, EZ)
    const range = EZ <= 9 ? 1 : (EZ <= 12 ? 2 : 3)
    const newTiles = []
    const newKeySet = {}
    for (var dx = -range; dx <= range; dx++) {
      for (var dy = -range; dy <= range; dy++) {
        var tx = center.x + dx
        var ty = center.y + dy
        var key = this._tileKey(EZ, tx, ty)
        newTiles.push({ x: tx, y: ty, key: key })
        newKeySet[key] = true
      }
    }

    this._refreshingTiles = true
    if (this._refreshTimeout) clearTimeout(this._refreshTimeout)
    this._refreshTimeout = setTimeout(() => { this._refreshingTiles = false }, 30000)

    var that = this
    var mapCtx = wx.createMapContext('trackMap')
    if (!this._tileCache) this._tileCache = {}

    // 删除过期瓦片
    var removed = 0
    for (var oldKey in this._tileCache) {
      if (!newKeySet[oldKey]) {
        mapCtx.removeGroundOverlay({ id: this._tileCache[oldKey].id })
        delete this._tileCache[oldKey]
        removed++
      }
    }

    // 过滤已缓存
    var toDownload = []
    newTiles.forEach(function(t) {
      if (!that._tileCache[t.key]) toDownload.push(t)
    })
    if (toDownload.length === 0) {
      that._refreshingTiles = false
      return
    }

    var loaded = 0
    var total = toDownload.length
    toDownload.forEach(function(t) {
      var bounds = that._tileToBounds(t.x, t.y, EZ)
      var url = that._buildAmapUrl(t.x, t.y, EZ)
      var overlayId = that._tileOverlayId(EZ, t.x, t.y)
      wx.downloadFile({
        url: url,
        success: function(res) {
          if (res.statusCode !== 200) { checkTileDone(); return }
          mapCtx.addGroundOverlay({
            id: overlayId,
            src: res.tempFilePath,
            bounds: {
              southwest: { longitude: bounds.southwest.longitude, latitude: bounds.southwest.latitude },
              northeast: { longitude: bounds.northeast.longitude, latitude: bounds.northeast.latitude }
            },
            opacity: 1,
            zIndex: 1000 + (t.x + t.y) % 100,
            success: function() { that._tileCache[t.key] = { id: overlayId, bounds: bounds } }
          })
          checkTileDone()
        },
        fail: function() { checkTileDone() }
      })
    })

    function checkTileDone() {
      loaded++
      if (loaded >= total) {
        that._refreshingTiles = false
        if (that._refreshTimeout) { clearTimeout(that._refreshTimeout); that._refreshTimeout = null }
      }
    }
  },

  _refreshOverlays(lat, lng, zoom) {
    const key = lat.toFixed(4) + ',' + lng.toFixed(4) + ',' + zoom
    if (this._lastOverlayKey === key) return
    this._lastOverlayKey = key
    if (zoom < 15) {
      this._clearAllOverlays()
      return
    }
    if (!this.data.isSatellite) {
      this.setData({ isSatellite: true })
    }
    this._loadOverlayTile(lat, lng, zoom)
  },

  _clearAllOverlays() {
    var mapCtx = wx.createMapContext('trackMap')
    if (this._tileCache) {
      for (var key in this._tileCache) {
        mapCtx.removeGroundOverlay({ id: this._tileCache[key].id })
      }
      this._tileCache = {}
    }
  },

  onRegionChange(e) {
    if (e.type !== 'end') return
    if (this._refreshingTiles) return
    const mapCtx = wx.createMapContext('trackMap')
    const isProgrammatic = e.causedBy === 'update'
    const that = this
    mapCtx.getRegion({
      success: (region) => {
        const sw = region.southwest || {}
        const ne = region.northeast || {}
        const cLat = (parseFloat(sw.latitude) + parseFloat(ne.latitude)) / 2
        const cLng = (parseFloat(sw.longitude) + parseFloat(ne.longitude)) / 2
        if (isProgrammatic) return
        mapCtx.getScale({
          success: function(res) { that._refreshOverlays(cLat, cLng, res.scale) },
          fail: function() { that._refreshOverlays(cLat, cLng, that.data.nativeScale) }
        })
      }
    })
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
          }, () => {
            that.renderMarkers()
            that._refreshOverlays(res.latitude, res.longitude, 15)
          })
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
          }, () => {
            that.renderMarkers()
            that._refreshOverlays(baseLat, baseLon, 15)
          })
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
      const gcj = wgs84ToGcj02(coord.lng, coord.lat)
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
      }, () => {
        this._refreshOverlays(markers[0].latitude, markers[0].longitude, this.data.nativeScale)
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
    // loadMap 内已调用 _refreshOverlays
  },

  onToolBtn2() {
    wx.showLoading({ title: '刷新中...' })
    this.setData({ markers: [], polyline: [] })
    this.renderMarkers()
    wx.hideLoading()
  }
})
