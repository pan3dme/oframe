// features.js
const API_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')
 
Page({
  data: {
    receivedMsg: '',
    recordList: [],
    showRecordTable: false,
    showInsertModal: false,
    insertDeviceId: '',
    insertLorastr: '',
    showTrackModal: false,
    trackDeviceId: '',
    trackDeviceIdIndex: 0,
    deviceIdList: [],
    trackDate: '',
    featureBtns: [{
        id: 1,
        label: '最近10条记录'
      },
      {
        id: 2,
        label: '插入一条记录'
      },
      {
        id: 3,
        label: '设备最新数据'
      },
      {
        id: 4,
        label: '查看设备轨迹'
      },
      {
        id: 5,
        label: '管理牛羊'
      },
    ]
  },

  // 统一解析接口返回数据，提取 deviceId / lorastr / time
  parseRecordList(data) {
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
      // 拆分日期和时间，用于两行显示
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']
      return {
        deviceId,
        lorastr,
        date: date || '-',
        time_part: time_part || '',
        rawTime
      }
    })
    // 按时间降序排列，最新的在最上面
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

  onLoad() {
  },

  // 获取当天日期字符串，格式 YYYY-MM-DD
  getTodayStr() {
    const d = new Date()
    const y = d.getFullYear()
    const m = String(d.getMonth() + 1).padStart(2, '0')
    const day = String(d.getDate()).padStart(2, '0')
    return y + '-' + m + '-' + day
  },

  // 功能按钮事件
  onFeatureTap(e) {
    const id = e.currentTarget.dataset.id
    console.log('功能按钮 ' + id + ' 被点击')

    if (id === 1) {
      // 最近10条记录
      wx.request({
        url: API_URL,
        method: 'POST',
        data: {
          action: 'getlastlog',
          info: {
            limit: 10
          },
          time: getApp().formatTime()
        },
        success: (res) => {
          console.log('最近10条返回:', JSON.stringify(res.data))
          wx.showToast({
            title: '获取成功',
            icon: 'success',
            duration: 1500
          })
          const recordList = this.parseRecordList(res.data)
          // 提取去重设备编号列表
          const idSet = new Set()
          recordList.forEach(r => { if (r.deviceId && r.deviceId !== '-') idSet.add(r.deviceId) })
          const deviceIdList = Array.from(idSet).sort()
          this.setData({
            recordList,
            deviceIdList,
            showRecordTable: recordList.length > 0
          })
        },
        fail: (err) => {
          console.error('获取失败:', err)
          wx.showToast({
            title: '获取失败',
            icon: 'error',
            duration: 2000
          })
        }
      })
    } else if (id === 2) {
      // 插入一条记录 — 弹出输入框
      this.setData({
        showInsertModal: true,
        insertDeviceId: 'v4-1',
        insertLorastr: '1|v3-1|26.000000,109.360000|1'
      })
    } else if (id === 3) {
      // 获取设备最新数据（强制刷新）
      dataCache.refreshDeviceList((cachedData) => {
        const { recordList } = cachedData
        const idSet = new Set()
        recordList.forEach(r => { if (r.deviceId && r.deviceId !== '-') idSet.add(r.deviceId) })
        const deviceIdList = Array.from(idSet).sort()
        this.setData({
          recordList,
          deviceIdList,
          showRecordTable: recordList.length > 0
        })
        wx.showToast({ title: '获取成功', icon: 'success', duration: 1500 })
      })
    } else if (id === 4) {
      // 查看设备轨迹 - 弹出下拉选择
      const deviceIdList = this.data.deviceIdList
      if (deviceIdList.length === 0) {
        wx.showToast({ title: '请先获取设备最新数据', icon: 'none' })
        return
      }
      this.setData({
        showTrackModal: true,
        trackDeviceIdIndex: 0,
        trackDeviceId: deviceIdList[0] || '',
        trackDate: this.getTodayStr()
      })
    } else if (id === 5) {
      // 管理牛羊 - 跳转管理页面
      wx.navigateTo({
        url: '/pages/livestock/livestock'
      })
    }
  },

  // ========== 查看设备轨迹弹窗 ==========
  onTrackDeviceIdChange(e) {
    const idx = e.detail.value
    const deviceId = this.data.deviceIdList[idx] || ''
    this.setData({
      trackDeviceIdIndex: idx,
      trackDeviceId: deviceId
    })
  },
  onTrackDateChange(e) {
    this.setData({ trackDate: e.detail.value })
  },
  onTrackClose() {
    this.setData({ showTrackModal: false })
  },
  onTrackConfirm() {
    const deviceId = this.data.trackDeviceId
    if (!deviceId) {
      wx.showToast({ title: '请选择设备ID', icon: 'none' })
      return
    }
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
        wx.showToast({ title: '查询成功', icon: 'success', duration: 1500 })
        const recordList = this.parseRecordList(res.data)
        this.setData({
          recordList,
          showRecordTable: recordList.length > 0
        })
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('设备轨迹查询失败:', err)
        wx.showToast({ title: '查询失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // ========== 插入记录弹窗 ==========
  onInsertDeviceIdInput(e) {
    this.setData({ insertDeviceId: e.detail.value })
  },
  onInsertLorastrInput(e) {
    this.setData({ insertLorastr: e.detail.value })
  },
  onInsertClose() {
    this.setData({ showInsertModal: false })
  },
  onInsertConfirm() {
    const deviceId = this.data.insertDeviceId.trim()
    const lorastr = this.data.insertLorastr.trim()
    if (!deviceId) {
      wx.showToast({ title: '请输入设备ID', icon: 'none' })
      return
    }
    if (!lorastr) {
      wx.showToast({ title: '请输入LoRa数据', icon: 'none' })
      return
    }
    this.setData({ showInsertModal: false })
    wx.showLoading({ title: '插入中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'insertlog',
        info: {
          deviceId: deviceId,
          lorastr: lorastr,
          time: getApp().formatTime()
        }

      },
      success: (res) => {
        wx.hideLoading()
        console.log(JSON.stringify(res.data))
        wx.showToast({ title: '插入成功', icon: 'success', duration: 1500 })
        this.setData({
          receivedMsg: JSON.stringify(res.data),
          showRecordTable: false
        })
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('插入失败:', err)
        wx.showToast({ title: '插入失败', icon: 'error', duration: 2000 })
      }
    })
  },

})