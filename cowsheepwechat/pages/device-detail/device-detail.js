// device-detail.js - 设备详情
const API_URL = getApp().globalData.apiUrl
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    deviceId: '',
    deviceInfo: null,
    // 设备记录
    showRecords: false,
    recordLoading: false,
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

  // 查看设备记录
  onViewRecords() {
    const deviceId = this.data.deviceId
    if (!deviceId) return

    // 如果已展开则收起
    if (this.data.showRecords) {
      this.setData({ showRecords: false, recordList: [] })
      return
    }

    this.setData({ showRecords: true, recordLoading: true })
    this._fetchRecords(deviceId)
  },

  _fetchRecords(deviceId) {
    const now = new Date()
    const start = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 0, 0, 0)
    const end = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 23, 59, 59)

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceRecordsByTime',
        info: {
          deviceId: deviceId,
          startTime: this._fmtTime(start),
          endTime: this._fmtTime(end)
        }
      },
      success: (res) => {
        const list = this._parseRecords(res.data)
        this.setData({ recordLoading: false, recordList: list })
      },
      fail: (err) => {
        this.setData({ recordLoading: false })
        console.error('获取设备记录失败:', err)
        wx.showToast({ title: '网络请求失败', icon: 'error' })
      }
    })
  },

  _fmtTime(date) {
    const y = date.getFullYear()
    const m = String(date.getMonth() + 1).padStart(2, '0')
    const d = String(date.getDate()).padStart(2, '0')
    const h = String(date.getHours()).padStart(2, '0')
    const min = String(date.getMinutes()).padStart(2, '0')
    const s = String(date.getSeconds()).padStart(2, '0')
    return `${y}-${m}-${d} ${h}:${min}:${s}`
  },

  _parseRecords(data) {
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
        lorastr: attr.lorastr || record.lorastr || '',
        time: attr.time || record.time || '',
        upDateDevice: attr.upDateDevice || record.upDateDevice || '',
        picurl: attr.picurl || record.picurl || ''
      }
    })
  }
})
