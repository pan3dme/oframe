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
    let deviceData, livestockData, lotData, syncData
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

      const syncMap = (syncData && syncData.syncMap) || {}

      const deviceList = (deviceData.recordList || []).map(item => {
        const lotRec = lotMap[item.deviceId]
        const syncInfo = syncMap[item.deviceId]

        // 对比设备表、LOT表(最后定位)、同步时间表三者的时间，取最新的显示
        let displayTime = item.rawTime
        let displayDate = item.date
        let displayTimePart = item.time_part
        let lastRecordType = ''  // 'gps' | 'time' | ''

        const deviceTime = new Date(item.rawTime || '').getTime()
        const lotTime = (lotRec && lotRec.rawTime) ? new Date(lotRec.rawTime).getTime() : NaN
        const syncTime = (syncInfo && syncInfo.rawTime) ? new Date(syncInfo.rawTime).getTime() : NaN

        let newestTime = isNaN(deviceTime) ? 0 : deviceTime
        let newestSource = 'device'

        if (!isNaN(lotTime) && lotTime > newestTime) {
          newestTime = lotTime
          displayTime = lotRec.rawTime
          displayDate = lotRec.date
          displayTimePart = lotRec.time_part
          newestSource = 'lot'
        }
        if (!isNaN(syncTime) && syncTime > newestTime) {
          newestTime = syncTime
          displayTime = syncInfo.rawTime
          displayDate = syncInfo.date
          displayTimePart = syncInfo.time_part
          newestSource = 'sync'
        }

        // 根据最新数据来源判断最后记录类型
        if (newestSource === 'lot' && lotRec) {
          // LOT表的 lorastr 首段为类型编号：1=GPS, 2=对时
          const lorastr = lotRec.lorastr || ''
          const typePart = lorastr.split('|')[0]
          if (typePart === '1') lastRecordType = 'gps'
          else if (typePart === '2') lastRecordType = 'time'
        } else if (newestSource === 'sync') {
          // 同步时间表记录 = 对时
          lastRecordType = 'time'
        }

        const timeInfo = this._calcRelativeTime(displayTime)

        return {
          ...item,
          date: displayDate,
          time_part: displayTimePart,
          rawTime: displayTime,
          bindName: item.link_cowsheep_id ? (nameMap[item.link_cowsheep_id] || item.link_cowsheep_id) : '',
          relativeTime: timeInfo.text,
          timeColor: timeInfo.color,
          timeBgColor: timeInfo.bgColor,
          lastRecordType
        }
      })

      // 按设备ID中"-"后面的序号数字排序（忽略V3/V4等类型前缀）
      deviceList.sort((a, b) => {
        const getSeq = (id) => {
          if (!id) return 0
          const match = id.match(/-(\d+)$/)
          return match ? parseInt(match[1], 10) : 0
        }
        return getSeq(a.deviceId) - getSeq(b.deviceId)
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
    dataCache.getDeviceSyncAll((data) => { syncData = data; merge() }, forceRefresh)
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

  // 计算相对时间：返回 { text, color, bgColor }
  // 超过1天→灰色，超过10分钟→红色，其他→默认蓝色
  _calcRelativeTime(rawTime) {
    const empty = { text: '', color: '', bgColor: '' }
    if (!rawTime || rawTime === '-') return empty
    const t = new Date(rawTime).getTime()
    if (isNaN(t)) return empty
    const now = Date.now()
    const diff = now - t
    const sec = Math.floor(diff / 1000)
    const min = Math.floor(sec / 60)
    const hour = Math.floor(min / 60)
    const day = Math.floor(hour / 24)

    let text = ''
    if (sec < 60) text = sec + '秒前'
    else if (min < 60) text = min + '分钟前'
    else if (hour < 24) text = hour + '小时前'
    else if (day < 30) text = day + '天前'
    else if (day < 365) text = Math.floor(day / 30) + '个月前'
    else text = Math.floor(day / 365) + '年前'

    // 颜色规则：超过2小时灰色，30分钟~2小时红色，小于30分钟绿色
    let color, bgColor
    if (hour >= 2) {
      color = '#999'
      bgColor = '#f5f5f5'
    } else if (min >= 30) {
      color = '#f44336'
      bgColor = '#ffebee'
    } else {
      color = '#4caf50'
      bgColor = '#e8f5e9'
    }

    return { text, color, bgColor }
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
