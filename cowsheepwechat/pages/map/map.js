// map.js
Page({
  data: {
    scale: 1,
    baiduMapUrl: '',
    useBaidu: false,
    coords: { lng: 116.397, lat: 39.908 },
    showNativeMap: false,
    nativeLat: 39.908,
    nativeLng: 116.397,
    nativeScale: 15,
    markers: [],
    baiduAK: '',  // ⚠️ 填入百度 AK
    baiduZoom: 16,     // 当前卫星图 zoom 级别（限制<=16避免无图）
    zoomRetries: 0,    // zoom 降级重试次数
    isSatellite: true,  // 原生map卫星图开关
    lockScale: false,    // 防止 regionchange 递归触发
    currentMarker: -1    // 当前巡览到的 marker 索引，-1=未选中
  },

  onLoad() {
    this.loadMap()
    this.fetchCrowData()
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

  // 统一的标记点渲染：接收 recordList，生成 markers 并 setData
  renderMarkersFromData(recordList) {
    if (!recordList || recordList.length === 0) {
      this.setData({ markers: [], currentMarker: -1 })
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

    const visited = new Array(markers.length).fill(false)
    for (let i = 0; i < markers.length; i++) {
      if (visited[i]) continue
      const cluster = [i]
      visited[i] = true
      let expanded = true
      while (expanded) {
        expanded = false
        for (let j = 0; j < markers.length; j++) {
          if (visited[j]) continue
          for (const ci of cluster) {
            if (Math.abs(markers[ci].latitude - markers[j].latitude) < PROXIMITY &&
                Math.abs(markers[ci].longitude - markers[j].longitude) < PROXIMITY) {
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
        markers[mi].label.anchorX = Math.round(useRadius * Math.sin(rad))
        markers[mi].label.anchorY = -Math.round(useRadius * Math.cos(rad))
      })
    }

    console.log('地图页标记点:', JSON.stringify(markers))
    this.setData({ markers, currentMarker: -1 })
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
        if (that.data.baiduAK) {
          const bd = that.gcjToBd(116.397, 39.908)
          that.setData({
            coords: bd,
            baiduMapUrl: that.buildBaiduUrl(bd.lng, bd.lat, 14),
            useBaidu: true,
            showNativeMap: false
          })
        } else {
          that.setData({ showNativeMap: true })
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
    this.setData({ markers: [] })
    wx.showLoading({ title: '刷新中...' })
    this.fetchCrowData()
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
  }
})
