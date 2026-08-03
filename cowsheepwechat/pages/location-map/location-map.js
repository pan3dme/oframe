// location-map.js - 单点定位地图页（从设备详情定位记录跳转过来）
const { wgs84ToGcj02 } = require('../../utils/coord-transform.js')

Page({
  data: {
    nativeLat: 26.529950,
    nativeLng: 109.390224,
    nativeScale: 17,
    markers: [],
    isSatellite: true,
    // 定位记录信息
    deviceId: '',
    recordTime: '',
    lorastr: '',
    upDateDevice: '',
    originLat: '',
    originLng: ''
  },

  onLoad(options) {
    const lat = parseFloat(options.lat)
    const lng = parseFloat(options.lng)
    const deviceId = decodeURIComponent(options.deviceId || '')
    const recordTime = decodeURIComponent(options.time || '')
    const lorastr = decodeURIComponent(options.lorastr || '')
    const upDateDevice = decodeURIComponent(options.upDateDevice || '')

    if (!isNaN(lat) && !isNaN(lng)) {
      // WGS-84 → GCJ-02 转换
      const gcj = wgs84ToGcj02(lng, lat)
      this.setData({
        nativeLat: gcj.lat,
        nativeLng: gcj.lng,
        deviceId,
        recordTime,
        lorastr,
        upDateDevice,
        originLat: lat.toFixed(6),
        originLng: lng.toFixed(6)
      })
      this.renderMarker(gcj.lat, gcj.lng, lat, lng)
      this._refreshOverlays(gcj.lat, gcj.lng, 17)
    } else {
      wx.showToast({ title: '坐标无效', icon: 'none' })
    }
  },

  renderMarker(gcjLat, gcjLng, wgsLat, wgsLng) {
    const { deviceId, recordTime, upDateDevice } = this.data
    const labelText = deviceId ? deviceId.substring(0, 10) : '定位点'

    const markers = [{
      id: 0,
      latitude: gcjLat,
      longitude: gcjLng,
      width: 36,
      height: 36,
      iconPath: '',
      title: '设备: ' + (deviceId || '-'),
      callout: {
        content: '设备:' + (deviceId || '-') +
          (upDateDevice ? '\n上传:' + upDateDevice : '') +
          '\nWGS84: ' + wgsLat + ', ' + wgsLng +
          '\n时间:' + (recordTime || '-'),
        display: 'ALWAYS',
        textAlign: 'center',
        fontSize: 13,
        padding: 10,
        borderRadius: 8
      },
      label: {
        content: labelText,
        color: '#ffffff',
        fontSize: 13,
        bgColor: '#E53935',
        borderRadius: 6,
        padding: 6,
        anchorX: 0,
        anchorY: 3,
        textAlign: 'center'
      },
      // 红色圆点标记
      anchor: { x: 0.5, y: 0.5 }
    }]

    this.setData({ markers })
  },

  // ==================== 高德瓦片叠加 ====================
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
    return 7000 + ((EZ * 1000000 + x * 10000 + y) % 98000)
  },

  _tileKey(EZ, x, y) {
    return EZ + '_' + x + '_' + y
  },

  // 加载多片高德卫星瓦片覆盖可见区域，增量更新
  // 支持无限放大：EZ 钳在 8~16，超出 16 级时自动用 16 级瓦片放大替代
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

    console.log('[overlay] 中心 GCJ-02:', lat.toFixed(4), lng.toFixed(4), ' zoom:', zoom, ' EZ:', EZ,
      ' 瓦片:', newTiles.length, '片')

    // 加锁
    this._refreshingTiles = true
    if (this._refreshTimeout) clearTimeout(this._refreshTimeout)
    this._refreshTimeout = setTimeout(() => { this._refreshingTiles = false }, 30000)

    var that = this
    var mapCtx = wx.createMapContext('locMap')

    // 初始化瓦片缓存
    if (!this._tileCache) this._tileCache = {}

    // 1. 删除不在新范围内的旧瓦片
    var removed = 0
    for (var oldKey in this._tileCache) {
      if (!newKeySet[oldKey]) {
        mapCtx.removeGroundOverlay({ id: this._tileCache[oldKey].id })
        delete this._tileCache[oldKey]
        removed++
      }
    }
    if (removed > 0) console.log('[overlay] 移除过期瓦片:', removed, '个，保留:', Object.keys(this._tileCache).length, '个')

    // 2. 新瓦片中过滤掉已缓存的
    var toDownload = []
    newTiles.forEach(function(t) {
      if (!that._tileCache[t.key]) toDownload.push(t)
    })
    if (toDownload.length === 0) {
      // 全部命中缓存，直接解锁
      console.log('[overlay] 全部瓦片已缓存，无需下载')
      that._refreshingTiles = false
      return
    }

    console.log('[overlay] 需下载:', toDownload.length, '片（缓存命中:', (newTiles.length - toDownload.length), '片）')

    var loaded = 0
    var total = toDownload.length

    toDownload.forEach(function(t) {
      var bounds = that._tileToBounds(t.x, t.y, EZ)
      var url = that._buildAmapUrl(t.x, t.y, EZ)
      var overlayId = that._tileOverlayId(EZ, t.x, t.y)

      wx.downloadFile({
        url: url,
        success: function(res) {
          if (res.statusCode !== 200) {
            console.log('[overlay] 瓦片', t.key, 'HTTP', res.statusCode)
            checkTileDone()
            return
          }
          mapCtx.addGroundOverlay({
            id: overlayId,
            src: res.tempFilePath,
            bounds: {
              southwest: { longitude: bounds.southwest.longitude, latitude: bounds.southwest.latitude },
              northeast: { longitude: bounds.northeast.longitude, latitude: bounds.northeast.latitude }
            },
            opacity: 1,
            zIndex: 1000 + (t.x + t.y) % 100,
            success: function() {
              that._tileCache[t.key] = { id: overlayId, bounds: bounds }
            },
            fail: function(err) {
              console.log('[overlay] 瓦片', t.key, 'addGroundOverlay 失败:', JSON.stringify(err))
            }
          })
          checkTileDone()
        },
        fail: function(err) {
          console.log('[overlay] 瓦片', t.key, '下载失败:', JSON.stringify(err))
          checkTileDone()
        }
      })
    })

    function checkTileDone() {
      loaded++
      if (loaded < total) return

      that._refreshingTiles = false
      if (that._refreshTimeout) { clearTimeout(that._refreshTimeout); that._refreshTimeout = null }
      var cached = Object.keys(that._tileCache).length
      console.log('[overlay] 完成，缓存瓦片:', cached)
    }
  },

  _refreshOverlays(lat, lng, zoom) {
    const key = lat.toFixed(4) + ',' + lng.toFixed(4) + ',' + zoom
    if (this._lastOverlayKey === key) return
    this._lastOverlayKey = key

    const SATELLITE_THRESHOLD = 15  // 15级以上才覆盖高德卫星瓦片

    if (zoom < SATELLITE_THRESHOLD) {
      // 低于15级：清除高德瓦片，仅保留腾讯卫星底图
      this._clearAllOverlays()
      console.log('[overlay] 缩放' + zoom + ' < ' + SATELLITE_THRESHOLD + '，仅腾讯卫星底图')
      return
    }

    // 15级以上：腾讯卫星底图 + 高德瓦片覆盖（超过16级自动用16级瓦片放大）
    if (!this.data.isSatellite) {
      this.setData({ isSatellite: true })
    }
    this._loadOverlayTile(lat, lng, zoom)
  },

  // 清除所有 ground overlay
  _clearAllOverlays() {
    var mapCtx = wx.createMapContext('locMap')
    // 清除瓦片缓存
    if (this._tileCache) {
      for (var key in this._tileCache) {
        mapCtx.removeGroundOverlay({ id: this._tileCache[key].id })
      }
      this._tileCache = {}
    }
    console.log('[overlay] 清除所有瓦片')
  },

  // 手势缩放/拖动 → 刷新瓦片
  onRegionChange(e) {
    if (e.type !== 'end') return
    if (this._refreshingTiles) {
      console.log('[overlay] regionChange 被忽略（瓦片加载中）')
      return
    }
    const mapCtx = wx.createMapContext('locMap')
    const isProgrammatic = e.causedBy === 'update'
    const that = this

    mapCtx.getRegion({
      success: (region) => {
        const sw = region.southwest || {}
        const ne = region.northeast || {}
        const cLat = (parseFloat(sw.latitude) + parseFloat(ne.latitude)) / 2
        const cLng = (parseFloat(sw.longitude) + parseFloat(ne.longitude)) / 2

        const doRefresh = function(newScale) {
          console.log('[overlay] regionChange 刷新瓦片: lat=', cLat.toFixed(4), 'lng=', cLng.toFixed(4), 'scale=', newScale)
          that._refreshOverlays(cLat, cLng, newScale)
        }

        if (isProgrammatic) return
        mapCtx.getScale({
          success: function(res) { doRefresh(res.scale) },
          fail: function() { doRefresh(that.data.nativeScale) }
        })
      }
    })
  },

  // 回到定位点
  moveToPoint() {
    const mapCtx = wx.createMapContext('locMap')
    mapCtx.moveToLocation({
      latitude: this.data.nativeLat,
      longitude: this.data.nativeLng
    })
    wx.showToast({ title: '已回到定位点', icon: 'success', duration: 1000 })
  }
})
