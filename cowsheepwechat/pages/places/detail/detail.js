// places/detail/detail.js - 单个地名地图展示（地名+附近设备叠加）
const { wgs84ToGcj02, calcDistance } = require('../../../utils/coord-transform.js')
const dataCache = require('../../../config/data-cache.js')

Page({
  data: {
    placeId: '',
    placeName: '地名详情',
    gpsText: '-',
    mapCenter: { lat: 26.529950, lng: 109.390224 },
    scale: 14,
    markers: [],
    isSatellite: true,
    deviceCount: 0
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
    const gcj = wgs84ToGcj02(coord.lng, coord.lat)
    const name = item.name || '未命名地名'

    this.setData({
      placeId: item.placeId || '',
      placeName: name,
      gpsText: coord.lat.toFixed(5) + ', ' + coord.lng.toFixed(5),
      mapCenter: { lat: gcj.lat, lng: gcj.lng },
      markers: [this._buildPlaceMarker(gcj, name)]
    }, () => {
      this._refreshOverlays(gcj.lat, gcj.lng, this.data.scale)
    })

    // 保存坐标和地名 marker（供 _loadNearbyDevices 复用）
    this._placeGcj = gcj
    this._placeMarker = this._buildPlaceMarker(gcj, name)
    this._loadNearbyDevices()

    // 用完清除
    getApp().globalData._placeDetailItem = null
  },

  // ===== 地名 marker：红色定位图钉 /images/place_pin.png =====
  _buildPlaceMarker(gcj, name) {
    return {
      id: 0,
      latitude: gcj.lat,
      longitude: gcj.lng,
      width: 30,
      height: 32,
      iconPath: '/images/place_pin.png',
      title: name,
      anchor: { x: 0.5, y: 1 },
      zIndex: 100,
      callout: {
        content: name,
        display: 'ALWAYS',
        textAlign: 'center',
        fontSize: 13,
        bgColor: '#ffffff',
        color: '#333333',
        borderColor: '#cccccc',
        borderWidth: 1,
        borderRadius: 6,
        padding: 6
      }
    }
  },

  // ===== 设备 marker：绿色圆形 + 白底气泡显示设备号（始终显示） =====
  _buildDeviceMarker(gcj, deviceId, rename, idx) {
    const tail = rename ? '\n(' + rename + ')' : ''
    return {
      id: idx,
      latitude: gcj.lat,
      longitude: gcj.lng,
      width: 28,
      height: 28,
      iconPath: '/images/device_pin.png',
      title: '设备: ' + deviceId,
      anchor: { x: 0.5, y: 0.5 },
      zIndex: 50,
      callout: {
        content: deviceId + tail,
        display: 'ALWAYS',
        textAlign: 'center',
        fontSize: 12,
        bgColor: '#ffffff',
        color: '#333333',
        borderColor: '#cccccc',
        borderWidth: 1,
        borderRadius: 6,
        padding: 6
      }
    }
  },

  // 加载附近设备：按距离过滤，加为 marker
  _loadNearbyDevices() {
    const that = this
    dataCache.getDeviceList((cached) => {
      const list = (cached && cached.recordList) || []
      const placeGcj = that._placeGcj
      // 地名 marker（设备图）+ 设备 marker
      const markers = that._placeMarker ? [that._placeMarker] : []
      let deviceCount = 0
      // 距离阈值（米）：50 km 内都显示
      const MAX_DIST = 50000

      list.forEach(function(d) {
        if (!d.visible) return
        if (!d.lorastr || d.lorastr === '-') return
        const c = that._parseSingleGPS(d.lorastr)
        if (!c) return
        // WGS-84 → GCJ-02
        const gcj = wgs84ToGcj02(c.lng, c.lat)
        const dist = calcDistance(placeGcj.lat, placeGcj.lng, gcj.lat, gcj.lng)
        if (dist > MAX_DIST) return
        deviceCount++
        markers.push(that._buildDeviceMarker(gcj, d.deviceId, d.rename, 100 + deviceCount))
      })

      that.setData({ markers, deviceCount })
    })
  },

  // ========== 高德瓦片叠加 ==========
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
    var mapCtx = wx.createMapContext('placeMap')
    if (!this._tileCache) this._tileCache = {}

    for (var oldKey in this._tileCache) {
      if (!newKeySet[oldKey]) {
        mapCtx.removeGroundOverlay({ id: this._tileCache[oldKey].id })
        delete this._tileCache[oldKey]
      }
    }

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
    var mapCtx = wx.createMapContext('placeMap')
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
    if (e.detail && e.detail.scale) {
      this.setData({ scale: e.detail.scale })
    }
    const mapCtx = wx.createMapContext('placeMap')
    const that = this
    mapCtx.getRegion({
      success: (region) => {
        const sw = region.southwest || {}
        const ne = region.northeast || {}
        const cLat = (parseFloat(sw.latitude) + parseFloat(ne.latitude)) / 2
        const cLng = (parseFloat(sw.longitude) + parseFloat(ne.longitude)) / 2
        if (e.causedBy === 'update') return
        mapCtx.getScale({
          success: function(res) { that._refreshOverlays(cLat, cLng, res.scale) },
          fail: function() { that._refreshOverlays(cLat, cLng, that.data.scale) }
        })
      }
    })
  },

  // ====== GPS 解析（单点） ======
  _parseSingleGPS(gps) {
    if (!gps || gps === '-') return null

    // 尝试 | 分隔（lorastr 可能是 crow_idx|lat|lng，取最后两段）
    let parts = gps.split(/[｜|]/)
    if (parts.length >= 2) {
      const tail = parts.slice(-2)
      const lat = parseFloat(tail[0])
      const lng = parseFloat(tail[1])
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

})
