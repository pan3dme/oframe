// battery-analysis/battery-analysis.js - 设备电量分析
const API_DEVICE_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    deviceIdOptions: [],
    selectedDeviceIndex: 0,
    selectedDeviceId: '',
    records: [],
    loading: false,
    hasSearched: false
  },

  onLoad() {
    // 从设备列表缓存加载设备ID下拉选项
    dataCache.getDeviceList((deviceData) => {
      const idSet = new Set()
      if (deviceData && deviceData.recordList) {
        deviceData.recordList.forEach(record => {
          if (record.deviceId && record.deviceId !== '-') idSet.add(record.deviceId)
        })
      }
      const options = idSet.size > 0 ? Array.from(idSet).sort() : []
      this.setData({
        deviceIdOptions: options,
        selectedDeviceId: options.length > 0 ? options[0] : ''
      })
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
        that.setData({ records, hasSearched: true, loading: false })
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
  }
})
