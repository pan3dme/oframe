// trackmap.js - 轨迹地图页（接收设备详情传入的GPS数据，红点+连线）
const { wgs84ToGcj02, parseRoadPoints } = require('../../utils/coord-transform.js')
const dataCache = require('../../config/data-cache.js')

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
    polylines: [],
    // 图层切换
    showRoadLayer: false,
    currentLevel: 0,
    maxLevel: 1,
    layerLabel: '图层'
  },

  onLoad(options) {
    const deviceId = options.deviceId || ''
    if (!deviceId) {
      console.warn('[轨迹地图] 缺少 deviceId')
      this._trackData = null
      this.loadMap()
      return
    }
    this._deviceId = deviceId
    this.fetchTrackData()
  },

  // 从网络请求设备GPS轨迹记录（最近99条）
  fetchTrackData() {
    const app = getApp()
    const deviceId = this._deviceId
    const now = new Date()
    const curdate = now.getFullYear() + '-' +
      String(now.getMonth() + 1).padStart(2, '0') + '-' +
      String(now.getDate()).padStart(2, '0')

    wx.showLoading({ title: '加载轨迹...' })
    console.log('[轨迹地图] 请求设备GPS轨迹, deviceId:', deviceId)
    wx.request({
      url: app.globalData.api_device_Url,
      method: 'POST',
      data: {
        action: 'getDeviceLogGpsbyId',
        info: {
          deviceId: deviceId,
          limit: 99,
          curdate: curdate
        },
        time: app.formatTime(now)
      },
      timeout: 10000,
      success: (res) => {
        wx.hideLoading()
        const data = res.data
        let rawList = []
        if (data && data.data && Array.isArray(data.data)) {
          rawList = data.data
        } else if (Array.isArray(data)) {
          rawList = data
        }

        const recordList = rawList.map(record => {
          const attr = {}
          if (record.attributes) {
            record.attributes.forEach(item => {
              attr[item.columnName] = item.columnValue
            })
          }
          if (record.primaryKey) {
            record.primaryKey.forEach(item => {
              attr[item.name] = item.value
            })
          }
          const gps = attr.gps || record.gps || '-'
          const time = attr.time || record.time || '-'
          const deviceIdVal = attr.deviceId || record.deviceId || this._deviceId
          const lorastr = attr.lorastr || record.lorastr || ''
          return { gps, time, deviceId: deviceIdVal, lorastr }
        })
        console.log('[轨迹地图] 网络获取:', recordList.length, '条GPS记录')
        if (recordList.length > 0) {
          this._trackData = recordList
        } else {
          this._trackData = null
          wx.showToast({ title: '暂无轨迹记录', icon: 'none' })
        }
        this.loadMap()
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('[轨迹地图] 请求失败:', JSON.stringify(err))
        wx.showToast({ title: '轨迹数据加载失败', icon: 'none' })
        this.loadMap()
      }
    })
  },

  // ==================== GPS 坐标提取（兼容多种格式） ====================

  // 判断坐标是否有效（非 NaN 且非 0,0）
  _isValidCoord(lat, lng) {
    if (isNaN(lat) || isNaN(lng)) return false
    // 排除无效定位 (0,0)
    if (lat === 0 && lng === 0) return false
    return true
  },

  _extractCoord(item) {
    // 1) 优先用 gps 字段: lat|lng 或 lat,lng
    if (item.gps && item.gps !== '-') {
      const parts = item.gps.split(/[｜|,，]\s*/)
      if (parts.length >= 2) {
        const lat = parseFloat(parts[0])
        const lng = parseFloat(parts[1])
        if (this._isValidCoord(lat, lng)) return { lat, lng }
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
          if (this._isValidCoord(lat, lng)) return { lat, lng }
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

  // 两点间近似距离（单位：度）
  _coordDist(a, b) {
    const dlat = a.lat - b.lat
    const dlng = a.lng - b.lng
    return Math.sqrt(dlat * dlat + dlng * dlng)
  },

  // 稀化坐标：去除与前后都极近的中间点，使地图更清晰
  _thinCoords(coords, threshold) {
    if (coords.length <= 2) return coords
    const result = [coords[0]]
    for (let i = 1; i < coords.length - 1; i++) {
      const dPrev = this._coordDist(coords[i], coords[i - 1])
      const dNext = this._coordDist(coords[i], coords[i + 1])
      if (dPrev < threshold && dNext < threshold) continue
      result.push(coords[i])
    }
    result.push(coords[coords.length - 1])
    return result
  },

  renderMarkers() {
    const trackData = this._trackData
    if (!trackData || trackData.length === 0) {
      console.log('[轨迹地图] 无轨迹数据')
      return
    }

    // 1) 提取全部有效坐标（WGS84 → GCJ02）
    const rawCoords = []
    trackData.forEach((item) => {
      const coord = this._extractCoord(item)
      if (!coord) return
      const gcj = wgs84ToGcj02(coord.lng, coord.lat)
      rawCoords.push({ gcj, coord, item })
    })
    if (rawCoords.length === 0) {
      console.log('[轨迹地图] 无有效坐标')
      return
    }

    // 2) 稀化：剔除与前后都极近的点（阈值约15米 ≈ 0.00015度）
    const THRESHOLD = 0.00055
    const thinGcj = this._thinCoords(rawCoords.map(c => c.gcj), THRESHOLD)
    // 用 hash 匹配回原始数据
    const thinSet = new Set(thinGcj.map(p => p.lat.toFixed(8) + ',' + p.lng.toFixed(8)))
    const filtered = rawCoords.filter(c =>
      thinSet.has(c.gcj.lat.toFixed(8) + ',' + c.gcj.lng.toFixed(8))
    )

    const skipped = rawCoords.length - filtered.length
    if (skipped > 0) {
      console.log('[轨迹地图] 稀化跳过', skipped, '个极近点')
    }

    // 3) 构建标记
    const markers = []
    filtered.forEach((row, index) => {
      const { gcj, coord, item } = row
      markers.push({
        id: index,
        latitude: gcj.lat,
        longitude: gcj.lng,
        width: 20,
        height: 20,
        title: item.crow_id || item.deviceId || ('点' + (index + 1)),
        callout: {
          content: (item.crow_id ? 'ID:' + item.crow_id : '设备:' + (item.deviceId || '-')) +
            '\nGPS:' + coord.lat + ',' + coord.lng,
          display: 'BYCLICK',
          textAlign: 'center'
        },
        label: {
          content: (index + 1) + '',
          color: '#ffffff',
          fontSize: 13,
          anchorX: 0,
          anchorY: 3,
          textAlign: 'center'
        }
      })
    })

    // 4) 连线
    const points = markers.map(m => ({
      latitude: m.latitude,
      longitude: m.longitude
    }))
    this._trackPolyline = points.length >= 2 ? [{
      points,
      color: '#FF4444CC',
      width: 3,
      arrowLine: true
    }] : []

    this._trackMarkers = markers
    console.log('[轨迹地图] 红点:', markers.length, '个, 连线:', this._trackPolyline.length, '条')

    if (markers.length > 0) {
      this.setData({
        markers,
        nativeLat: markers[0].latitude,
        nativeLng: markers[0].longitude
      }, () => {
        this._applyPolylines()
        this._refreshOverlays(markers[0].latitude, markers[0].longitude, this.data.nativeScale)
      })
    } else {
      this.setData({ markers })
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
    this.setData({ markers: [] })
    this.renderMarkers()
    wx.hideLoading()
  },

  // ==================== 图层切换（道路+地名） ====================

  // 合并轨迹折线和道路折线，更新 polylines
  _applyPolylines() {
    const track = this._trackPolyline || []
    const roads = this._roadPolylines || []
    this.setData({ polylines: track.concat(roads) })
  },

  // 左下角图层按钮
  toggleLayer() {
    if (!this._roadFetched || !this._placeFetched) {
      if (!this._roadFetched) this.fetchRoadData()
      if (!this._placeFetched) this.fetchPlaceData()
      return
    }
    const { currentLevel, maxLevel } = this.data
    if (currentLevel >= maxLevel) {
      this._applyLevel(0)
      return
    }
    this._applyLevel(currentLevel + 1)
  },

  _applyLevel(level) {
    const show = level > 0

    // 过滤道路
    const filteredRoads = show
      ? (this._fullRoadList || []).filter(r => (parseInt(r.level) || 1) <= level)
      : []
    this._buildRoadPolylines(filteredRoads)

    // 过滤地名
    const filteredPlaces = show
      ? (this._fullPlaceList || []).filter(p => (parseInt(p.level) || 1) <= level)
      : []
    this._buildPlaceMarkers(filteredPlaces)

    const label = show ? ('Lv.' + level) : '图层'
    this.setData({
      showRoadLayer: show,
      currentLevel: level,
      layerLabel: label
    })
    this._applyPolylines()
    this._applyAllMarkers()
  },

  _applyAllMarkers() {
    const trackMarkers = this._trackMarkers || []
    const placeMarkers = this._placeMarkers || []
    this.setData({ markers: trackMarkers.concat(placeMarkers) })
  },

  fetchRoadData() {
    wx.showLoading({ title: '加载道路...' })
    const that = this
    dataCache.getRoadListFromCache((cachedData) => {
      wx.hideLoading()
      const roadList = (cachedData && cachedData.roadList) ? cachedData.roadList : []
      if (roadList.length === 0) {
        console.log('[道路] 暂无道路数据')
        that._roadFetched = true
        that._tryInitLevel()
        return
      }
      console.log('[道路] 已解析:', roadList.length, '条')
      that._fullRoadList = roadList
      that._roadFetched = true
      that._tryInitLevel()
    })
  },

  fetchPlaceData() {
    const that = this
    dataCache.getPlaceListFromCache((cachedData) => {
      const placeList = (cachedData && cachedData.placeList) ? cachedData.placeList : []
      if (placeList.length === 0) {
        console.log('[地名] 暂无数据')
        that._placeFetched = true
        that._tryInitLevel()
        return
      }
      console.log('[地名] 已解析:', placeList.length, '条')
      that._fullPlaceList = placeList
      that._placeFetched = true
      that._tryInitLevel()
    })
  },

  _tryInitLevel() {
    if (!this._roadFetched || !this._placeFetched) return
    if (this.data.currentLevel > 0) return

    let maxLevel = 0
    const allItems = [...(this._fullRoadList || []), ...(this._fullPlaceList || [])]
    allItems.forEach(item => {
      const lv = parseInt(item.level) || 1
      if (lv > maxLevel) maxLevel = lv
    })
    if (maxLevel < 1) maxLevel = 1

    this.setData({ maxLevel })
    console.log('[图层] maxLevel =', maxLevel)
    this._applyLevel(1)
  },

  _buildRoadPolylines(roadList) {
    const polylines = []
    roadList.forEach((road) => {
      const points = parseRoadPoints(road.roadinfo)
      if (points.length < 2) {
        console.warn('[道路] 坐标点不足，跳过:', road.roadname || road.route_id)
        return
      }
      const gcjPoints = points.map(p => {
        const gcj = wgs84ToGcj02(p.lng, p.lat)
        return { latitude: gcj.lat, longitude: gcj.lng }
      })
      polylines.push({
        points: gcjPoints,
        color: '#C8C8C8',
        width: 4,
        borderColor: '#808080',
        borderWidth: 1.5,
        arrowLine: false,
        dottedLine: false
      })
    })
    console.log('[道路] 构建折线:', polylines.length, '条')
    this._roadPolylines = polylines
  },

  _parseSingleGPS(gpsStr) {
    if (!gpsStr || gpsStr === '-') return null
    const parts = gpsStr.split(/[｜|,，]\s*/)
    if (parts.length < 2) return null
    const lat = parseFloat(parts[0])
    const lng = parseFloat(parts[1])
    if (isNaN(lat) || isNaN(lng)) return null
    return { lat, lng }
  },

  _buildPlaceMarkers(placeList) {
    const markers = []
    const ID_BASE = 90000
    placeList.forEach((place, index) => {
      const coord = this._parseSingleGPS(place.gps)
      if (!coord) return
      const gcj = wgs84ToGcj02(coord.lng, coord.lat)
      const name = place.name || place.placeid || '-'
      markers.push({
        id: ID_BASE + index,
        latitude: gcj.lat,
        longitude: gcj.lng,
        width: 30,
        height: 32,
        iconPath: '/images/place_pin.png',
        title: name,
        anchor: { x: 0.5, y: 1 },
        callout: {
          content: name + '\n' + coord.lat.toFixed(6) + ',' + coord.lng.toFixed(6),
          display: 'BYCLICK',
          textAlign: 'center'
        },
        label: {
          content: name,
          color: '#C62828',
          fontSize: 13,
          bgColor: '#ffffff',
          borderColor: '#E53935',
          borderWidth: 1,
          borderRadius: 4,
          padding: 2,
          anchorX: 0,
          anchorY: -12,
          textAlign: 'center'
        }
      })
    })
    console.log('[地名] 构建标记:', markers.length, '个')
    this._placeMarkers = markers
  }
})
