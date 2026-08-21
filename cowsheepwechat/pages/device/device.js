// device.js - 设备管理
const API_DEVICE_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')
const timeWindowCodec = require('../../utils/time-window-codec.js')

Page({
  data: {
    // 新增设备弹窗
    showAddModal: false,
    addDeviceId: '',

    // 设备列表
    deviceList: [],
    isAdmin: false,
    singleLineRecord: false,
    showAllDevices: false,
    refresherTriggered: false,
    // 设备配置休眠状态映射 deviceId -> { isDormant, powerOnTime }
    deviceConfigMap: {}
  },

  _readSettings() {
    let isAdmin = false
    let singleLineRecord = false
    let showAllDevices = false
    try {
      const adminVal = wx.getStorageSync('setting_is_admin')
      isAdmin = !!(getApp().globalData.isAdmin || adminVal)
    } catch (e) { /* ignore */ }
    try {
      const raw = wx.getStorageSync('setting_single_line_record')
      singleLineRecord = raw === true || raw === 'true' || raw === 1 || raw === '1'
    } catch (e) { /* ignore */ }
    try {
      const raw = wx.getStorageSync('setting_show_all_devices')
      showAllDevices = raw === true || raw === 'true' || raw === 1 || raw === '1'
    } catch (e) { /* ignore */ }
    this.setData({ isAdmin, singleLineRecord, showAllDevices })
  },

  onLoad() {
    this._readSettings()
    this.fetchDeviceList()
  },

  onShow() {
    this._readSettings()
  },

  // ========== 获取设备配置（工作时间判断休眠） ==========
  fetchDeviceConfigAll(forceRefresh, callback) {
    const that = this
    wx.request({
      url: API_DEVICE_URL,
      method: 'POST',
      data: {
        action: 'getDeviceConfigAll',
        info: {}
      },
      success: (res) => {
        console.log('设备配置All查询返回:', JSON.stringify(res.data))
        let rawList = []
        if (res.data && res.data.data && Array.isArray(res.data.data)) {
          rawList = res.data.data
        } else if (Array.isArray(res.data)) {
          rawList = res.data
        }
        // 构建 deviceId → { isDormant, powerOnTime } 映射
        const configMap = {}
        rawList.forEach(record => {
          const attr = {}
          if (record.attributes) {
            record.attributes.forEach(item => { attr[item.columnName] = item.columnValue })
          }
          if (record.primaryKey) {
            record.primaryKey.forEach(item => { attr[item.name] = item.value })
          }
          if (record.lorastr) attr.lorastr = record.lorastr
          const deviceId = attr.deviceId || (record.primaryKey && record.primaryKey.find(p => p.name === 'deviceId') ? record.primaryKey.find(p => p.name === 'deviceId').value : null)
          if (!deviceId) return

          const configLorastr = attr.lorastr || ''
          const result = that._checkWorkingHours(configLorastr)
          configMap[deviceId] = result
        })
        that.setData({ deviceConfigMap: configMap })
        if (callback) callback(configMap)
      },
      fail: (err) => {
        console.error('设备配置All查询失败:', err)
        if (callback) callback({})
      }
    })
  },

  // 根据配置lorastr判断当前是否在工作时间内，同时提取上报周期（分钟）
  // lorastr格式: 6|v4-16|30,0M,38|1.0|4.2|18
  // 第3段(按|分)再按,分: 上报周期,开机时间,GPS工作时间
  // 开机时间/GPS工作时间为两位base62代号（兼容旧格式 "8-6" = 8:00开始持续6小时）
  _checkWorkingHours(configLorastr) {
    const result = { isDormant: false, powerOnTime: '-', reportInterval: 30 }
    if (!configLorastr) return result

    const parts = configLorastr.split('|')
    if (parts.length < 3 || !parts[2]) return result

    const configParts = parts[2].split(',')
    if (configParts.length < 1) return result

    // 上报周期（分钟），第3段第1项
    const intervalNum = parseInt(configParts[0].trim(), 10)
    if (intervalNum > 0) result.reportInterval = intervalNum

    if (configParts.length < 2 || !configParts[1]) return result

    const powerRaw = configParts[1].trim()
    result.powerOnTime = timeWindowCodec.formatTimeRange(powerRaw)

    // 时间窗口（仅当天）：区间内=活跃，区间外=休眠
    const win = timeWindowCodec.parseTimeWindow(powerRaw)
    if (!win) return result

    const now = new Date()
    const currentMinutes = now.getHours() * 60 + now.getMinutes()
    const startMinutes = win.start * 60
    // end=23 代表 23:59
    const endMinutes = win.end === 23 ? 23 * 60 + 59 : win.end * 60
    result.isDormant = currentMinutes < startMinutes || currentMinutes >= endMinutes
    return result
  },

  // ========== 获取设备列表 ==========
  fetchDeviceList(forceRefresh, onComplete) {
    let deviceData, livestockData, lotData, syncData, configMapData
    let done = 0
    const merge = () => {
      done++
      if (done < 5) return

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

        // 电量仅从同步时间表（device_sync）取，统一归一化为 0~100 显示
        let battery = ''
        if (syncInfo && syncInfo.battery) battery = this._formatBatteryPercent(syncInfo.battery)

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

        // 休眠状态完全由设备配置 lorastr 中的工作时间决定：区间内=活跃(绿)，区间外=休眠(灰)
        const cfg = (configMapData && configMapData[item.deviceId]) || { isDormant: false, powerOnTime: '-', reportInterval: 30 }
        const isDormant = cfg.isDormant
        const timeInfo = this._calcRelativeTime(displayTime, cfg.reportInterval, isDormant)

        return {
          ...item,
          date: displayDate,
          time_part: displayTimePart,
          rawTime: displayTime,
          bindName: item.link_cowsheep_id ? (nameMap[item.link_cowsheep_id] || item.link_cowsheep_id) : '',
          relativeTime: timeInfo.text,
          timeColor: timeInfo.color,
          timeBgColor: timeInfo.bgColor,
          dotColor: timeInfo.color,
          lastRecordType,
          battery,
          batteryColor: isDormant ? '#999999' : (battery && parseFloat(battery) < 50) ? '#f44336' : '#333',
          isDormant: isDormant,
          powerOnTime: cfg.powerOnTime
        }
      })

      // 排序：无ProductKey的在前，有ProductKey的排到最后，各自内部按设备ID中"-"后面的序号数字排序
      deviceList.sort((a, b) => {
        const hasPK = (item) => !!(item.ProductKey && item.ProductKey !== '-')
        // 有ProductKey的排后面
        if (hasPK(a) !== hasPK(b)) return hasPK(a) ? 1 : -1
        // 同组内按设备ID序号升序
        const getSeq = (id) => {
          if (!id) return 0
          const match = id.match(/-(\d+)$/)
          return match ? parseInt(match[1], 10) : 0
        }
        return getSeq(a.deviceId) - getSeq(b.deviceId)
      })

      // 根据设置过滤：如果未开启"显示所有设备"，仅显示 visible=true 的设备
      const filteredList = this.data.showAllDevices
        ? deviceList
        : deviceList.filter(item => item.visible === true)

      this.setData({ deviceList: filteredList })
      if (forceRefresh) {
        wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
      }
      if (onComplete) onComplete()
    }

    dataCache.getDeviceList((data) => { deviceData = data; merge() }, forceRefresh)
    dataCache.getLivestockList((data) => { livestockData = data; merge() }, forceRefresh)
    dataCache.getDeviceLotRefresh((data) => { lotData = data; merge() }, forceRefresh)
    dataCache.getDeviceSyncAll((data) => { syncData = data; merge() }, forceRefresh)
    this.fetchDeviceConfigAll(forceRefresh, (configMap) => { configMapData = configMap; merge() })
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

  // 电量归一化为 0~100 显示：兼容 0~1 小数（如 1.0/0.87）与 0~100 整数（如 99）
  _formatBatteryPercent(raw) {
    if (raw === null || raw === undefined || raw === '') return ''
    const n = parseFloat(raw)
    if (isNaN(n)) return raw
    const percent = n > 1 ? Math.round(n) : Math.round(n * 100)
    return String(percent)
  },

  // 计算相对时间：返回 { text, color, bgColor }
  // 颜色按设备配置的上报周期（分钟）判断：
  //   < 1个周期 → 绿色；1~2个周期 → 红色；> 2个周期 → 灰色
  //   若在 1~2 周期区间但不在工作时间内（休眠中），红色降级为灰色
  _calcRelativeTime(rawTime, reportInterval, isDormant) {
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

    // 颜色规则：按配置上报周期判断，默认周期30分钟
    // <1个周期 绿色；1~2个周期 红色；>2个周期 灰色
    // 在1~2周期区间内但不在工作时间（休眠中），红色降级为灰色
    const period = (typeof reportInterval === 'number' && reportInterval > 0) ? reportInterval : 30
    let color, bgColor
    if (min >= period * 2) {
      color = '#999'
      bgColor = '#f5f5f5'
    } else if (min >= period) {
      if (isDormant) {
        color = '#999'
        bgColor = '#f5f5f5'
      } else {
        color = '#f44336'
        bgColor = '#ffebee'
      }
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
