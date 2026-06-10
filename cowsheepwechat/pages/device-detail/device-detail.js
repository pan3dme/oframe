// device-detail.js - 设备详情
const API_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    deviceId: '',
    deviceInfo: null,
    // 查看设备轨迹
    showTrackModal: false,
    trackDate: '',
    // 列出数据（表格展示）
    showRecordTable: false,
    recordList: []
  },

  onLoad(options) {
    const deviceId = options.deviceId || ''
    this.setData({ deviceId })
    if (deviceId) {
      this.loadDeviceInfo(deviceId)
    }
  },

  loadDeviceInfo(deviceId) {
    dataCache.getDeviceList((deviceData) => {
      if (!deviceData || !deviceData.recordList) {
        wx.showToast({ title: '数据加载失败', icon: 'none' })
        return
      }
      const recordList = deviceData.recordList || []
      const item = recordList.find(v => v.deviceId === deviceId)

      if (item) {
        this._loadBindName(item)
      } else {
        wx.showToast({ title: '未找到设备', icon: 'none' })
        setTimeout(() => wx.navigateBack(), 1500)
      }
    })
  },

  _loadBindName(item) {
    if (!item.link_cowsheep_id) {
      this.setData({ deviceInfo: { ...item, bindName: '' } })
      return
    }
    dataCache.getLivestockList((livestockData) => {
      let bindName = ''
      const list = (livestockData && livestockData.livestockList) ? livestockData.livestockList : []
      const found = list.find(v => v.cowsheepId === item.link_cowsheep_id)
      if (found) bindName = found.name
      this.setData({ deviceInfo: { ...item, bindName } })
    })
  },

  getTodayStr() {
    const d = new Date()
    return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0')
  },

  // 查看设备轨迹 — 弹日期选择 → 查GPS数据 → 跳转地图页
  onViewRecords() {
    this.setData({
      showTrackModal: true,
      trackDate: this.getTodayStr()
    })
  },

  onTrackDateChange(e) {
    this.setData({ trackDate: e.detail.value })
  },

  onTrackClose() {
    this.setData({ showTrackModal: false })
  },

  onTrackConfirm() {
    const deviceId = this.data.deviceId
    this.setData({ showTrackModal: false })
    wx.showLoading({ title: '查询中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: {
          deviceId: deviceId,
          curdate: this.data.trackDate
        },
        time: getApp().formatTime()
      },
      success: (res) => {
        wx.hideLoading()
        console.log('设备轨迹查询返回:', JSON.stringify(res.data))
        const recordList = this._parseRecords(res.data)
        if (recordList.length === 0) {
          wx.showToast({ title: '该设备当天无轨迹数据', icon: 'none' })
          return
        }
        // 列出数据：在详情页内显示表格
        this.setData({ recordList, showRecordTable: true })
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('设备轨迹查询失败:', err)
        wx.showToast({ title: '查询失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // 轨迹地图：跳转到地图页展示GPS轨迹
  onTrackShowMap() {
    const deviceId = this.data.deviceId
    this.setData({ showTrackModal: false })
    wx.showLoading({ title: '查询中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: {
          deviceId: deviceId,
          curdate: this.data.trackDate
        },
        time: getApp().formatTime()
      },
      success: (res) => {
        wx.hideLoading()
        console.log('设备轨迹查询返回:', JSON.stringify(res.data))
        const recordList = this._parseRecordsForMap(res.data)
        if (recordList.length === 0) {
          wx.showToast({ title: '该设备当天无轨迹数据', icon: 'none' })
          return
        }
        getApp().globalData.trackData = recordList
        wx.navigateTo({ url: '/pages/trackmap/trackmap' })
      },
      fail: (err) => {
        wx.hideLoading()
        wx.showToast({ title: '查询失败', icon: 'error' })
      }
    })
  },

  // 列出数据的解析：和features页parseRecordList格式一致
  _parseRecords(data) {
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
      const deviceId = attr.deviceId || attr.deviceid || record.deviceId || record.deviceid || '-'
      const lorastr = attr.lorastr || record.lorastr || '-'
      const rawTime = attr.time || record.time || '-'
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']
      return { deviceId, lorastr, date: date || '-', time_part: time_part || '', rawTime }
    })
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

  // 地图轨迹解析：输出gps/crow_id供trackmap使用
  _parseRecordsForMap(data) {
    let rawList = []
    if (data && data.data && Array.isArray(data.data)) {
      rawList = data.data
    } else if (Array.isArray(data)) {
      rawList = data
    }
    return rawList.map(record => {
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
        deviceId: attr.deviceId || record.deviceId || '-',
        auto_id: attr.auto_id || record.auto_id || '-',
        gps: attr.gps || record.gps || '',
        crow_id: attr.crow_idx || record.crow_idx || '',
        time: attr.time || record.time || ''
      }
    })
  }
})
