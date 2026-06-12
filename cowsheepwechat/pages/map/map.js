// map.js
const dataCache = require('../../config/data-cache.js')

const ROAD_API_URL = 'https://gpsmoveinfo.cn/fc/route_place'

Page({
  data: {
    scale: 1,
    baiduMapUrl: '',
    useBaidu: false,
    coords: { lng: 109.390224, lat: 26.529950 },
    showNativeMap: false,
    nativeLat: 26.529950,
    nativeLng: 109.390224,
    nativeScale: 15,
    markers: [],
    polylines: [],
    showRoadLayer: false,
    baiduAK: '',  // ⚠️ 填入百度 AK
    baiduZoom: 16,     // 当前卫星图 zoom 级别（限制<=16避免无图）
    zoomRetries: 0,    // zoom 降级重试次数
    isSatellite: true,  // 原生map卫星图开关
    lockScale: false,    // 防止 regionchange 递归触发
    currentMarker: -1    // 当前巡览到的 marker 索引，-1=未选中
  },

  _cowMarkers: [],
  _deviceMarkers: [],
  _roadPolylines: [],   // 缓存已构建的道路折线数据
  _roadFetched: false,  // 是否已请求过道路数据
  _placeFetched: false, // 是否已请求过地名数据
  _placeMarkers: [],    // 缓存已构建的地名图钉标记
  _pinIconPath: '',     // 地名蓝色图钉图标路径
  _deviceIconPath: '',  // 设备红色圆点图标路径

  onLoad() {
    this._generateDeviceDot()  // 提前生成设备图标
    this.loadMap()
    this.fetchCrowData()
    this.fetchDeviceLotData()
  },

  buildBaiduUrl(lng, lat, zoom) {
    const { baiduAK } = this.data
    if (!baiduAK) return ''
    const info = wx.getSystemInfoSync()
    const size = Math.max(info.windowWidth, info.windowHeight)
    // 限制 zoom <= 16，避免百度返回"此区域无卫星图"占位图
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

  gcjToBd(lng, lat) {
    const x = +lng, y = +lat
    const z = Math.sqrt(x * x + y * y) + 0.00002 * Math.sin(y * Math.PI)
    const theta = Math.atan2(y, x) + 0.000003 * Math.cos(x * Math.PI)
    return {
      lng: (z * Math.cos(theta) + 0.0065).toFixed(6),
      lat: (z * Math.sin(theta) + 0.006).toFixed(6)
    }
  },

  // WGS-84 → GCJ-02（火星坐标系）
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

  // WGS-84 → BD-09（百度坐标系）
  wgs84ToBd09(lng, lat) {
    const gcj = this.wgs84ToGcj02(lng, lat)
    return this.gcjToBd(gcj.lng, gcj.lat)
  },

  // 统一的标记点渲染：接收 recordList，生成 markers 并加入合并
  renderMarkersFromData(recordList) {
    if (!recordList || recordList.length === 0) {
      this._cowMarkers = []
      this._applyAllMarkers()
      return
    }
    // 归一化 recordList，兼容 features 页传入的格式（crow_id/gps/rawTime）
    const normalized = recordList.map(item => ({
      crow_id: item.crow_id || item.crow_idx || '-',
      crow_idx: item.crow_idx || item.crow_id || '-',
      gps: item.gps || '-',
      time: item.time || item.rawTime || '-'
    }))
    console.log('renderMarkersFromData 归一化:', JSON.stringify(normalized))

    // 获取当前时间（只显示时分秒）
    const now = new Date()
    const currentTime = [now.getHours(), now.getMinutes(), now.getSeconds()]
      .map(n => String(n).padStart(2, '0'))
      .join(':')

    // 将记录转换为地图标记点
    const markers = normalized
      .filter(item => item.gps !== '-')
      .map((item, index) => {
        const parts = item.gps.split(/[｜|]/)
        if (parts.length < 2) return null
        const wgsLat = parseFloat(parts[0])
        const wgsLng = parseFloat(parts[1])
        if (isNaN(wgsLat) || isNaN(wgsLng)) return null
        const gcj = this.wgs84ToGcj02(wgsLng, wgsLat)
        return {
          id: index,
          latitude: gcj.lat,
          longitude: gcj.lng,
          width: 30,
          height: 30,
          iconPath: this._deviceIconPath || '',
          title: '牛群 ' + item.crow_id,
          callout: {
            content: 'ID:' + item.crow_id + '\n时间:' + currentTime,
            display: 'BYCLICK',
            textAlign: 'center'
          },
          label: {
            content: item.crow_idx + '\n' + currentTime,
            color: '#ffffff',
            fontSize: 13,
            anchorX: 0,
            anchorY: 3,
            textAlign: 'center'
          }
        }
      })
      .filter(item => item !== null)

    console.log('[地图] 牛群标记点:', markers.length, '个')
    this._cowMarkers = markers
    this._applyAllMarkers()
  },

  // ==================== 设备 LOT 标记点 ====================

  fetchDeviceLotData() {
    dataCache.getDeviceLotRefresh((data) => {
      const lotList = data.lotList || []
      console.log('[地图] 设备LOT数据:', lotList.length, '条')
      this._renderDeviceMarkers(lotList)
      this._applyAllMarkers()
      wx.hideLoading()
    }, true)
  },

  _renderDeviceMarkers(lotList) {
    if (!lotList || lotList.length === 0) {
      this._deviceMarkers = []
      return
    }

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
      const coord = extractCoord(item)
      if (!coord) return
      const gcj = this.wgs84ToGcj02(coord.lng, coord.lat)
      const shortId = (item.deviceId || '-').substring(0, 10)
      markers.push({
        id: index + 50000,
        latitude: gcj.lat,
        longitude: gcj.lng,
        width: 30,
        title: '设备 ' + (item.deviceId || '-'),
        callout: {
          content: '设备:' + (item.deviceId || '-') + '\nGPS:' + coord.lat + ',' + coord.lng,
          display: 'BYCLICK',
          textAlign: 'center'
        },
        label: {
          content: shortId,
          color: '#ffffff',
          fontSize: 13,
          anchorX: 0,
          anchorY: 3,
          textAlign: 'center'
        }
      })
    })
    console.log('[地图] 设备标记点:', markers.length, '个')
    this._deviceMarkers = markers
  },

  // ==================== 合并标记点并统一调整邻近标签 ====================

  _applyAllMarkers() {
    const base = [...(this._cowMarkers || []), ...(this._deviceMarkers || [])]
    // 图层开启时追加地名黄点（放在末尾，不参与邻近标签分散）
    const places = this.data.showRoadLayer ? (this._placeMarkers || []) : []
    const all = [...base, ...places]
    all.forEach((m, i) => { m.id = i })

    // 邻近标记点的标签围绕红点均匀分布，避免文字叠压
    const PROXIMITY = 0.001
    const LABEL_RADIUS = 26

    function getAngle(N, posIdx) {
      if (N === 1) return 180
      if (N === 2) return [90, 270][posIdx]
      if (N === 3) return [180, 60, 300][posIdx]
      if (N === 4) return [180, 0, 90, 270][posIdx]
      const step = 360 / N
      return (180 + posIdx * step) % 360
    }

    const visited = new Array(all.length).fill(false)
    // 地名黄点不参与邻近标签分散，保持 label 在正下方
    for (let i = base.length; i < all.length; i++) { visited[i] = true }
    for (let i = 0; i < all.length; i++) {
      if (visited[i]) continue
      const cluster = [i]
      visited[i] = true
      let expanded = true
      while (expanded) {
        expanded = false
        for (let j = 0; j < all.length; j++) {
          if (visited[j]) continue
          for (const ci of cluster) {
            if (Math.abs(all[ci].latitude - all[j].latitude) < PROXIMITY &&
                Math.abs(all[ci].longitude - all[j].longitude) < PROXIMITY) {
              cluster.push(j)
              visited[j] = true
              expanded = true
              break
            }
          }
        }
      }
      const useRadius = cluster.length === 1 ? 7 : LABEL_RADIUS
      cluster.forEach((mi, posIdx) => {
        const angleDeg = getAngle(cluster.length, posIdx)
        const rad = angleDeg * Math.PI / 180
        all[mi].label.anchorX = Math.round(useRadius * Math.sin(rad))
        all[mi].label.anchorY = -Math.round(useRadius * Math.cos(rad))
      })
    }

    console.log('[地图] 合并标记点总数:', all.length)
    this.setData({ markers: all, currentMarker: -1 })
  },

  fetchCrowData() {
    const crowAllData = {
      time: new Date().toLocaleString(),
      action: "getcrowtableall"
    }
    console.log('地图页 POST发送数据:', crowAllData)
    wx.request({
      url: 'https://insertcrow-mlkndrhadh.cn-shanghai.fcapp.run',
      method: 'POST',
      data: crowAllData,
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
        console.error('地图页请求失败:', err)
        wx.hideLoading()
      }
    })
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
          })
        }
      },
      fail: () => {
        // 定位失败 → 默认湖南怀化基准点
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
          })
        }
      }
    })
  },

  onScale(e) {
    this.setData({ scale: e.detail.scale })
  },

  onImageError() {
    // 卫星图加载失败 → 逐级降低 zoom 重试，用低分辨率图放大显示
    const { baiduZoom, zoomRetries, coords } = this.data
    const fallbackZoom = baiduZoom - 2
    if (zoomRetries < 3 && fallbackZoom >= 4) {
      this.setData({
        baiduZoom: fallbackZoom,
        zoomRetries: zoomRetries + 1,
        baiduMapUrl: this.buildBaiduUrl(coords.lng, coords.lat, fallbackZoom)
      })
      wx.showToast({ title: '卫星图降级显示 (zoom ' + fallbackZoom + ')', icon: 'none', duration: 1200 })
      return
    }
    // 所有重试耗尽，切回原生地图
    wx.showToast({ title: '卫星图全部不可用，切换原生地图', icon: 'none' })
    this.setData({ showNativeMap: true, useBaidu: false })
  },

  // 回到我的位置
  moveToMyLocation() {
    const that = this
    wx.getLocation({
      type: 'gcj02',
      success: (res) => {
        if (that.data.showNativeMap) {
          // 原生地图：用 MapContext 移动
          const mapCtx = wx.createMapContext('cowMap')
          mapCtx.moveToLocation({
            latitude: res.latitude,
            longitude: res.longitude
          })
          that.setData({
            nativeLat: res.latitude,
            nativeLng: res.longitude
          })
        } else if (that.data.useBaidu && that.data.baiduAK) {
          // 百度静态图：重新生成图片 URL
          const bd = that.gcjToBd(res.longitude, res.latitude)
          that.setData({
            coords: bd,
            baiduMapUrl: that.buildBaiduUrl(bd.lng, bd.lat, 16),
            baiduZoom: 16,
            zoomRetries: 0
          })
        }
        wx.showToast({ title: '已定位', icon: 'success', duration: 1000 })
      },
      fail: () => {
        wx.showToast({ title: '定位失败', icon: 'error' })
      }
    })
  },

  onToolBtn2() {
    // 清掉旧标记，重新拉取最新数据
    this._cowMarkers = []
    this._deviceMarkers = []
    this.setData({ markers: [] })
    wx.showLoading({ title: '刷新中...' })
    this.fetchCrowData()
    this.fetchDeviceLotData()
  },

  // 监听原生 map 手势缩放：超过卫星图可用级别自动拉回
  onRegionChange(e) {
    if (e.type !== 'end' || e.causedBy !== 'gesture') return
    if (this.data.lockScale) return
    const mapCtx = wx.createMapContext('cowMap')
    mapCtx.getScale({
      success: (res) => {
        if (res.scale > 16) {
          this.setData({ lockScale: true })
          this.setData({ nativeScale: 16 })
          wx.showToast({ title: '已到卫星图最大级别', icon: 'none', duration: 1000 })
          // 解锁，避免阻塞下次手势
          setTimeout(() => this.setData({ lockScale: false }), 400)
        }
      },
      fail: () => {
        // getScale 失败不处理
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

    if (this.data.showNativeMap) {
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
    } else if (this.data.useBaidu && this.data.baiduAK) {
      const bd = this.gcjToBd(marker.longitude, marker.latitude)
      this.setData({
        coords: bd,
        baiduMapUrl: this.buildBaiduUrl(bd.lng, bd.lat, 16),
        baiduZoom: 16,
        zoomRetries: 0,
        currentMarker: next
      })
    }
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

  toggleMapType() {
    // 原生地图：卫星图 / 标准地图切换
    const next = !this.data.isSatellite
    this.setData({ isSatellite: next })
    wx.showToast({
      title: next ? '已切换卫星图' : '已切换标准地图',
      icon: 'none',
      duration: 1000
    })
  },

  toggleLayer() {
    if (!this._roadFetched) {
      // 首次点击：并行请求道路 + 地名数据
      this.fetchRoadData()
      this.fetchPlaceData()
      return
    }
    // 已加载：切换显隐
    const show = !this.data.showRoadLayer
    this.setData({
      showRoadLayer: show,
      polylines: show ? this._roadPolylines : []
    })
    this._applyAllMarkers()
    wx.showToast({
      title: show ? '道路图层已显示' : '道路图层已隐藏',
      icon: 'none',
      duration: 1000
    })
  },

  fetchRoadData() {
    wx.showLoading({ title: '加载道路...' })
    const that = this
    wx.request({
      url: ROAD_API_URL,
      method: 'POST',
      data: {
        time: new Date().toLocaleString(),
        action: 'getroutetableall'
      },
      success: (res) => {
        wx.hideLoading()
        console.log('[道路] 返回原始数据:', JSON.stringify(res.data))
        let rawList = []
        const data = res.data
        if (data && data.data && Array.isArray(data.data)) {
          rawList = data.data
        } else if (Array.isArray(data)) {
          rawList = data
        }
        if (rawList.length === 0) {
          wx.showToast({ title: '暂无道路数据', icon: 'none' })
          that._roadFetched = true
          return
        }
        // 解析每条记录
        const roadList = rawList.map(record => {
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
          return {
            route_id: attr.route_id || record.route_id || '-',
            roadname: attr.roadname || record.roadname || '',
            roadinfo: attr.roadinfo || record.roadinfo || ''
          }
        })
        console.log('[道路] 解析后:', roadList.length, '条')
        that._buildRoadPolylines(roadList)
        that._roadFetched = true
        // 默认显示
        that.setData({
          showRoadLayer: true,
          polylines: that._roadPolylines
        })
        that._applyAllMarkers()
        wx.showToast({
          title: '已加载 ' + roadList.length + ' 条道路',
          icon: 'none',
          duration: 1200
        })
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('[道路] 请求失败:', err)
        wx.showToast({ title: '道路数据加载失败', icon: 'error' })
      }
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
   * 用 Canvas 绘制红色圆点图标（设备/牛群用），返回临时文件路径
   */
  _generateDeviceDot() {
    const query = wx.createSelectorQuery()
    query.select('#deviceDotCanvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) return
      const canvas = res[0].node
      const ctx = canvas.getContext('2d')
      const dpr = wx.getSystemInfoSync().pixelRatio
      canvas.width = 30 * dpr
      canvas.height = 30 * dpr
      ctx.scale(dpr, dpr)

      // 红色实心圆 + 深色描边
      ctx.beginPath()
      ctx.arc(15, 15, 12, 0, 2 * Math.PI)
      ctx.fillStyle = '#F44336'
      ctx.fill()
      ctx.strokeStyle = '#B71C1C'
      ctx.lineWidth = 2
      ctx.stroke()

      // 内部高光小白点
      ctx.beginPath()
      ctx.arc(12, 11, 4, 0, 2 * Math.PI)
      ctx.fillStyle = '#FFCDD2'
      ctx.fill()

      wx.canvasToTempFilePath({
        canvas: canvas,
        success: (fileRes) => {
          this._deviceIconPath = fileRes.tempFilePath
          // 如果牛群标记已构建，刷新图标
          if ((this._cowMarkers || []).length > 0) {
            this._cowMarkers.forEach(m => { m.iconPath = this._deviceIconPath })
            this._applyAllMarkers()
          }
        }
      })
    })
  },

  /**
   * 用 Canvas 绘制经典定位图钉图标，返回临时文件路径
   */
  _generateYellowDot() {
    return new Promise((resolve) => {
      const query = wx.createSelectorQuery()
      query.select('#pinCanvas').fields({ node: true, size: true }).exec((res) => {
        if (!res || !res[0] || !res[0].node) {
          resolve('')
          return
        }
        const canvas = res[0].node
        const ctx = canvas.getContext('2d')
        const dpr = wx.getSystemInfoSync().pixelRatio
        canvas.width = 40 * dpr
        canvas.height = 40 * dpr
        ctx.scale(dpr, dpr)

        // 绘制蓝色定位图钉（泪滴形）
        const cx = 20, cy = 18, r = 13
        ctx.beginPath()
        ctx.arc(cx, cy, r, Math.PI, 0)          // 上半圆
        ctx.lineTo(cx, 34)                        // 右侧斜到尖端
        ctx.closePath()
        ctx.fillStyle = '#2979FF'
        ctx.fill()
        ctx.strokeStyle = '#0D47A1'
        ctx.lineWidth = 1.5
        ctx.stroke()

        // 内部白色小圆（高光）
        ctx.beginPath()
        ctx.arc(cx, cy - 2, 5, 0, 2 * Math.PI)
        ctx.fillStyle = '#ffffff'
        ctx.fill()

        wx.canvasToTempFilePath({
          canvas: canvas,
          success: (fileRes) => resolve(fileRes.tempFilePath),
          fail: () => resolve('')
        })
      })
    })
  },

  fetchPlaceData() {
    const that = this
    // 先生成图钉图标（仅首次）
    const iconPromise = this._pinIconPath
      ? Promise.resolve(this._pinIconPath)
      : this._generateYellowDot().then(path => {
          that._pinIconPath = path
          return path
        })

    iconPromise.then((iconPath) => {
      wx.request({
        url: ROAD_API_URL,
        method: 'POST',
        data: {
          time: new Date().toLocaleString(),
          action: 'getplacetableall'
        },
        success: (res) => {
          console.log('[地名] 返回原始数据:', JSON.stringify(res.data))
          let rawList = []
          const data = res.data
          if (data && data.data && Array.isArray(data.data)) {
            rawList = data.data
          } else if (Array.isArray(data)) {
            rawList = data
          }
          if (rawList.length === 0) {
            console.log('[地名] 暂无数据')
            that._placeFetched = true
            that._markBothReady()
            return
          }
          // 解析每条记录
          const placeList = rawList.map(record => {
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
            return {
              placeid: attr.placeid || record.placeid || '-',
              name: attr.name || record.name || '',
              gps: attr.gps || record.gps || ''
            }
          })
          console.log('[地名] 解析后:', placeList.length, '条')
          that._buildPlaceMarkers(placeList, iconPath)
          that._placeFetched = true
          that._markBothReady()
        },
        fail: (err) => {
          console.error('[地名] 请求失败:', err)
          that._placeFetched = true
          that._markBothReady()
        }
      })
    })
  },

  _buildPlaceMarkers(placeList, iconPath) {
    const markers = []
    // 地名图钉 ID 从 90000 起，避免与牛群(0~N)和设备(50000~N)冲突
    const ID_BASE = 90000
    placeList.forEach((place, index) => {
      const coord = this._parseSingleGPS(place.gps)
      if (!coord) return
      const gcj = this.wgs84ToGcj02(coord.lng, coord.lat)
      const name = place.name || place.placeid || '-'
      markers.push({
        id: ID_BASE + index,
        latitude: gcj.lat,
        longitude: gcj.lng,
        width: 30,
        height: 36,
        iconPath: iconPath || '',
        title: name,
        callout: {
          content: name + '\n' + coord.lat.toFixed(6) + ',' + coord.lng.toFixed(6),
          display: 'BYCLICK',
          textAlign: 'center',
          bgColor: '#FFD600',
          color: '#333333'
        },
        label: {
          content: name,
          color: '#ffffff',
          fontSize: 12,
          anchorX: 0,
          anchorY: 4,
          textAlign: 'center'
        }
      })
    })
    console.log('[地名] 构建图钉:', markers.length, '个')
    this._placeMarkers = markers
  },

  /**
   * 道路和地名都完成（或失败）后，刷新地图显示
   */
  _markBothReady() {
    if (this._roadFetched && this._placeFetched && this.data.showRoadLayer) {
      this._applyAllMarkers()
    }
  },

  /**
   * 解析 roadinfo 中的 GPS 坐标，构建绿色 polyline
   * roadinfo 格式兼容：
   *   lat1,lng1|lat2,lng2|...        (逗号分隔经纬度，竖线分隔点)
   *   lat1|lng1|lat2|lng2|...        (竖线交替)
   *   lat1,lng1;lat2,lng2;...        (分号分隔点)
   */
  _parseRoadPoints(roadinfo) {
    if (!roadinfo || roadinfo === '-') return []

    // Strategy 0: flat lat, lng, lat, lng, ... (逗号交替平铺)
    const commaAll = roadinfo.split(/[,，]\s*/)
    if (commaAll.length >= 4 && commaAll.length % 2 === 0) {
      const points = []
      let allValid = true
      for (let i = 0; i + 1 < commaAll.length; i += 2) {
        const lat = parseFloat(commaAll[i])
        const lng = parseFloat(commaAll[i + 1])
        if (isNaN(lat) || isNaN(lng)) { allValid = false; break }
        points.push({ lat, lng })
      }
      if (allValid && points.length >= 2) return points
    }

    // Strategy 1: split by | → for each segment try lat,lng
    const segs = roadinfo.split(/[｜|]/)
    if (segs.length >= 2) {
      const points = []
      for (const seg of segs) {
        const parts = seg.split(/[,，]\s*/)
        if (parts.length >= 2) {
          const lat = parseFloat(parts[0])
          const lng = parseFloat(parts[1])
          if (!isNaN(lat) && !isNaN(lng)) {
            points.push({ lat, lng })
          }
        }
      }
      if (points.length >= 2) return points
    }

    // Strategy 2: split by | → alternating lat|lng|lat|lng...
    if (segs.length >= 4) {
      const points = []
      for (let i = 0; i + 1 < segs.length; i += 2) {
        const lat = parseFloat(segs[i])
        const lng = parseFloat(segs[i + 1])
        if (!isNaN(lat) && !isNaN(lng)) {
          points.push({ lat, lng })
        }
      }
      if (points.length >= 2) return points
    }

    // Strategy 3: split by ; for point groups
    const semicolonSegs = roadinfo.split(';')
    if (semicolonSegs.length >= 2) {
      const points = []
      for (const seg of semicolonSegs) {
        const parts = seg.split(/[,，]\s*/)
        if (parts.length >= 2) {
          const lat = parseFloat(parts[0])
          const lng = parseFloat(parts[1])
          if (!isNaN(lat) && !isNaN(lng)) {
            points.push({ lat, lng })
          }
        }
      }
      if (points.length >= 2) return points
    }

    return []
  },

  _buildRoadPolylines(roadList) {
    const polylines = []
    roadList.forEach((road) => {
      const points = this._parseRoadPoints(road.roadinfo)
      if (points.length < 2) {
        console.warn('[道路] 坐标点不足，跳过:', road.roadname || road.route_id)
        return
      }
      // WGS-84 → GCJ-02 转换全部点
      const gcjPoints = points.map(p => {
        const gcj = this.wgs84ToGcj02(p.lng, p.lat)
        return { latitude: gcj.lat, longitude: gcj.lng }
      })
      polylines.push({
        points: gcjPoints,
        color: '#C8C8C8',
        width: 4,
        borderColor: '#808080',
        borderWidth: 1.2,
        borderWidth: 1.5,
        arrowLine: false,
        dottedLine: false
      })
    })
    console.log('[道路] 构建折线:', polylines.length, '条')
    this._roadPolylines = polylines
  },
})
