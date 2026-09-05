// location-map.js - 单点定位地图页（从设备详情定位记录跳转过来）
const { wgs84ToGcj02, calcDistance, parseRoadPoints } = require('../../utils/coord-transform.js')
const dataCache = require('../../config/data-cache.js')

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
    originLng: '',
    // 距用户当前位置的距离（米）
    distanceText: '',
    // 从我的坐标到设备的虚线
    polylines: [],
    // 道路显示（左下角图层开关，与地图中心一致）
    showRoadLayer: false,
    currentLevel: 0,
    maxLevel: 0,
    layerLabel: '图层',
    // 设备气泡是否显示：默认显示；点击气泡隐藏；隐藏时点击设备图标恢复
    deviceCalloutShow: true
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
        originLat: lat.toFixed(5),
        originLng: lng.toFixed(5)
      })
      // 保存坐标引用，图标生成完成后重刷 marker
      this._markerGcj = { lat: gcj.lat, lng: gcj.lng, wgsLat: lat, wgsLng: lng }
      this.renderMarker(gcj.lat, gcj.lng, lat, lng)
      this._refreshOverlays(gcj.lat, gcj.lng, 17)
      // 生成绿色设备图钉图标（与地图中心设备一致），生成后自动刷新 marker
      this._generateDevPin()
      // 生成蓝色"我的位置"圆点图标（避免系统 show-location 蓝点与 polyline 起点有亚像素偏差）
      this._generateMyPin()
      // 生成透明占位图标（用于在虚线上显示距离文字）
      this._generateTransparentIcon()
      // 异步计算"距我"距离（GCJ-02 vs GCJ-02，避免坐标系差异）
      this._calcDistanceFromMe(gcj.lat, gcj.lng)
      // 查询设备别名（rename），用于气泡第一行"设备（别名）"显示
      this._loadDeviceRename(deviceId, upDateDevice)
    } else {
      wx.showToast({ title: '坐标无效', icon: 'none' })
    }
  },

  // 计算定位点距离用户当前位置的直线距离，显示在顶部信息栏
  // 距离 < 1km 显示 "xx 米"；>= 1km 显示 "x.xx 千米"
  // 同时保存我的坐标（GCJ-02），绘制"我 → 设备"的虚线 + 我的位置蓝点 marker
  // 首次定位成功后启动位置轮询，虚线/蓝点/距离随我的位置实时变化
  _calcDistanceFromMe(gcjLat, gcjLng) {
    wx.getLocation({
      type: 'gcj02',
      success: (res) => {
        this._myGcj = { lat: res.latitude, lng: res.longitude }
        this._applyOverlays()
        // 启动实时位置更新（轮询，每 3 秒）
        this._startLocationWatch()
      },
      fail: () => {
        this.setData({ distanceText: '获取失败' })
      }
    })
  },

  // 查询设备别名（rename）：优先按 deviceId 匹配，其次按 upDateDevice
  _loadDeviceRename(deviceId, upDateDevice) {
    const that = this
    dataCache.getDeviceList((cached) => {
      const list = (cached && cached.recordList) || []
      const findRename = (id) => {
        if (!id) return ''
        const item = list.find(v => v.deviceId === id)
        return item ? (item.rename || '') : ''
      }
      const rename = findRename(deviceId) || findRename(upDateDevice) || ''
      if (rename !== that._deviceRename) {
        that._deviceRename = rename
        that._applyOverlays()
      }
    })
  },

  // 实时跟随我的位置：每 3 秒重新获取一次位置，刷新虚线与距离
  _startLocationWatch() {
    if (this._locationTimer) return
    this._locationTimer = setInterval(() => {
      wx.getLocation({
        type: 'gcj02',
        success: (res) => {
          const moved = !this._myGcj ||
            Math.abs(res.latitude - this._myGcj.lat) > 0.000001 ||
            Math.abs(res.longitude - this._myGcj.lng) > 0.000001
          if (!moved) return
          this._myGcj = { lat: res.latitude, lng: res.longitude }
          this._applyOverlays()
        },
        fail: () => {}
      })
    }, 3000)
  },

  // 统一刷新所有叠加层：设备 marker(0) + 我的蓝点(1) + 虚线 + 线中距离(2) + 道路折线
  // 设备气泡：第一行 = 设备 +（别名），第二行 = 定位时间
  // 蓝点坐标 = polyline 起点坐标 = 同一份 wx.getLocation 返回值 → 严格对齐，0 位差
  _applyOverlays() {
    const { deviceId } = this.data
    const markers = []
    const polylines = []

    // 设备 marker（绿色图钉）；气泡两行内容 + 可点击开关
    if (this._markerGcj) {
      const alias = this._deviceRename || ''
      const calloutLines = [(deviceId || '-') + (alias ? '（' + alias + '）' : '')]
      if (this.data.recordTime) calloutLines.push(this.data.recordTime)
      markers.push({
        id: 0,
        latitude: this._markerGcj.lat,
        longitude: this._markerGcj.lng,
        width: 28,
        height: 28,
        iconPath: this._deviceIconPath || '',
        title: '设备: ' + (deviceId || '-'),
        callout: {
          content: calloutLines.join('\n'),
          // ALWAYS 常显；BYCLICK 隐藏（点击后原生显示），由 onMarkerTap/onCalloutTap 控制
          display: this.data.deviceCalloutShow ? 'ALWAYS' : 'BYCLICK',
          textAlign: 'center',
          fontSize: 13,
          padding: 10,
          borderRadius: 8
        },
        anchor: { x: 0.5, y: 0.5 }
      })
    }

    // 道路折线（开启图层时显示，放最底层）
    if (this.data.showRoadLayer && this._roadPolylines && this._roadPolylines.length) {
      this._roadPolylines.forEach(line => polylines.push(line))
    }

    const payload = {}
    // 我的蓝点 + 虚线 + 距离（红色虚线压在路上方）
    if (this._myGcj) {
      const meters = this._markerGcj
        ? calcDistance(this._markerGcj.lat, this._markerGcj.lng, this._myGcj.lat, this._myGcj.lng)
        : 0
      const text = meters < 1000
        ? Math.round(meters) + ' 米'
        : (meters / 1000).toFixed(2) + ' 千米'
      payload.distanceText = text
      polylines.push({
        points: [
          { latitude: this._myGcj.lat, longitude: this._myGcj.lng },
          { latitude: this._markerGcj.lat, longitude: this._markerGcj.lng }
        ],
        color: '#ff0000',
        width: 3,
        dottedLine: true,
        arrowLine: true,
        zIndex: 10
      })

      // 我的蓝点
      if (this._myIconPath) {
        markers.push({
          id: 1,
          latitude: this._myGcj.lat,
          longitude: this._myGcj.lng,
          width: 22,
          height: 22,
          iconPath: this._myIconPath,
          title: '我的位置',
          anchor: { x: 0.5, y: 0.5 }
        })
      }

      // 线中距离文字：透明占位 marker + callout，位于线段中点
      if (this._transparentIconPath && this._markerGcj) {
        markers.push({
          id: 2,
          latitude: (this._markerGcj.lat + this._myGcj.lat) / 2,
          longitude: (this._markerGcj.lng + this._myGcj.lng) / 2,
          width: 4,
          height: 4,
          iconPath: this._transparentIconPath,
          callout: {
            content: text,
            display: 'ALWAYS',
            color: '#333333',
            fontSize: 12,
            bgColor: '#ffffff',
            borderColor: '#999999',
            borderWidth: 1,
            padding: 4,
            borderRadius: 4,
            textAlign: 'center'
          },
          anchor: { x: 0.5, y: 0.5 }
        })
      }
    }

    payload.polylines = polylines
    payload.markers = markers
    this.setData(payload)
  },

  // 生成蓝色"我的位置"圆点图标：白色描边 + 蓝色实心
  // 离屏绘制到固定 PNG，生成完成后统一刷新叠加层
  _generateMyPin() {
    const query = wx.createSelectorQuery()
    query.select('#myPinCanvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) return
      const canvas = res[0].node
      const ctx = canvas.getContext('2d')
      const dpr = wx.getSystemInfoSync().pixelRatio
      const size = 22
      canvas.width = size * dpr
      canvas.height = size * dpr
      ctx.scale(dpr, dpr)

      const cx = size / 2, cy = size / 2, r = 9
      // 白色描边圈
      ctx.beginPath()
      ctx.arc(cx, cy, r + 1, 0, Math.PI * 2)
      ctx.fillStyle = '#ffffff'
      ctx.fill()
      // 蓝色实心
      ctx.beginPath()
      ctx.arc(cx, cy, r, 0, Math.PI * 2)
      ctx.fillStyle = '#3C9CFF'
      ctx.fill()

      const filePath = (wx.env.USER_DATA_PATH || '') + '/my_pin.png'
      wx.canvasToTempFilePath({
        canvas: canvas,
        fileType: 'png',
        filePath: filePath,
        success: (fileRes) => {
          this._myIconPath = fileRes.tempFilePath
          this._applyOverlays()
        },
        fail: () => {}
      })
    })
  },

  // 生成 4x4 全透明占位图标：用于"线中距离文字"marker 的 iconPath
  // （marker 必须带 iconPath 才能显示 callout，用透明图避免遮挡虚线）
  _generateTransparentIcon() {
    const query = wx.createSelectorQuery()
    query.select('#transparentCanvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) return
      const canvas = res[0].node
      const dpr = wx.getSystemInfoSync().pixelRatio
      canvas.width = 4 * dpr
      canvas.height = 4 * dpr
      wx.canvasToTempFilePath({
        canvas: canvas,
        fileType: 'png',
        filePath: (wx.env.USER_DATA_PATH || '') + '/transparent.png',
        success: (fileRes) => {
          this._transparentIconPath = fileRes.tempFilePath
          this._applyOverlays()
        },
        fail: () => {}
      })
    })
  },

  renderMarker(gcjLat, gcjLng, wgsLat, wgsLng) {
    // 坐标已保存在 this._markerGcj，统一走 _applyOverlays 渲染所有叠加层
    this._applyOverlays()
  },

  // ==================== 绿色设备图钉图标（与地图中心一致） ====================

  // 通用图钉绘制：canvas选择器 → fillColor/strokeColor → 导图
  _drawPin(canvasSelector, fillColor, strokeColor, targetPath, cb) {
    const query = wx.createSelectorQuery()
    query.select(canvasSelector).fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) return
      const canvas = res[0].node
      const ctx = canvas.getContext('2d')
      const dpr = wx.getSystemInfoSync().pixelRatio
      canvas.width = 28 * dpr
      canvas.height = 28 * dpr
      ctx.scale(dpr, dpr)

      const cx = 14, cy = 14, r = 10

      // 白色圆底 + 绿色描边
      ctx.beginPath()
      ctx.arc(cx, cy, r, 0, Math.PI * 2)
      ctx.fillStyle = '#ffffff'
      ctx.fill()
      ctx.strokeStyle = fillColor
      ctx.lineWidth = 2
      ctx.stroke()

      // 内部绿色倒三角
      ctx.beginPath()
      ctx.moveTo(cx - 5, cy - 5)
      ctx.lineTo(cx, cy + 5)
      ctx.lineTo(cx + 5, cy - 5)
      ctx.closePath()
      ctx.fillStyle = fillColor
      ctx.fill()

      wx.canvasToTempFilePath({
        canvas: canvas,
        fileType: 'png',
        filePath: targetPath,
        success: (fileRes) => cb(fileRes.tempFilePath),
        fail: () => {}
      })
    })
  },

  // 生成设备绿色图钉图标（绿色 #00C853，与地图中心 _generateDevPin 完全一致），
  // 固定路径每次覆盖不累积；生成完成后统一刷新叠加层
  _generateDevPin() {
    const that = this
    const targetPath = (wx.env.USER_DATA_PATH || '') + '/dev_pin.png'
    this._drawPin('#devPinCanvas', '#00C853', '#1B5E20', targetPath, (filePath) => {
      that._deviceIconPath = filePath
      that._applyOverlays()
    })
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
  },

  // ==================== 道路显示（左下角图层按钮，与地图中心一致） ====================

  // 左下角图层按钮：首次点击加载道路并自动显示最低等级；之后点击逐级显示，
  // 已达最大等级后再点击隐藏全部道路
  toggleLayer() {
    if (!this._roadFetched) {
      this.fetchRoadData()
      return
    }
    const { currentLevel, maxLevel } = this.data
    if (currentLevel >= maxLevel) {
      // 已到最大等级，再按隐藏所有
      this._applyLevel(0)
      return
    }
    // 升一级
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
      const roadList = (cachedData && cachedData.roadList) || []
      if (roadList.length === 0) {
        wx.showToast({ title: '暂无道路数据', icon: 'none' })
        return
      }
      console.log('[道路] 定位详情页加载:', roadList.length, '条（缓存优先）')
      that._fullRoadList = roadList
      // 道路中的最大等级
      let maxLevel = 0
      roadList.forEach(item => {
        const lv = parseInt(item.level) || 1
        if (lv > maxLevel) maxLevel = lv
      })
      if (maxLevel < 1) maxLevel = 1
      that.setData({ maxLevel })
      // 加载完成自动显示最低等级
      that._applyLevel(1)
    })
  },

  // 按等级刷新道路折线：level=0 隐藏；level>0 显示 level≤该值的所有道路
  _applyLevel(level) {
    const show = level > 0
    const filteredRoads = show
      ? this._fullRoadList.filter(r => (parseInt(r.level) || 1) <= level)
      : []
    this._buildRoadPolylines(filteredRoads)

    const label = show ? ('Lv.' + level) : '图层'
    this.setData({
      showRoadLayer: show,
      currentLevel: level,
      layerLabel: label
    })
    // 统一刷新叠加层：合并道路折线与"我→设备"虚线
    this._applyOverlays()
    wx.showToast({
      title: show ? ('已显示等级 ≤' + level) : '道路已隐藏',
      icon: 'none',
      duration: 1000
    })
  },

  // 解析 roadinfo 坐标并构建灰色道路折线（与地图中心样式一致）
  _buildRoadPolylines(roadList) {
    const polylines = []
    roadList.forEach((road) => {
      const points = parseRoadPoints(road.roadinfo)
      if (points.length < 2) {
        console.warn('[道路] 坐标点不足，跳过:', road.roadname || road.route_id)
        return
      }
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
    console.log('[道路] 构建折线:', polylines.length, '条')
    this._roadPolylines = polylines
  },

  // ==================== 设备气泡 显示/隐藏 ====================

  // 点击设备 marker：气泡未显示时恢复显示（已显示则不做任何事）
  onMarkerTap(e) {
    const markerId = e && e.detail ? e.detail.markerId : -1
    if (markerId !== 0) return
    // 部分平台点击气泡会连带触发 markertap，隐藏后短暂忽略，避免"刚隐藏又显示"
    if (Date.now() - (this._lastCalloutHideTs || 0) < 350) return
    if (this.data.deviceCalloutShow) return
    this.setData({ deviceCalloutShow: true })
    this._applyOverlays()
  },

  // 点击设备气泡 → 隐藏气泡
  onCalloutTap(e) {
    const markerId = e && e.detail ? e.detail.markerId : -1
    if (markerId !== 0) return
    if (!this.data.deviceCalloutShow) return
    this._lastCalloutHideTs = Date.now()
    this.setData({ deviceCalloutShow: false })
    this._applyOverlays()
  },

  // 页面销毁：停止位置轮询
  onUnload() {
    if (this._locationTimer) {
      clearInterval(this._locationTimer)
      this._locationTimer = null
    }
  }
})
