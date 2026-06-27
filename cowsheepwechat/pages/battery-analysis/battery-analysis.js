// battery-analysis/battery-analysis.js - 设备电量分析
const API_DEVICE_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    deviceIdOptions: [],
    selectedDeviceIndex: 0,
    selectedDeviceId: '',
    records: [],
    chartRecords: [],
    loading: false,
    hasSearched: false
  },

  onLoad(options) {
    const targetDeviceId = options.deviceId ? decodeURIComponent(options.deviceId) : ''

    // 从设备列表缓存加载设备ID下拉选项
    dataCache.getDeviceList((deviceData) => {
      const idSet = new Set()
      if (deviceData && deviceData.recordList) {
        deviceData.recordList.forEach(record => {
          if (record.deviceId && record.deviceId !== '-') idSet.add(record.deviceId)
        })
      }
      const options = idSet.size > 0 ? Array.from(idSet).sort() : []

      // 如果传入了设备ID，尝试匹配下拉选项
      let selectedIndex = 0
      let selectedDeviceId = options.length > 0 ? options[0] : ''
      if (targetDeviceId && options.length > 0) {
        const idx = options.indexOf(targetDeviceId)
        if (idx >= 0) {
          selectedIndex = idx
          selectedDeviceId = targetDeviceId
        }
      }

      this.setData({
        deviceIdOptions: options,
        selectedDeviceIndex: selectedIndex,
        selectedDeviceId: selectedDeviceId
      })

      // 如果传入了设备ID且匹配成功，自动查询
      if (targetDeviceId && selectedDeviceId === targetDeviceId) {
        this.onFetchLogs()
      }
    }, true)
  },

  // 设备下拉选择
  onDevicePickerChange(e) {
    const idx = parseInt(e.detail.value)
    const deviceId = this.data.deviceIdOptions[idx]
    this.setData({
      selectedDeviceIndex: idx,
      selectedDeviceId: deviceId
    })
  },

  // 查询该设备最近电量 LOG 记录
  onFetchLogs() {
    const deviceId = this.data.selectedDeviceId
    if (!deviceId) {
      wx.showToast({ title: '请先选择设备', icon: 'none' })
      return
    }
    this.setData({ loading: true })
    const that = this
    wx.request({
      url: API_DEVICE_URL,
      method: 'POST',
      data: {
        action: 'getDeviceBatteryLogbyId',
        info: {
          limit: 50,
          deviceId: deviceId
        }
      },
      success: (res) => {
        console.log('[电量分析] 返回:', JSON.stringify(res.data))
        const records = that._parseBatteryRecords(res.data, deviceId)
        // 过滤2025年及以后的电量记录用于图表
        const chartRecords = records
          .filter(r => {
            const y = new Date(r.rawTime).getFullYear()
            return y >= 2025 && r.msgType === '3' && r.batteryVal !== '-' && !isNaN(Number(r.batteryVal))
          })
          .reverse() // 图表按时间升序
        that.setData({ records, chartRecords, hasSearched: true, loading: false })
        if (chartRecords.length > 0) {
          // 延迟等待 Canvas 渲染完成
          setTimeout(function () {
            that._drawChart()
          }, 300)
        }
      },
      fail: (err) => {
        console.error('[电量分析] 获取失败:', err)
        wx.showToast({ title: '获取失败', icon: 'error' })
        that.setData({ loading: false })
      }
    })
  },

  // 解析记录
  _parseBatteryRecords(data, deviceId) {
    let rawList = []
    if (data && data.data && Array.isArray(data.data)) {
      rawList = data.data
    } else if (Array.isArray(data)) {
      rawList = data
    }
    const records = rawList.map(record => {
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
      const lorastr = attr.lorastr || record.lorastr || '-'
      const rawTime = attr.time || record.time || '-'
      const batteryRaw = attr.battery || record.battery || '-'
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']

      // 解析 lorastr 中的电量信息：格式为 type|deviceId|data
      let batteryVal = batteryRaw
      let msgType = '-'
      if (lorastr && lorastr !== '-') {
        const parts = lorastr.split('|')
        msgType = parts[0] || '-'
        // type=3 为电量消息
        if (parts[0] === '3' && parts.length >= 3 && batteryVal === '-') {
          batteryVal = parts[2] || '-'
        }
      }

      return {
        deviceId,
        lorastr,
        date: date || '-',
        time_part: time_part || '',
        rawTime,
        msgType,
        batteryVal
      }
    })
    // 按时间降序
    records.sort((a, b) => {
      const ta = new Date(a.rawTime).getTime()
      const tb = new Date(b.rawTime).getTime()
      if (isNaN(ta) && isNaN(tb)) return 0
      if (isNaN(ta)) return 1
      if (isNaN(tb)) return -1
      return tb - ta
    })
    return records
  },

  // 绘制电量趋势图
  _drawChart: function() {
    const that = this
    const query = wx.createSelectorQuery()
    query.select('#batteryCanvas')
      .fields({ node: true, size: true })
      .exec((res) => {
        if (!res || !res[0] || !res[0].node) return
        const canvas = res[0].node
        const ctx = canvas.getContext('2d')
        const dpr = wx.getSystemInfoSync().pixelRatio
        const width = res[0].width
        const height = res[0].height
        canvas.width = width * dpr
        canvas.height = height * dpr
        ctx.scale(dpr, dpr)

        const records = that.data.chartRecords
        if (records.length === 0) return

        const PAD_LEFT = 60
        const PAD_RIGHT = 20
        const PAD_TOP = 20
        const PAD_BOTTOM = 36
        const plotW = width - PAD_LEFT - PAD_RIGHT
        const plotH = height - PAD_TOP - PAD_BOTTOM

        const values = records.map(r => Number(r.batteryVal))
        const minVal = 0
        const maxVal = Math.min(1.0, Math.ceil(Math.max(...values) * 10) / 10)
        const valRange = maxVal - minVal || 1

        // 坐标转换
        const xFor = (i) => PAD_LEFT + (i / (records.length - 1 || 1)) * plotW
        const yFor = (v) => PAD_TOP + plotH - ((v - minVal) / valRange) * plotH

        // 背景
        ctx.clearRect(0, 0, width, height)

        // 网格线
        ctx.strokeStyle = '#f0f0f0'
        ctx.lineWidth = 0.5
        const ySteps = 5
        for (let i = 0; i <= ySteps; i++) {
          const y = PAD_TOP + (plotH / ySteps) * i
          ctx.beginPath()
          ctx.moveTo(PAD_LEFT, y)
          ctx.lineTo(width - PAD_RIGHT, y)
          ctx.stroke()
        }

        // 渐变填充
        const gradient = ctx.createLinearGradient(0, PAD_TOP, 0, PAD_TOP + plotH)
        gradient.addColorStop(0, 'rgba(76, 175, 80, 0.30)')
        gradient.addColorStop(1, 'rgba(76, 175, 80, 0.02)')

        // 填充区域
        ctx.beginPath()
        ctx.moveTo(xFor(0), PAD_TOP + plotH)
        for (let i = 0; i < records.length; i++) {
          ctx.lineTo(xFor(i), yFor(values[i]))
        }
        ctx.lineTo(xFor(records.length - 1), PAD_TOP + plotH)
        ctx.closePath()
        ctx.fillStyle = gradient
        ctx.fill()

        // 折线
        ctx.beginPath()
        ctx.strokeStyle = '#4CAF50'
        ctx.lineWidth = 2.5
        ctx.lineJoin = 'round'
        ctx.lineCap = 'round'
        for (let i = 0; i < records.length; i++) {
          if (i === 0) ctx.moveTo(xFor(i), yFor(values[i]))
          else ctx.lineTo(xFor(i), yFor(values[i]))
        }
        ctx.stroke()

        // 数据点
        for (let i = 0; i < records.length; i++) {
          const cx = xFor(i), cy = yFor(values[i])
          ctx.beginPath()
          ctx.arc(cx, cy, 4, 0, Math.PI * 2)
          ctx.fillStyle = '#fff'
          ctx.fill()
          ctx.strokeStyle = '#4CAF50'
          ctx.lineWidth = 2
          ctx.stroke()
        }

        // Y轴标签
        ctx.fillStyle = '#999'
        ctx.font = '10px sans-serif'
        ctx.textAlign = 'right'
        for (let i = 0; i <= ySteps; i++) {
          const val = maxVal - (valRange / ySteps) * i
          const y = PAD_TOP + (plotH / ySteps) * i
          ctx.fillText((val * 100).toFixed(0) + '%', PAD_LEFT - 6, y + 3)
        }

        // X轴标签（MM-DD）
        ctx.textAlign = 'center'
        const maxLabels = Math.min(records.length, 6)
        const step = Math.max(1, Math.floor((records.length - 1) / (maxLabels - 1)))
        for (let i = 0; i < records.length; i += step) {
          const t = records[i].rawTime
          const d = new Date(t)
          let label = ''
          if (!isNaN(d.getTime())) {
            const m = String(d.getMonth() + 1).padStart(2, '0')
            const day = String(d.getDate()).padStart(2, '0')
            label = m + '-' + day
          }
          ctx.fillText(label, xFor(i), PAD_TOP + plotH + 16)
        }
      })
  }
})
