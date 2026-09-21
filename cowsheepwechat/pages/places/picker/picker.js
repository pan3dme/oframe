// places/picker/picker.js - 地图选坐标
const { gcj02ToWgs84, wgs84ToGcj02, parseRoadPoints } = require('../../../utils/coord-transform.js')
const dataCache = require('../../../config/data-cache.js')

Page({
  data: {
    centerLat: 26.529950,
    centerLng: 109.390224,
    scale: 14,
    gpsText: '—',
    isSatellite: true,
    // 道路/地名图层（左下角按钮，与地图中心一致）
    showRoadLayer: false,
    currentLevel: 0,
    maxLevel: 0,
    layerLabel: '图层',
    markers: [],
    polylines: []
  },

  onLoad(options) {
    // 设置中继坐标：调用方传入 LOT 表中该设备已有坐标（WGS-84）作为初始中心
    if (options && options.lat && options.lng) {
      const lat = parseFloat(options.lat)
      const lng = parseFloat(options.lng)
      if (!isNaN(lat) && !isNaN(lng) && !(lat === 0 && lng === 0) &&
          lat >= -90 && lat <= 90 && lng >= -180 && lng <= 180) {
        const gcj = wgs84ToGcj02(lng, lat)
        this.setData({
          centerLat: gcj.lat,
          centerLng: gcj.lng,
          scale: 16,
          gpsText: lat.toFixed(6) + ',' + lng.toFixed(6)
        })
        this._wgsCoords = { lat: lat, lng: lng }
        this._refreshOverlays(gcj.lat, gcj.lng, 16)
        return
      }
    }
    // 尝试定位到用户当前位置
    const that = this
    wx.getLocation({
      type: 'gcj02',
      success: (res) => {
        that.setData({
          centerLat: res.latitude,
          centerLng: res.longitude
        }, () => {
          that._updateGpsText(res.longitude, res.latitude)
          that._refreshOverlays(res.latitude, res.longitude, 15)
        })
      },
      fail: () => {
        // 使用默认中心点
        that._updateGpsText(109.390224, 26.529950)
        that._refreshOverlays(26.529950, 109.390224, 15)
      }
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
    var mapCtx = wx.createMapContext('pickerMap')
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
    var mapCtx = wx.createMapContext('pickerMap')
    if (this._tileCache) {
      for (var key in this._tileCache) {
        mapCtx.removeGroundOverlay({ id: this._tileCache[key].id })
      }
      this._tileCache = {}
    }
  },

  onRegionChange(e) {
    if (e.type !== 'end') return
    // 更新坐标显示
    if (e.detail && e.detail.centerLocation) {
      const { longitude, latitude } = e.detail.centerLocation
      this._updateGpsText(longitude, latitude)
    }
    if (this._refreshingTiles) return
    // 高德瓦片刷新
    const mapCtx = wx.createMapContext('pickerMap')
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

  // GCJ-02 → WGS-84 显示 + 存储
  _updateGpsText(lng, lat) {
    const wgs = gcj02ToWgs84(lng, lat)
    const text = wgs.lat.toFixed(6) + ',' + wgs.lng.toFixed(6)
    this.setData({ gpsText: text })
    this._wgsCoords = wgs
  },

  // ==================== 道路/地名图层（左下角按钮，与地图中心一致） ====================

  // 首次点击：并行请求道路 + 地名数据，加载完成自动显示最低等级；
  // 之后点击逐级升档，已达最大等级后再点击隐藏全部
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

  // 加载道路数据（缓存优先，与地图中心同一份缓存）
  fetchRoadData() {
    if (this._roadLoading) return
    this._roadLoading = true
    const that = this
    wx.showLoading({ title: '加载道路...' })
    dataCache.getRoadListFromCache((cachedData) => {
      that._roadLoading = false
      wx.hideLoading()
      that._roadFetched = true
      that._fullRoadList = (cachedData && cachedData.roadList) || []
      that._checkLayerReady()
    })
  },

  // 加载地名数据（缓存优先，与地图中心同一份缓存）
  fetchPlaceData() {
    if (this._placeLoading) return
    this._placeLoading = true
    const that = this
    dataCache.getPlaceListFromCache((cachedData) => {
      that._placeLoading = false
      that._placeFetched = true
      that._fullPlaceList = (cachedData && cachedData.placeList) || []
      that._checkLayerReady()
    })
  },

  // 道路和地名都加载完成后：计算 maxLevel，默认显示最低等级
  _checkLayerReady() {
    if (!this._roadFetched || !this._placeFetched) return
    const roads = this._fullRoadList || []
    const places = this._fullPlaceList || []
    if (roads.length === 0 && places.length === 0) {
      // 无数据：重置标记，下次点击可重试
      this._roadFetched = false
      this._placeFetched = false
      wx.showToast({ title: '暂无道路/地名数据', icon: 'none' })
      return
    }
    let maxLevel = 0
    roads.concat(places).forEach(item => {
      const lv = parseInt(item.level) || 1
      if (lv > maxLevel) maxLevel = lv
    })
    if (maxLevel < 1) maxLevel = 1
    this.setData({ maxLevel })
    this._applyLevel(1)
  },

  // 按等级刷新道路折线和地名标记：level=0 隐藏；level>0 显示 level≤该值的所有项
  _applyLevel(level) {
    const show = level > 0
    const filteredRoads = show
      ? this._fullRoadList.filter(r => (parseInt(r.level) || 1) <= level)
      : []
    this._buildRoadPolylines(filteredRoads)

    const filteredPlaces = show
      ? this._fullPlaceList.filter(p => (parseInt(p.level) || 1) <= level)
      : []
    this._buildPlaceMarkers(filteredPlaces)

    const label = show ? ('Lv.' + level) : '图层'
    this.setData({
      showRoadLayer: show,
      currentLevel: level,
      layerLabel: label,
      polylines: show ? this._roadPolylines : [],
      markers: show ? (this._placeMarkers || []) : []
    })
    wx.showToast({
      title: show ? ('已显示等级 ≤' + level) : '图层已隐藏',
      icon: 'none',
      duration: 1000
    })
  },

  // 解析 roadinfo 坐标并构建灰色道路折线（与地图中心样式一致）
  _buildRoadPolylines(roadList) {
    const polylines = []
    roadList.forEach((road) => {
      const points = parseRoadPoints(road.roadinfo)
      if (points.length < 2) return
      // WGS-84 → GCJ-02 转换全部点
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
    this._roadPolylines = polylines
  },

  // 地名标记：逐个生成"白字黑描边+红点"PNG 图钉，全部完成后渲染
  _buildPlaceMarkers(placeList) {
    const that = this
    const ID_BASE = 90000
    const items = []
    placeList.forEach((place, index) => {
      const coord = this._parseSingleGPS(place.gps)
      if (!coord) return
      const gcj = wgs84ToGcj02(coord.lng, coord.lat)
      items.push({ id: ID_BASE + index, gcj, name: place.name || place.placeid || '-' })
    })
    if (items.length === 0) {
      this._placeMarkers = []
      return
    }
    const markers = []
    let done = 0
    items.forEach((item, i) => {
      this._generatePlaceTextPin(item.name, i, (iconPath, W, H, anchorY) => {
        if (iconPath) {
          markers.push({
            id: item.id,
            latitude: item.gcj.lat,
            longitude: item.gcj.lng,
            width: W,
            height: H,
            iconPath: iconPath,
            title: item.name,
            zIndex: 50,
            // 红点圆心对准坐标点
            anchor: { x: 0.5, y: anchorY }
          })
        }
        done++
        if (done === items.length) {
          that._placeMarkers = markers
          if (that.data.showRoadLayer) that.setData({ markers })
        }
      })
    })
  },

  // 从单条 GPS 字符串中提取 lat/lng（兼容 "lat|lng" / "lat,lng"）
  _parseSingleGPS(gpsStr) {
    if (!gpsStr || gpsStr === '-') return null
    const parts = gpsStr.split(/[｜|,，]\s*/)
    if (parts.length < 2) return null
    const lat = parseFloat(parts[0])
    const lng = parseFloat(parts[1])
    if (isNaN(lat) || isNaN(lng)) return null
    return { lat, lng }
  },

  // 生成"黑描边白字 + 红色图钉"组合 PNG（与地图中心同款，红点尖端对准坐标点）
  _generatePlaceTextPin(name, idx, cb) {
    const query = wx.createSelectorQuery()
    query.select('#placeTextCanvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) return cb('', 0, 0, 0)
      const canvas = res[0].node
      const ctx = canvas.getContext('2d')
      const dpr = wx.getSystemInfoSync().pixelRatio
      const fontSize = 13
      const dotH = 28
      const textH = 24
      const textW = Math.ceil(name.length * fontSize * 1.05) + 10
      const W = Math.max(32, textW)
      const H = textH + dotH

      canvas.width = W * dpr
      canvas.height = H * dpr
      ctx.scale(dpr, dpr)
      ctx.clearRect(0, 0, W, H)

      // ---- 上半部：白字 + 黑色描边 ----
      ctx.font = 'bold ' + fontSize + 'px sans-serif'
      ctx.textAlign = 'center'
      ctx.textBaseline = 'middle'
      ctx.lineJoin = 'round'
      ctx.strokeStyle = '#000000'
      ctx.lineWidth = 3
      ctx.strokeText(name, W / 2, textH / 2 + 1)
      ctx.fillStyle = '#ffffff'
      ctx.fillText(name, W / 2, textH / 2 + 1)

      // ---- 下半部：红色定位图钉（尖端对准坐标点） ----
      const cx = W / 2
      const headCy = textH + 10
      const headR = 8
      const tipY = textH + dotH - 2

      ctx.beginPath()
      ctx.moveTo(cx, headCy - headR)
      ctx.arc(cx, headCy, headR, -Math.PI / 2, Math.PI / 2 + 0.35, false)
      ctx.quadraticCurveTo(cx + headR * 0.4, headCy + headR + 2, cx, tipY)
      ctx.quadraticCurveTo(cx - headR * 0.4, headCy + headR + 2, cx - headR, headCy + headR * 0.35)
      ctx.arc(cx, headCy, headR, Math.PI / 2 - 0.35, -Math.PI / 2, false)
      ctx.closePath()
      ctx.fillStyle = '#E53935'
      ctx.fill()
      ctx.strokeStyle = '#C62828'
      ctx.lineWidth = 1
      ctx.stroke()

      // 中心白点
      ctx.beginPath()
      ctx.arc(cx, headCy, 3.5, 0, Math.PI * 2)
      ctx.fillStyle = '#ffffff'
      ctx.fill()

      const filePath = (wx.env.USER_DATA_PATH || '') + '/picker_place_' + idx + '.png'
      wx.canvasToTempFilePath({
        canvas: canvas,
        fileType: 'png',
        filePath: filePath,
        success: (fileRes) => cb(fileRes.tempFilePath, W, H, tipY / H),
        fail: () => cb('', 0, 0, 0)
      })
    })
  },

  onConfirm() {
    let gps = ''
    if (this._wgsCoords) {
      const wgs = this._wgsCoords
      gps = wgs.lat.toFixed(6) + ',' + wgs.lng.toFixed(6)
    }
    // 调用方（如首页/设备详情设置中继坐标）可注册回调 _onPlacePicked，
    // 确定后直接回调发起提交，避免依赖 onShow 时机导致不触发
    const cb = getApp().globalData._onPlacePicked
    getApp().globalData._onPlacePicked = null
    if (cb) {
      wx.navigateBack()
      if (gps) setTimeout(() => { cb(gps) }, 300)
      return
    }
    // 无回调：沿用 _placePickedGps 供调用方 onShow 读取（地名选取等）
    if (gps) {
      getApp().globalData._placePickedGps = gps
    }
    wx.navigateBack()
  },

  onUnload() {
    // 未点确定直接返回时清理回调，避免下次误触发
    if (getApp().globalData) getApp().globalData._onPlacePicked = null
  },

})
