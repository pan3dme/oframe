// trackmap.js - 轨迹地图页，百度卫星图 / 原生地图兜底
Page({
  data: {
    scale: 1,
    baiduMapUrl: '',
    coords: { lng: 116.397, lat: 39.908 },
    baiduAK: '',  // ⚠️ 填入百度 AK
    baiduZoom: 16,
    zoomRetries: 0,
    showNativeMap: false,
    nativeLat: 39.908,
    nativeLng: 116.397,
    nativeScale: 15,
    isSatellite: true,
    markers: [],
    polyline: [],
    greenDotIcon: ''  // 绿点图标路径
  },

  onLoad() {
    // 接收并打印轨迹数据
    const app = getApp()
    const trackData = app.globalData.trackData
    if (trackData && trackData.length > 0) {
      console.log('轨迹地图页收到数据:', JSON.stringify(trackData))
      this.trackData = trackData
    } else {
      console.log('轨迹地图页：暂无轨迹数据')
      this.trackData = null
    }
    // 清除，避免下次误用
    app.globalData.trackData = null

    // 生成绿点图标并加载地图
    this.createGreenDotIcon()
  },

  // 用 canvas 画一个绿圆点，导出为图标路径
  createGreenDotIcon() {
    const query = wx.createSelectorQuery()
    query.select('#iconCanvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) {
        // 降级：直接加载地图，用默认标记
        console.warn('canvas 不可用，使用默认标记')
        this.loadMap()
        return
      }
      const canvas = res[0].node
      const ctx = canvas.getContext('2d')
      canvas.width = 12
      canvas.height = 12
      // 绘制绿色实心圆
      ctx.fillStyle = '#07c160'
      ctx.beginPath()
      ctx.arc(6, 6, 5, 0, 2 * Math.PI)
      ctx.fill()
      // 加一点深绿边框
      ctx.strokeStyle = '#059b4a'
      ctx.lineWidth = 1
      ctx.stroke()

      wx.canvasToTempFilePath({
        canvas: canvas,
        success: (result) => {
          console.log('绿点图标生成成功:', result.tempFilePath)
          this.setData({ greenDotIcon: result.tempFilePath })
          this.loadMap()
        },
        fail: (err) => {
          console.warn('canvasToTempFilePath 失败:', err)
          this.loadMap()
        }
      })
    })
  },

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

  gcjToBd(lng, lat) {
    const x = +lng, y = +lat
    const z = Math.sqrt(x * x + y * y) + 0.00002 * Math.sin(y * Math.PI)
    const theta = Math.atan2(y, x) + 0.000003 * Math.cos(x * Math.PI)
    return {
      lng: (z * Math.cos(theta) + 0.0065).toFixed(6),
      lat: (z * Math.sin(theta) + 0.006).toFixed(6)
    }
  },

  // WGS-84 → GCJ-02
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
            zoomRetries: 0
          })
        } else {
          that.setData({
            nativeLat: res.latitude,
            nativeLng: res.longitude,
            nativeScale: 15,
            showNativeMap: true
          }, () => {
            // 原生地图就绪后，绘制绿点标记
            that.renderTrackMarkers()
          })
        }
      },
      fail: () => {
        if (that.data.baiduAK) {
          const bd = that.gcjToBd(116.397, 39.908)
          that.setData({
            coords: bd,
            baiduMapUrl: that.buildBaiduUrl(bd.lng, bd.lat, 14)
          })
        } else {
          that.setData({ showNativeMap: true }, () => {
            that.renderTrackMarkers()
          })
        }
      }
    })
  },

  // 解析轨迹数据，生成绿点标记
  renderTrackMarkers() {
    const trackData = this.trackData
    if (!trackData || trackData.length === 0) {
      console.log('无轨迹数据，不绘制标记')
      return
    }

    const greenDot = this.data.greenDotIcon

    const markers = trackData
      .filter(item => item.gps && item.gps !== '-')
      .map((item, index) => {
        const parts = item.gps.split(/[｜|]/)
        if (parts.length < 2) return null
        const wgsLat = parseFloat(parts[0])
        const wgsLng = parseFloat(parts[1])
        if (isNaN(wgsLat) || isNaN(wgsLng)) return null
        const gcj = this.wgs84ToGcj02(wgsLng, wgsLat)

        const marker = {
          id: index,
          latitude: gcj.lat,
          longitude: gcj.lng,
          width: 12,
          height: 12,
          title: item.crow_id || '-'
        }
        // 有绿点图标就用，否则显示系统默认红点
        if (greenDot) {
          marker.iconPath = greenDot
        }
        return marker
      })
      .filter(item => item !== null)

    console.log('轨迹绿点标记:', JSON.stringify(markers))

    // 按顺序连线（白线）
    const points = markers.map(m => ({
      latitude: m.latitude,
      longitude: m.longitude
    }))
    const polyline = points.length >= 2 ? [{
      points,
      color: '#FFFFFF',
      width: 3,
      arrowLine: true
    }] : []

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
      wx.showToast({ title: '卫星图降级显示 (zoom ' + fallbackZoom + ')', icon: 'none', duration: 1200 })
      return
    }
    wx.showToast({ title: '卫星图加载失败', icon: 'none' })
  },

  // 以下按钮待加功能
  moveToMyLocation() {},
  onToolBtn2() {}
})
