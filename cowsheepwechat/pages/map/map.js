// map.js
const app = getApp()
const dataCache = require('../../config/data-cache.js')
const timeWindowCodec = require('../../utils/time-window-codec.js')
const { wgs84ToGcj02, parseRoadPoints } = require('../../utils/coord-transform.js')

// 中继DTU指令转发云函数地址（与 中继DTU指令页 relay-dtu-cmd 一致）
const RELAY_FC_URL = 'https://gpsmoveinfo.cn/fc/sendtodtucmd'

Page({
  data: {
    showNativeMap: false,
    nativeLat: 26.529950,
    nativeLng: 109.390224,
    nativeScale: 15,
    markers: [],
    polylines: [],
    showRoadLayer: false,
    currentLevel: 0,
    maxLevel: 0,
    layerLabel: '图层',
    isSatellite: true,     // 全程开启卫星底图
    currentMarker: -1,
    activeCalloutId: -1,   // 当前唯一展开的气泡 marker id
    groundOverlays: []
  },

  _cowMarkers: [],
  _deviceMarkers: [],
  _roadPolylines: [],
  _roadFetched: false,
  _placeFetched: false,
  _placeMarkers: [],
  _fullRoadList: [],
  _fullPlaceList: [],
  _cowIconPath: '',
  _deviceIconPath: '',
  _deviceGrayIconPath: '',   // 最后定位超过1小时且最近无对时时使用的灰色图标
  _deviceLightGreenIconPath: '', // 最后定位超过1小时但最近1小时内有对时时使用的浅绿色图标
  _cowIconReady: false,
  _devIconReady: false,
  _pendingCrowData: null,
  _pendingDeviceArgs: null,
  _deviceLotList: null,      // 最近一次设备LOT原始数据（用于图标老化重算）
  _deviceInfoMap: null,      // 最近一次设备信息映射
  _deviceSyncMap: {},        // deviceId -> device_sync 对时同步表记录 { rawTime,... }（用于浅绿色图标判断）
  _staleTimer: null,         // 设备图标老化检测定时器
  _lastCalloutHideTs: 0,     // 上次点击气泡收起的时间戳，用于避免 markertap 误触发

  onLoad() {
    // 数据请求先发起；图标在 onReady 中绘制，避免 canvas 节点未就绪导致失败
    this.loadMap()
    this.fetchCrowData()
    this.fetchDeviceLotData()
  },

  onReady() {
    // 页面渲染完成后再绘制 canvas 图标，并刷新已拿到数据的 marker
    this._generateCowPin()
    this._generateDevPin()
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
    // 高德瓦片服务器不需要 Key
    return 'https://webst0' + s + '.is.autonavi.com/appmaptile?style=6&x=' + x + '&y=' + y + '&z=' + z
  },

  // 生成稳定的 overlay ID（基于 zoom + 瓦片坐标，避免与其它 ID 冲突）
  _tileOverlayId(EZ, x, y) {
    return 2000 + ((EZ * 1000000 + x * 10000 + y) % 98000)
  },
  _tileKey(EZ, x, y) {
    return EZ + '_' + x + '_' + y
  },

  // 加载多片高德卫星瓦片覆盖可见区域，增量更新——已显示的瓦片不动，只删多余、加新增
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
    var mapCtx = wx.createMapContext('cowMap')

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
      console.log('[overlay] 完成，缓存瓦片:', cached, '（本次新增:', total - removed, '移除:', removed, '）')
    }
  },

  _refreshOverlays(lat, lng, zoom) {
    const key = lat.toFixed(4) + ',' + lng.toFixed(4) + ',' + zoom
    if (this._lastOverlayKey === key) return
    this._lastOverlayKey = key

    const SATELLITE_THRESHOLD = 15  // 15级以上才覆盖高德卫星瓦片

    if (zoom < SATELLITE_THRESHOLD) {
      // 低于15级：清除高德瓦片，保留腾讯卫星底图
      this._clearAllOverlays()
      console.log('[overlay] 缩放' + zoom + ' < ' + SATELLITE_THRESHOLD + '，仅腾讯卫星底图')
      return
    }

    // 15级以上：腾讯卫星底图 + 高德瓦片覆盖
    if (!this.data.isSatellite) {
      this.setData({ isSatellite: true })
    }
    this._loadOverlayTile(lat, lng, zoom)
  },

  // 清除所有 ground overlay
  _clearAllOverlays() {
    var mapCtx = wx.createMapContext('cowMap')
    // 清除瓦片缓存
    if (this._tileCache) {
      for (var key in this._tileCache) {
        mapCtx.removeGroundOverlay({ id: this._tileCache[key].id })
      }
      this._tileCache = {}
    }
    console.log('[overlay] 清除所有瓦片')
  },

  // ========== marker 渲染 ==========
  renderMarkersFromData(recordList) {
    if (!recordList || recordList.length === 0) {
      this._cowMarkers = []
      this._applyAllMarkers()
      return
    }
    // 图标未准备好时先暂存数据，避免用空 iconPath 渲染成默认红点
    if (!this._cowIconReady) {
      this._pendingCrowData = recordList
      return
    }
    this._pendingCrowData = null
    const normalized = recordList.map(item => ({
      crow_id: item.crow_id || item.crow_idx || '-',
      crow_idx: item.crow_idx || item.crow_id || '-',
      gps: item.gps || '-',
      time: item.time || item.rawTime || '-'
    }))

    // 构建牛羊名称映射：cowsheepId → name
    const nameMap = {}
    const livestockCache = getApp().globalData.livestockCache
    if (livestockCache && livestockCache.livestockList) {
      livestockCache.livestockList.forEach(l => {
        if (l.cowsheepId) nameMap[String(l.cowsheepId)] = l.name
      })
    }

    const markers = normalized
      .filter(item => item.gps !== '-')
      .map((item, index) => {
        const parts = item.gps.split(/[｜|]/)
        if (parts.length < 2) return null
        const wgsLat = parseFloat(parts[0])
        const wgsLng = parseFloat(parts[1])
        if (isNaN(wgsLat) || isNaN(wgsLng)) return null
        const gcj = wgs84ToGcj02(wgsLng, wgsLat)

        let labelText = nameMap[item.crow_id] || (item.crow_id || item.crow_idx)
        // if (labelText && labelText.length > 9) {
        //   labelText = labelText.substring(0, 9) + '...'
        // }

        return {
          id: index,
          latitude: gcj.lat,
          longitude: gcj.lng,
          width: 50,
          height: 28,
          iconPath: this._cowIconPath || '',
          title: labelText,
          callout: {
            content: labelText + '\nID:' + item.crow_id + '\n更新:' + (item.time || '-'),
            display: 'BYCLICK',
            textAlign: 'center',
            fontSize: 13,
            padding: 8,
            borderRadius: 6
          },
          label: {
            content: labelText,
            color: '#333333',
            fontSize: 14,
            bgColor: '#ffffff',
            borderColor: '#999999',
            borderWidth: 1,
            borderRadius: 4,
            padding: 2,
            anchorX: 0,
            anchorY: 0,
            textAlign: 'left'
          }
        }
      })
      .filter(item => item !== null)
 
    this._cowMarkers = markers
    this._applyAllMarkers()
  },

  // ==================== 设备 LOT 标记点 ====================

  fetchDeviceLotData() {
    // 先获取设备列表（含rename别名、visible、ProductKey），构建映射
    dataCache.getDeviceList((devData) => {
      const deviceInfoMap = {} // deviceId -> { rename, visible, hasProductKey }
      const recordList = devData.recordList || []
      recordList.forEach(r => {
        if (r.deviceId) {
          const visible = r.visible === true || r.visible === 'true' || r.visible === 1
          const hasProductKey = !!(r.ProductKey && r.ProductKey !== '-')
          deviceInfoMap[r.deviceId] = {
            rename: r.rename || '',
            visible: visible,
            hasProductKey: hasProductKey
          }
        }
      })

      // 对时同步表（device_sync）：用于 GPS 超1小时但最近1小时内有对时时显示浅绿色图标
      dataCache.getDeviceSyncAll((syncData) => {
        this._deviceSyncMap = (syncData && syncData.syncMap) || {}
        console.log('[地图] 设备对时同步数据:', Object.keys(this._deviceSyncMap).length, '条')
        // LOT 数据已就绪时，按最新对时信息重算设备图标颜色
        if (this._deviceLotList && this._deviceLotList.length > 0) {
          this._renderDeviceMarkers(this._deviceLotList, deviceInfoMap)
          this._applyAllMarkers()
        }
      }, true)

      dataCache.getDeviceLotRefresh((lotData) => {
        const lotList = lotData.lotList || []
        console.log('[地图] 设备LOT数据:', lotList.length, '条')
        this._deviceLotList = lotList
        this._deviceInfoMap = deviceInfoMap
        this._renderDeviceMarkers(lotList, deviceInfoMap)
        this._applyAllMarkers()
        if (lotList.length > 0) this._startStaleTimer()
        wx.hideLoading()
      }, true)
    })
  },

  // 把各种格式的"时间字符串/时间戳"统一解析为毫秒时间戳，解析失败返回 NaN
  // 兼容 "YYYY/M/D H:m:s"、"YYYY-MM-DD H:m:s"、秒/毫秒时间戳等格式
  _parseTimeToTs(timeStr) {
    if (timeStr === undefined || timeStr === null || timeStr === '' || timeStr === '-') return NaN
    let ts = NaN
    if (typeof timeStr === 'number') {
      ts = timeStr > 1e12 ? timeStr : timeStr * 1000
    } else {
      const s = String(timeStr).trim()
      if (/^\d+$/.test(s)) {
        const n = parseInt(s, 10)
        ts = n > 1e12 ? n : n * 1000
      } else {
        // 部分机型(如 iOS)对 "2026/8/10 23:13:33" 直接 new Date 会解析失败，先规范化
        const norm = s.replace(/\//g, '-').replace(' ', 'T')
        ts = new Date(norm).getTime()
        if (isNaN(ts)) {
          const m = s.match(/^(\d{4})[-/](\d{1,2})[-/](\d{1,2})[ T](\d{1,2}):(\d{1,2})(?::(\d{1,2}))?/)
          if (m) ts = new Date(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +(m[6] || 0)).getTime()
        }
      }
    }
    return ts
  },

  // 判断设备"最后定位时间"距今是否已超过 1 小时（超过 → 图标置灰/置浅绿）
  _deviceIsStale(timeStr) {
    const ts = this._parseTimeToTs(timeStr)
    if (isNaN(ts)) return false
    return Date.now() - ts > 3600 * 1000
  },

  // 判断设备最近 1 小时内是否有对时同步记录（有 → GPS超1小时时用浅绿色图标显示）
  _deviceHasRecentSync(deviceId) {
    const sync = (this._deviceSyncMap || {})[deviceId]
    if (!sync) return false
    const ts = this._parseTimeToTs(sync.rawTime || sync.time || '')
    if (isNaN(ts)) return false
    return Date.now() - ts <= 3600 * 1000
  },

  // 启动设备图标老化检测：页面停留时每分钟重算一次，
  // 设备最后定位时间跨过 1 小时阈值时自动把图标由绿色切为灰色
  _startStaleTimer() {
    if (this._staleTimer) return
    const that = this
    this._staleTimer = setInterval(() => {
      that._refreshDeviceIconByStale()
    }, 60000)
  },

  _refreshDeviceIconByStale() {
    if (!this._devIconReady || !this._deviceLotList || this._deviceLotList.length === 0) return
    const prevIcons = (this._deviceMarkers || []).map(m => m.iconPath).join(',')
    this._renderDeviceMarkers(this._deviceLotList, this._deviceInfoMap || {})
    const nextIcons = (this._deviceMarkers || []).map(m => m.iconPath).join(',')
    // 仅当有图标颜色变化时才刷新地图，避免每60秒无意义地整组重绘
    if (prevIcons !== nextIcons) this._applyAllMarkers()
  },

  _renderDeviceMarkers(lotList, deviceInfoMap) {
    if (!lotList || lotList.length === 0) {
      this._deviceMarkers = []
      return
    }
    // 图标未准备好时先暂存数据，避免用空 iconPath 渲染成默认红点
    if (!this._devIconReady) {
      this._pendingDeviceArgs = { lotList, deviceInfoMap }
      return
    }
    this._pendingDeviceArgs = null

    // 从一条记录中提取 lat/lng，支持多种格式
    function extractCoord(item) {
      // 1) 优先用 gps 字段
      if (item.gps && item.gps !== '-') {
        // 支持 | 或 , 或 ,+空格 分隔
        const parts = item.gps.split(/[｜|,，]\s*/)
        if (parts.length >= 2) {
          const lat = parseFloat(parts[0])
          const lng = parseFloat(parts[1])
          if (!isNaN(lat) && !isNaN(lng)) return { lat, lng, src: 'gps' }
        }
      }
      // 2) 回退：从 lorastr 提取第3段（格式: type|v3-x|lat,lng|...）
      if (item.lorastr) {
        const segs = item.lorastr.split(/[｜|]/)
        if (segs.length >= 3 && segs[2]) {
          const parts = segs[2].split(/[,，]\s*/)
          if (parts.length >= 2) {
            const lat = parseFloat(parts[0])
            const lng = parseFloat(parts[1])
            if (!isNaN(lat) && !isNaN(lng)) return { lat, lng, src: 'lorastr' }
          }
        }
      }
      return null
    }

    const markers = []
    lotList.forEach((item, index) => {
      const info = deviceInfoMap[item.deviceId]
      // 过滤：只显示 visible=true 且 没有ProductKey 的设备
      if (!info || !info.visible || info.hasProductKey) return

      const coord = extractCoord(item)
      if (!coord) return
      const gcj = wgs84ToGcj02(coord.lng, coord.lat)
      const rename = info.rename || ''
      let labelText = (item.deviceId || '-').substring(0, 10) + (rename ? '（' + rename + '）' : '')
      var devAnchorX = Math.max(-30, Math.min(130, labelText.length * 12 - 30))

      // if (labelText && labelText.length > 15) {
      //   labelText = labelText.substring(0, 15) + '...'
      // }
      devAnchorX=((labelText.length) * 11.0)/2 +15

      // 图标颜色：
      //   绿色(#00C853) = 最后定位(GPS)在 1 小时内
      //   浅绿色(#66BB6A) = GPS 超过 1 小时，但最近 1 小时内有对时同步记录（设备仍在活，只是没更新位置）
      //   灰色 = GPS 超过 1 小时且最近 1 小时内无对时（设备长时间失联）
      const stale = this._deviceIsStale(item.rawTime || item.time || '')
      const recentSync = this._deviceHasRecentSync(item.deviceId)
      let iconPath = this._deviceIconPath || ''
      if (stale) {
        iconPath = recentSync
          ? (this._deviceLightGreenIconPath || this._deviceGrayIconPath || this._deviceIconPath || '')
          : (this._deviceGrayIconPath || this._deviceIconPath || '')
      }
      const syncInfo = (this._deviceSyncMap || {})[item.deviceId]
      const syncRaw = (syncInfo && (syncInfo.rawTime || syncInfo.time)) || '-'

      markers.push({
        id: index + 50000,
        latitude: gcj.lat,
        longitude: gcj.lng,
        width: 28,
        height: 28,
        iconPath: iconPath,
        title: '设备 ' + (item.deviceId || '-'),
        callout: {
          content: '设备:' + (item.deviceId || '-') + '\nGPS:' + coord.lat + ',' + coord.lng + '\n更新:' + (item.rawTime || '-') + '\n对时:' + syncRaw,
          display: 'BYCLICK',
          textAlign: 'center',
          fontSize: 13,
          padding: 8,
          borderRadius: 6
        },
        label: {
          content: labelText,
          color: '#333333',
          fontSize: 14,
          bgColor: '#ffffff',
          borderColor: '#999999',
          borderWidth: 1,
          borderRadius: 4,
          padding: 2,
          maxWidth: 300,
          anchorX: devAnchorX,
          anchorY: -25,
          textAlign: 'center'
        }
      })
    })
    console.log('[地图] 设备标记点:', markers.length, '个')
    this._deviceMarkers = markers
  },

  // ==================== 合并标记点 ====================

  _applyAllMarkers() {
    const base = [...(this._cowMarkers || []), ...(this._deviceMarkers || [])]
    const places = this.data.showRoadLayer ? (this._placeMarkers || []) : []
    const activeId = this.data.activeCalloutId
    // 深拷贝并保留稳定 id（牛/设备/地名在各自生成时已分配不冲突 id），
    // 仅根据 activeCalloutId 控制唯一气泡显隐，避免 id 重排导致“点 A 显 B”
    const all = [...base, ...places].map(m => {
      const clone = { ...m }
      if (m.callout) {
        clone.callout = {
          ...m.callout,
          display: (activeId !== -1 && m.id === activeId) ? 'ALWAYS' : 'BYCLICK'
        }
      }
      return clone
    })

    console.log('[地图] 合并标记点总数:', all.length)
    this.setData({ markers: all, currentMarker: -1 })
  },

  fetchCrowData() {
    const crowAllData = {
      time: new Date().toLocaleString(),
      action: "getCowTableAll",
      info: { wechatid: getApp().getWechatId() }
    }
    console.log('地图页 POST发送数据:', crowAllData)
    wx.request({
      url: app.globalData.api_cowsheep_Url,
      method: 'POST',
      data: crowAllData,
      timeout: 10000,
      success: (res) => {
        const data = res.data
        console.log('地图页返回原始数据:', JSON.stringify(data))
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
          const crow_id = attr.crowid || record.crowid || record.crow_id || record.crowId || record.crow_idx || '-'
          const crow_idx = attr.crow_idx || record.crow_idx || crow_id
          const gps = attr.gps || record.gps || '-'
          const time = attr.time || record.time || '-'
          return { crow_id, crow_idx, gps, time }
        })
        console.log('地图页最终 recordList:', JSON.stringify(recordList))
        this.renderMarkersFromData(recordList)
        wx.hideLoading()
      },
      fail: (err) => {
        console.error('地图页请求牛群数据失败:', JSON.stringify(err))
        wx.hideLoading()
        wx.showToast({ title: '牛群数据加载失败', icon: 'none' })
      }
    })
  },

  loadMap() {
    const that = this
    wx.getLocation({
      type: 'gcj02',
      success: (res) => {
        that.setData({
          nativeLat: res.latitude,
          nativeLng: res.longitude,
          showNativeMap: true
        })
        that._refreshOverlays(res.latitude, res.longitude, that.data.nativeScale)
      },
      fail: () => {
        that.setData({
          nativeLat: 26.529950, nativeLng: 109.390224,
          showNativeMap: true
        })
        that._refreshOverlays(26.529950, 109.390224, that.data.nativeScale)
      }
    })
  },

  // 回到我的位置
  moveToMyLocation() {
    const that = this
    wx.getLocation({
      type: 'gcj02',
      success: (res) => {
        const mapCtx = wx.createMapContext('cowMap')
        mapCtx.moveToLocation({ latitude: res.latitude, longitude: res.longitude })
        that.setData({
          nativeLat: res.latitude, nativeLng: res.longitude
        })
        that._refreshOverlays(res.latitude, res.longitude, that.data.nativeScale)
        wx.showToast({ title: '已定位', icon: 'success', duration: 1000 })
      },
      fail: () => { wx.showToast({ title: '定位失败', icon: 'error' }) }
    })
  },

  onToolBtn2() {
    this._cowMarkers = []
    this._deviceMarkers = []
    this._pendingCrowData = null
    this._pendingDeviceArgs = null
    this.setData({ markers: [] })
    wx.showLoading({ title: '刷新中...' })
    this.fetchCrowData()
    this.fetchDeviceLotData()
  },

  // 手势缩放/拖动 → 刷新瓦片
  onRegionChange(e) {
    if (e.type !== 'end') return
    if (this._refreshingTiles) {
      console.log('[overlay] regionChange 被忽略（瓦片加载中）')
      return
    }
    const mapCtx = wx.createMapContext('cowMap')
    const isProgrammatic = e.causedBy === 'update'
    const that = this

    mapCtx.getRegion({
      success: (region) => {
        const sw = region.southwest || {}
        const ne = region.northeast || {}
        const swLat = parseFloat(sw.latitude) || 0
        const swLng = parseFloat(sw.longitude) || 0
        const neLat = parseFloat(ne.latitude) || 0
        const neLng = parseFloat(ne.longitude) || 0
        const cLat = (swLat + neLat) / 2
        const cLng = (swLng + neLng) / 2

        const doRefresh = function(newScale) {
          console.log('[overlay] regionChange 刷新瓦片: lat=', cLat.toFixed(4), 'lng=', cLng.toFixed(4), 'scale=', newScale)
          that._refreshOverlays(cLat, cLng, newScale)
        }

        if (isProgrammatic) return
        mapCtx.getScale({
          success: function(res) {
            doRefresh(res.scale)
          },
          fail: function() {
            doRefresh(that.data.nativeScale)
          }
        })
      }
    })
  },

  // 逐个巡览标记点：点击后地图中心移到下一个红点
  nextMarker() {
    const { markers, currentMarker } = this.data
    if (!markers || markers.length === 0) {
      wx.showToast({ title: '暂无标记点', icon: 'none' })
      return
    }
    const next = (currentMarker + 1) % markers.length
    const marker = markers[next]

    const mapCtx = wx.createMapContext('cowMap')
    mapCtx.moveToLocation({
      latitude: marker.latitude,
      longitude: marker.longitude
    })
    this.setData({
      nativeLat: marker.latitude,
      nativeLng: marker.longitude,
      currentMarker: next
    })
    this._refreshOverlays(marker.latitude, marker.longitude, this.data.nativeScale)
    // 弹起该点的 callout 信息气泡
    setTimeout(() => {
      const mapCtx = wx.createMapContext('cowMap')
      mapCtx.includePoints({
        points: [{ latitude: marker.latitude, longitude: marker.longitude }],
        padding: [0, 0, 0, 0]
      })
    }, 300)

    wx.showToast({
      title: (marker.title || '点位') + ' (' + (next + 1) + '/' + markers.length + ')',
      icon: 'none',
      duration: 1000
    })
  },

  // 点击 marker：展开该点气泡，同时收起其它气泡（地图中心最多只显示一个气泡）
  onMarkerTap(e) {
    const markerId = e && e.detail ? e.detail.markerId : -1
    if (markerId === -1) return
    // 部分平台点击气泡会连带触发 markertap，隐藏后短暂忽略，避免"刚隐藏又显示"
    if (Date.now() - (this._lastCalloutHideTs || 0) < 350) return
    this.setData({ activeCalloutId: markerId }, () => {
      this._applyAllMarkers()
    })
  },

  // 点击已展开的气泡：收起气泡
  onCalloutTap(e) {
    const markerId = e && e.detail ? e.detail.markerId : -1
    if (markerId === -1 || markerId !== this.data.activeCalloutId) return
    this._lastCalloutHideTs = Date.now()
    this.setData({ activeCalloutId: -1 }, () => {
      this._applyAllMarkers()
    })
  },

  toggleMapType() {
    // 切换腾讯卫星图/标准地图
    const next = !this.data.isSatellite
    this.setData({ isSatellite: next })
    wx.showToast({ title: next ? '卫星图' : '标准地图', icon: 'none', duration: 1000 })
  },

  toggleLayer() {
    if (!this._roadFetched || !this._placeFetched) {
      // 首次点击：并行请求道路 + 地名数据
      if (!this._roadFetched) this.fetchRoadData()
      if (!this._placeFetched) this.fetchPlaceData()
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

  /**
   * 通用图钉绘制：canvas选择器 → fillColor/strokeColor → 导图
   */
  _drawPin(canvasSelector, fillColor, strokeColor, targetPath, cb, triangleColor) {
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

      // 内部倒三角：默认与描边同色；如需单独指定颜色可传 triangleColor
      ctx.beginPath()
      ctx.moveTo(cx - 5, cy - 5)
      ctx.lineTo(cx, cy + 5)
      ctx.lineTo(cx + 5, cy - 5)
      ctx.closePath()
      ctx.fillStyle = triangleColor || fillColor
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

  /**
   * 按等级刷新道路折线和地名标记
   * @param {number} level - 0=隐藏，1..maxLevel=显示 level<=该值的所有项
   */
  _applyLevel(level) {
    const show = level > 0

    // 过滤道路
    const filteredRoads = show
      ? this._fullRoadList.filter(r => (parseInt(r.level) || 1) <= level)
      : []
    this._buildRoadPolylines(filteredRoads)

    // 过滤地名
    const filteredPlaces = show
      ? this._fullPlaceList.filter(p => (parseInt(p.level) || 1) <= level)
      : []
    this._buildPlaceMarkers(filteredPlaces)

    const label = show ? ('Lv.' + level) : '图层'

    this.setData({
      showRoadLayer: show,
      currentLevel: level,
      layerLabel: label,
      polylines: show ? this._roadPolylines : []
    })
    this._applyAllMarkers()
    wx.showToast({
      title: show ? ('已显示等级 ≤' + level) : '图层已隐藏',
      icon: 'none',
      duration: 1000
    })
  },

  fetchRoadData() {
    wx.showLoading({ title: '加载道路...' })
    const that = this
    dataCache.getRoadListFromCache((cachedData) => {
      wx.hideLoading()
      const roadList = cachedData.roadList || []
      if (roadList.length === 0) {
        wx.showToast({ title: '暂无道路数据', icon: 'none' })
        that._roadFetched = true
        that._tryInitLevel()
        return
      }
      console.log('[道路] 已解析:', roadList.length, '条（缓存优先）')
      that._fullRoadList = roadList
      that._roadFetched = true
      that._tryInitLevel()
    })
  },

  // ==================== 地名黄点 ====================

  /**
   * 从单条 GPS 字符串中提取 lat/lng
   * 兼容: "lat|lng" 或 "lat,lng" 或 "lat, lng"
   */
  _parseSingleGPS(gpsStr) {
    if (!gpsStr || gpsStr === '-') return null
    const parts = gpsStr.split(/[｜|,，]\s*/)
    if (parts.length < 2) return null
    const lat = parseFloat(parts[0])
    const lng = parseFloat(parts[1])
    if (isNaN(lat) || isNaN(lng)) return null
    return { lat, lng }
  },

  /**
   * 用 Canvas 绘制牛群定位图钉图标（蓝色），固定路径，每次覆盖不累积
   */
  _generateCowPin() {
    const that = this
    const targetPath = (wx.env.USER_DATA_PATH || '') + '/cow_pin.png'
    this._drawPin('#cowPinCanvas', '#2979FF', '#0D47A1', targetPath, (filePath) => {
      that._cowIconPath = filePath
      that._cowIconReady = true
      // 如果牛群数据先返回、图标后生成，在这里补渲染
      if (that._pendingCrowData) {
        const list = that._pendingCrowData
        that._pendingCrowData = null
        that.renderMarkersFromData(list)
        return
      }
      // 兜底：已生成的标记点 iconPath 为空时补上
      if ((that._cowMarkers || []).length > 0) {
        that._cowMarkers.forEach(m => { m.iconPath = that._cowIconPath })
        that._applyAllMarkers()
      }
    })
  },

  /**
   * 用 Canvas 绘制设备定位图钉图标，固定路径，每次覆盖不累积
   * 绿色（#00C853）：最后定位(GPS)时间在 1 小时内
   * 浅绿色（#66BB6A）：GPS 超过 1 小时，但最近 1 小时内有对时同步记录（圆圈浅绿，内部倒三角灰色）
   * 灰色（#9E9E9E）：GPS 超过 1 小时且最近无对时（长时间未上报）
   * 三个图标都生成完成后才允许渲染设备标记点，避免出现"空 iconPath 红点"或漏色图标
   */
  _generateDevPin() {
    const that = this
    const greenPath = (wx.env.USER_DATA_PATH || '') + '/dev_pin.png'
    const grayPath = (wx.env.USER_DATA_PATH || '') + '/dev_pin_gray.png'
    const lightGreenPath = (wx.env.USER_DATA_PATH || '') + '/dev_pin_lightgreen.png'
    let done = 0
    const finish = function() {
      done++
      if (done < 3) return
      that._devIconReady = true
      // 如果设备数据先返回、图标后生成，在这里补渲染
      if (that._pendingDeviceArgs) {
        const { lotList, deviceInfoMap } = that._pendingDeviceArgs
        that._pendingDeviceArgs = null
        that._deviceLotList = lotList
        that._deviceInfoMap = deviceInfoMap
        that._renderDeviceMarkers(lotList, deviceInfoMap)
        that._applyAllMarkers()
        return
      }
      // 兜底：已生成的标记点 iconPath 为空时按最新数据重算并刷新地图
      if ((that._deviceMarkers || []).length > 0) {
        that._renderDeviceMarkers(that._deviceLotList || [], that._deviceInfoMap || {})
        that._applyAllMarkers()
      }
    }
    // 绿色图钉：GPS 1 小时内正常在线
    this._drawPin('#devPinCanvas', '#00C853', '#1B5E20', greenPath, (filePath) => {
      that._deviceIconPath = filePath
      finish()
    })
    // 灰色图钉：GPS 超过 1 小时且最近无对时（长时间未上报）
    this._drawPin('#devGrayPinCanvas', '#9E9E9E', '#616161', grayPath, (filePath) => {
      that._deviceGrayIconPath = filePath
      finish()
    })
    // 浅绿色图钉：GPS 超过 1 小时但最近 1 小时内有对时同步记录（设备仍在活）
    // 圆圈用浅绿 #66BB6A，内部倒三角单独改为灰色（示意位置信息已偏旧）
    this._drawPin('#devLightGreenPinCanvas', '#66BB6A', '#2E7D32', lightGreenPath, (filePath) => {
      that._deviceLightGreenIconPath = filePath
      finish()
    }, '#9E9E9E')
  },

  /**
   * 生成"黑描边白字 + 红色圆点"组合图钉 PNG
   * 文字在上（带黑色描边），红点在下——微信 callout/label 均不支持文字描边，
   * 只能用 canvas 把文字画进图标，这样文本描边是真实渲染的
   * @param {string} name   地名
   * @param {number} idx    序号（用于唯一文件名）
   * @param {function} cb   (iconPath, W, H, anchorY) 生成完成回调
   */
  _generatePlaceTextPin(name, idx, cb) {
    const query = wx.createSelectorQuery()
    query.select('#placeTextCanvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) return cb('', 0, 0, 0)
      const canvas = res[0].node
      const ctx = canvas.getContext('2d')
      const dpr = wx.getSystemInfoSync().pixelRatio
      const fontSize = 13
      const dotH = 28                    // 红点区高度（与设备/牛图标同规格）
      const textH = 24                   // 文字区高度
      const textW = Math.ceil(name.length * fontSize * 1.05) + 10  // 文字宽度估算
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

      // ---- 下半部：红色定位图钉（圆头尖尾，尖端对准坐标点） ----
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

      const filePath = (wx.env.USER_DATA_PATH || '') + '/place_' + idx + '.png'
      wx.canvasToTempFilePath({
        canvas: canvas,
        fileType: 'png',
        filePath: filePath,
        success: (fileRes) => cb(fileRes.tempFilePath, W, H, tipY / H),
        fail: () => cb('', 0, 0, 0)
      })
    })
  },

  fetchPlaceData() {
    const that = this

    dataCache.getPlaceListFromCache((cachedData) => {
      const placeList = cachedData.placeList || []
      if (placeList.length === 0) {
        console.log('[地名] 暂无数据')
        that._placeFetched = true
        that._tryInitLevel()
        return
      }
      console.log('[地名] 已解析:', placeList.length, '条（缓存优先）')
      that._fullPlaceList = placeList
      that._placeFetched = true
      that._tryInitLevel()
    })
  },

  _buildPlaceMarkers(placeList) {
    const that = this
    const ID_BASE = 90000

    // 先把每个地名的基础数据解析出来
    const items = []
    placeList.forEach((place, index) => {
      const coord = this._parseSingleGPS(place.gps)
      if (!coord) return
      const gcj = wgs84ToGcj02(coord.lng, coord.lat)
      const name = place.name || place.placeid || '-'
      items.push({ id: ID_BASE + index, gcj, name })
    })
    if (items.length === 0) {
      this._placeMarkers = []
      return
    }

    // 逐个生成"描边文字+红点"PNG，全部完成后再合并渲染
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
            // 红点圆心对准坐标点（anchorY = 红点圆心在整图中的相对位置）
            anchor: { x: 0.5, y: anchorY }
          })
        }
        done++
        if (done === items.length) {
          console.log('[地名] 生成完成:', markers.length, '个（共', items.length, '个）')
          that._placeMarkers = markers
          // 图层开启状态才需要刷新地图
          if (that.data.showRoadLayer) that._applyAllMarkers()
        }
      })
    })
  },

  /**
   * 道路和地名都请求完成后，计算 maxLevel 并初始显示 level=1
   */
  _tryInitLevel() {
    if (!this._roadFetched || !this._placeFetched) return
    if (this.data.currentLevel > 0) return

    // 计算道路和地名中 level 的最大值
    let maxLevel = 0
    const allItems = [...this._fullRoadList, ...this._fullPlaceList]
    allItems.forEach(item => {
      const lv = parseInt(item.level) || 1
      if (lv > maxLevel) maxLevel = lv
    })
    if (maxLevel < 1) maxLevel = 1

    this.setData({ maxLevel })
    console.log('[图层] maxLevel =', maxLevel)
    this._applyLevel(1)
  },

  /**
   * 解析 roadinfo 中的 GPS 坐标，构建绿色 polyline
   * roadinfo 格式兼容：
   *   lat1,lng1|lat2,lng2|...        (逗号分隔经纬度，竖线分隔点)
   *   lat1|lng1|lat2|lng2|...        (竖线交替)
   *   lat1,lng1;lat2,lng2;...        (分号分隔点)
   */
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

  // ==================== 全体工作期间中继：GPS定时指令(gpstim=30) ====================
  // 中继 = 设备表中带 ProductKey 的设备（太阳能DTU）。中继只在其"开机时间窗口"（工作期间）内
  // 处于工作状态，故只向当前处于工作期间的中继下发指令，发送方式与 中继DTU指令页(relay-dtu-cmd) 一致。
  onGpstimAllTap() {
    const that = this
    wx.showLoading({ title: '查询中继...' })
    this._loadRelaysAndWorkPeriod((relays, workRelays) => {
      wx.hideLoading()
      if (relays.length === 0) {
        wx.showToast({ title: '未找到可用中继（无ProductKey/密钥）', icon: 'none' })
        return
      }
      if (workRelays.length === 0) {
        wx.showToast({ title: '当前没有处于工作期间的中继', icon: 'none' })
        return
      }
      const cmdText = JSON.stringify({ cmd: 'gpstim', value: 30 })
      wx.showModal({
        title: '全体中继 GPS 指令',
        content: '将向 ' + workRelays.length + ' 台工作期间的中继发送：\n' + cmdText + '\n（共 ' + relays.length + ' 台中继）',
        confirmText: '发送',
        cancelText: '取消',
        success: (res) => {
          if (res.confirm) that._broadcastCmdToRelays(workRelays, cmdText)
        }
      })
    })
  },

  // 加载设备列表 + 设备配置，筛出全部中继与当前"工作期间"的中继
  _loadRelaysAndWorkPeriod(callback) {
    let devData = null
    let cfgData = null
    let done = 0
    const finish = () => {
      done++
      if (done < 2) return
      const recordList = (devData && devData.recordList) || []
      const configMap = (cfgData && cfgData.configMap) || {}
      const seen = {}
      const relays = []
      recordList.forEach(r => {
        if (!r.deviceId || r.deviceId === '-') return
        if (seen[r.deviceId]) return
        // 中继设备：带 ProductKey + DeviceName（云密钥），与 中继DTU指令页 过滤一致
        if (!r.ProductKey || r.ProductKey === '-' || !r.DeviceName) return
        seen[r.deviceId] = true
        relays.push(r)
      })
      const now = new Date()
      const workRelays = relays.filter(r => this._isRelayInWorkPeriod(r.deviceId, configMap, now))
      console.log('[地图] 中继总数:', relays.length, '，工作期间中继:', workRelays.length,
        workRelays.map(r => r.deviceId).join(','))
      callback(relays, workRelays)
    }
    dataCache.getDeviceList((d) => { devData = d; finish() })
    dataCache.getDeviceConfigAll((d) => { cfgData = d; finish() })
  },

  // 判断中继当前是否处于工作期间：取其配置"开机时间窗口"（lorastr 第3段第2项，两位代号/旧格式均可）
  // 当前时间在窗口内=工作；无配置/无窗口=视为全天工作（与设备列表页 isDormant 判断一致）
  _isRelayInWorkPeriod(deviceId, configMap, now) {
    const cfg = (configMap && configMap[deviceId]) || null
    const lorastr = (cfg && cfg.lorastr) || ''
    if (!lorastr) return true
    const parts = String(lorastr).split('|')
    if (parts.length < 3 || !parts[2]) return true
    const cfgParts = parts[2].split(',')
    if (cfgParts.length < 2 || !cfgParts[1]) return true
    const win = timeWindowCodec.parseTimeWindow(cfgParts[1].trim())
    if (!win) return true
    const d = now || new Date()
    const currentMinutes = d.getHours() * 60 + d.getMinutes()
    const startMinutes = win.start * 60
    // end=23 代表 23:59
    const endMinutes = win.end === 23 ? 23 * 60 + 59 : win.end * 60
    return currentMinutes >= startMinutes && currentMinutes < endMinutes
  },

  // 并发向多台中继下发同一指令（消息注入该中继自身 deviceId，与 relay-dtu-cmd 一致），
  // 统一管理 loading 与成功/失败统计
  _broadcastCmdToRelays(relayList, cmdText) {
    const total = relayList.length
    let done = 0
    let okCount = 0
    const failed = []

    wx.showLoading({ title: '发送中(' + total + '台)...' })

    const finish = () => {
      if (done < total) return
      wx.hideLoading()
      if (failed.length === 0) {
        wx.showToast({ title: '已发送 ' + total + ' 台中继', icon: 'success' })
      } else if (failed.length === total) {
        wx.showToast({ title: '全部发送失败', icon: 'error' })
      } else {
        wx.showToast({ title: okCount + ' 台成功 ' + failed.length + ' 台失败', icon: 'none' })
      }
    }

    relayList.forEach(r => {
      // 解析指令并注入目标设备ID（即该中继自身）
      let msgObj
      try {
        msgObj = JSON.parse(cmdText)
      } catch (e) {
        msgObj = { text: cmdText }
      }
      msgObj.deviceId = r.deviceId
      const payload = {
        action: 'com',
        deviceName: r.DeviceName,
        productKey: r.ProductKey,
        msg: JSON.stringify(msgObj),
        timestamp: Date.now(),
        info: { wechatid: getApp().getWechatId() }
      }
      console.log('[地图] 中继GPS指令 → ' + r.deviceId + ': ' + payload.msg)

      wx.request({
        url: RELAY_FC_URL,
        method: 'POST',
        data: payload,
        timeout: 10000,
        success: (res) => {
          done++
          okCount++
          console.log('[地图] 中继 ' + r.deviceId + ' 返回:', JSON.stringify(res.data))
          finish()
        },
        fail: (err) => {
          done++
          failed.push(r.deviceId)
          console.error('[地图] 中继 ' + r.deviceId + ' 发送失败:', err)
          finish()
        }
      })
    })
  },

  // 页面销毁：停止设备图标老化检测定时器
  onUnload() {
    if (this._staleTimer) {
      clearInterval(this._staleTimer)
      this._staleTimer = null
    }
  }
})
