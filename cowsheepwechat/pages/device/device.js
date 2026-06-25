// device.js - 设备管理
const API_DEVICE_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    // 新增设备弹窗
    showAddModal: false,
    addDeviceId: '',

    // 设备列表
    deviceList: [],
    isAdmin: false,
    singleLineRecord: false,
    refresherTriggered: false
  },

  _readSettings() {
    let isAdmin = false
    let singleLineRecord = false
    try {
      const adminVal = wx.getStorageSync('setting_is_admin')
      isAdmin = !!(getApp().globalData.isAdmin || adminVal)
    } catch (e) { /* ignore */ }
    try {
      const raw = wx.getStorageSync('setting_single_line_record')
      singleLineRecord = raw === true || raw === 'true' || raw === 1 || raw === '1'
    } catch (e) { /* ignore */ }
    this.setData({ isAdmin, singleLineRecord })
  },

  onLoad() {
    this._readSettings()
    this.fetchDeviceList()
  },

  onShow() {
    this._readSettings()
  },

  // ========== 获取设备列表 ==========
  fetchDeviceList(forceRefresh, onComplete) {
    let deviceData, livestockData, lotData, batteryData
    let done = 0
    const merge = () => {
      done++
      if (done < 4) return

      const nameMap = {}
      if (livestockData && livestockData.livestockList) {
        livestockData.livestockList.forEach(item => {
          if (item.cowsheepId) nameMap[item.cowsheepId] = item.name
        })
      }

      const lotMap = {}
      if (lotData && lotData.lotList) {
        lotData.lotList.forEach(rec => {
          if (rec.deviceId && rec.deviceId !== '-') {
            if (!lotMap[rec.deviceId]) lotMap[rec.deviceId] = rec
          }
        })
      }

      const batteryMap = (batteryData && batteryData.batteryMap) || {}

      const deviceList = (deviceData.recordList || []).map(item => {
        const lotRec = lotMap[item.deviceId]
        const displayTime = lotRec ? lotRec.rawTime : item.rawTime
        const displayDate = lotRec ? lotRec.date : item.date
        const displayTimePart = lotRec ? lotRec.time_part : item.time_part
        return {
          ...item,
          date: displayDate,
          time_part: displayTimePart,
          rawTime: displayTime,
          bindName: item.link_cowsheep_id ? (nameMap[item.link_cowsheep_id] || item.link_cowsheep_id) : '',
          relativeTime: this._calcRelativeTime(displayTime),
          battery: batteryMap[item.deviceId] || ''
        }
      })

      this.setData({ deviceList })
      if (forceRefresh) {
        wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
      }
      if (onComplete) onComplete()
    }

    dataCache.getDeviceList((data) => { deviceData = data; merge() }, forceRefresh)
    dataCache.getLivestockList((data) => { livestockData = data; merge() }, forceRefresh)
    dataCache.getDeviceLotRefresh((data) => { lotData = data; merge() }, forceRefresh)
    dataCache.getDeviceBatteryAll((data) => { batteryData = data; merge() }, forceRefresh)
  },

  refreshDeviceList() {
    this.fetchDeviceList(true)
  },

  onPullDownRefresh() {
    this.setData({ refresherTriggered: true })
    this.fetchDeviceList(true, () => {
      this.setData({ refresherTriggered: false })
    })
  },

  // 计算相对时间：返回 "1天前" "3小时前" "刚刚" 等
  _calcRelativeTime(rawTime) {
    if (!rawTime || rawTime === '-') return ''
    const t = new Date(rawTime).getTime()
    if (isNaN(t)) return ''
    const now = Date.now()
    const diff = now - t
    const sec = Math.floor(diff / 1000)
    const min = Math.floor(sec / 60)
    const hour = Math.floor(min / 60)
    const day = Math.floor(hour / 24)
    if (sec < 60) return sec + '秒前'
    if (min < 60) return min + '分钟前'
    if (hour < 24) return hour + '小时前'
    if (day < 30) return day + '天前'
    if (day < 365) return Math.floor(day / 30) + '个月前'
    return Math.floor(day / 365) + '年前'
  },

  // ========== 新增设备 ==========
  onAdd() {
    this.setData({
      showAddModal: true,
      addDeviceId: ''
    })
  },

  onAddDeviceIdInput(e) {
    this.setData({ addDeviceId: e.detail.value })
  },

  onAddClose() {
    this.setData({ showAddModal: false })
  },

  onAddConfirm() {
    const deviceId = this.data.addDeviceId.trim()
    if (!deviceId) {
      wx.showToast({ title: '请输入设备ID', icon: 'none' })
      return
    }

    this.setData({ showAddModal: false })
    wx.showLoading({ title: '提交中...' })

    wx.request({
      url: API_DEVICE_URL,
      method: 'POST',
      data: {
        action: 'addDevice',
        info: { deviceId }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('新增设备返回:', JSON.stringify(res.data))
        wx.showToast({ title: '新增成功', icon: 'success', duration: 1500 })
        this.fetchDeviceList(true)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('新增设备失败:', err)
        wx.showToast({ title: '提交失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // ========== 点击设备进入详情 ==========
  onTapDevice(e) {
    const deviceId = e.currentTarget.dataset.deviceid
    if (!deviceId) return

    wx.navigateTo({
      url: '/pages/device-detail/device-detail?deviceId=' + encodeURIComponent(deviceId),
      fail: (err) => {
        console.error('跳转设备详情失败:', err)
        wx.showToast({ title: '页面跳转失败', icon: 'none' })
      }
    })
  }
})
