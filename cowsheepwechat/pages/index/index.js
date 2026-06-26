// index.js
const dataCache = require('../../config/data-cache.js')

const POLL_INTERVAL = 30000 // 30 秒轮询一次 LOT 表

Page({
  data: {
    deviceCount: 0,
    deviceUpdateTime: '',
    livestockCount: 0,
    boundCount: 0,
    // IoT 实时通知飘窗
    showIotNotify: false,
    iotNotifyLine1: '',
    iotNotifyLine2: '',
    iotNotifyLine3: '',
    // 统计告警（动态计算）
    alerts: [
      { icon: '📶', text: '暂无设备离线数据', color: '#999999' },
      { icon: '🔋', text: '暂无电量数据', color: '#999999' },
      { icon: '🐮', text: '暂无绑定数据', color: '#999999' }
    ]
  },

  _pollTimer: null,
  _lastLotKey: null,   // 上次 LOT 表首条记录的 rawTime，用于判断是否有新数据
  _notifyTimer: null,

  onLoad() {
    console.log('牛羊GPS小程序首页')
    this._preloadData(true)
  },

  onShow() {
    this._preloadData(false)
    this._startIotPolling()
  },

  onHide() {
    this._stopIotPolling()
  },

  onUnload() {
    this._stopIotPolling()
  },

  // ==================== IoT 轮询监听 LOT 表 ====================

  _startIotPolling() {
    if (this._pollTimer) return
    // 先跑一次拿到初始基准
    this._checkLotUpdate()
    this._pollTimer = setInterval(() => {
      this._checkLotUpdate()
    }, POLL_INTERVAL)
    console.log('[首页] IoT 轮询已启动, 间隔', POLL_INTERVAL / 1000, '秒')
  },

  _stopIotPolling() {
    if (this._pollTimer) {
      clearInterval(this._pollTimer)
      this._pollTimer = null
      console.log('[首页] IoT 轮询已停止')
    }
  },

  _checkLotUpdate() {
    dataCache.getDeviceLotRefresh((data) => {
      const lotList = data.lotList || []
      if (lotList.length === 0) return

      const latest = lotList[0]
      const key = latest.rawTime

      // 更新首页摘要时间（每次都刷新相对时间）
      const absolute = latest.date + ' ' + latest.time_part
      const relative = this._formatRelativeTime(latest.rawTime)
      this.setData({ deviceUpdateTime: absolute + '（' + relative + '）' })

      // 首次记录基准，不弹通知
      if (this._lastLotKey === null) {
        this._lastLotKey = key
        console.log('[首页] IoT 轮询基准时间:', latest.rawTime)
        return
      }

      // 与上次不同 → 有新数据
      if (key !== this._lastLotKey) {
        this._lastLotKey = key
        console.log('[首页] IoT 检测到新数据! rawTime:', latest.rawTime)
        this._updateAlerts()

        const deviceId = latest.deviceId || '未知设备'
        const gps = latest.gps || ''
        const lorastr = latest.lorastr || ''
        const timeStr = latest.time_part || ''
        const parts = lorastr.split('|')
        const isType1 = parts[0] === '1'

        let line1, line2, line3
        if (isType1) {
          // 类型 1：显示设备 + GPS（经纬度在第三段）
          const coord = parts[2] || gps || ''
          line1 = deviceId
          line2 = 'GPS: ' + coord
          line3 = timeStr
        } else {
          // 其他类型：显示全部原始数据
          line1 = deviceId
          line2 = lorastr
          line3 = timeStr
        }
        this.setData({
          showIotNotify: true,
          iotNotifyLine1: line1,
          iotNotifyLine2: line2,
          iotNotifyLine3: line3
        })

        // 5 秒后飘窗消失
        if (this._notifyTimer) clearTimeout(this._notifyTimer)
        this._notifyTimer = setTimeout(() => {
          this.setData({ showIotNotify: false })
        }, 5000)
      }
    }, true)
  },

  // 预加载设备列表和牛羊列表到全局缓存，并更新首页摘要
  _preloadData(force) {
    dataCache.getDeviceList((data) => {
      const deviceCount = data.deviceIdOptions ? data.deviceIdOptions.length - 1 : 0 // 去掉"未连接"
      this.setData({ deviceCount })
      console.log('设备表缓存已就绪:', deviceCount + '个设备')
      // 尝试计算绑定数
      this._calcBoundCount()
    }, force)

    dataCache.getLivestockList((data) => {
      const livestockCount = data.livestockList ? data.livestockList.length : 0
      this.setData({ livestockCount })
      console.log('牛羊表缓存已就绪:', livestockCount + '头牛羊')
      // 尝试计算绑定数
      this._calcBoundCount()
    }, force)

    // 加载设备LOT最新数据表，首页设备"最后更新"时间从这里取
    dataCache.getDeviceLotRefresh((data) => {
      const lotList = data.lotList || []
      let deviceUpdateTime = ''
      if (lotList.length > 0) {
        const latest = lotList[0]
        const absolute = latest.date + ' ' + latest.time_part
        const relative = this._formatRelativeTime(latest.rawTime)
        deviceUpdateTime = absolute + '（' + relative + '）'
      }
      this.setData({ deviceUpdateTime })
      console.log('设备LOT最新数据缓存已就绪:', lotList.length + '条记录')
      this._updateAlerts()
    }, force)

    // 加载设备电量表
    dataCache.getDeviceBatteryAll((data) => {
      const batteryMap = data.batteryMap || {}
      const keys = Object.keys(batteryMap)
      console.log('设备电量表缓存已就绪:', keys.length + '条记录')
      this._updateAlerts()
    }, force)
  },

  // 相对时间：刚刚 / X分钟前 / X小时前 / X天前
  _formatRelativeTime(rawTime) {
    if (!rawTime || rawTime === '-') return ''
    const past = new Date(rawTime).getTime()
    const now = Date.now()
    if (isNaN(past)) return rawTime
    const diff = now - past
    const seconds = Math.floor(diff / 1000)
    if (seconds < 60) return '刚刚'
    const minutes = Math.floor(seconds / 60)
    if (minutes < 60) return minutes + '分钟前'
    const hours = Math.floor(minutes / 60)
    if (hours < 24) return hours + '小时前'
    const days = Math.floor(hours / 24)
    return days + '天前'
  },

  // 计算已绑定设备的牛羊数量，同时刷新告警
  _calcBoundCount() {
    const app = getApp()
    const deviceCache = app.globalData.deviceCache
    const livestockCache = app.globalData.livestockCache
    if (!deviceCache || !livestockCache) return

    const bindMap = deviceCache.deviceBindMap || {}
    const livestockList = livestockCache.livestockList || []
    const livestockIdSet = new Set(livestockList.map(v => v.cowsheepId))
    // 统计有绑定设备的牛羊数
    let boundCount = 0
    const counted = new Set()
    for (const deviceId in bindMap) {
      const csId = bindMap[deviceId]
      if (csId && livestockIdSet.has(csId) && !counted.has(csId)) {
        counted.add(csId)
        boundCount++
      }
    }
    this.setData({ boundCount })
    this._updateAlerts()
  },

  // 动态更新告警：根据 LOT 数据（离线时间）和电量数据计算
  _updateAlerts() {
    const app = getApp()
    const lotCache = app.globalData.deviceLotCache
    const batteryCache = app.globalData.deviceBatteryCache
    const livestockCache = app.globalData.livestockCache

    const alerts = []

    // 1. 设备离线告警：LOT 数据中超过 1 小时未上报的设备
    if (lotCache && lotCache.lotList && lotCache.lotList.length > 0) {
      const now = Date.now()
      const ONE_HOUR = 3600000
      // 取每个设备的最新一条 LOT 记录
      const deviceLatest = {}
      lotCache.lotList.forEach(item => {
        const t = new Date(item.rawTime).getTime()
        if (!isNaN(t) && (!deviceLatest[item.deviceId] || t > deviceLatest[item.deviceId])) {
          deviceLatest[item.deviceId] = t
        }
      })
      const offlineCount = Object.values(deviceLatest).filter(t => now - t > ONE_HOUR).length
      if (offlineCount > 0) {
        alerts.push({ icon: '📶', text: offlineCount + '台设备1小时以上未上报数据', color: '#ff5252' })
      } else {
        alerts.push({ icon: '📶', text: '所有设备在线', color: '#4CAF50' })
      }
    }

    // 2. 设备电量告警：电量低于 20% 的设备
    if (batteryCache && batteryCache.batteryMap) {
      const batteryMap = batteryCache.batteryMap
      const lowBatteryDevices = Object.entries(batteryMap).filter(([, val]) => {
        const num = parseFloat(val)
        return !isNaN(num) && num < 20
      })
      if (lowBatteryDevices.length > 0) {
        alerts.push({ icon: '🔋', text: lowBatteryDevices.length + '台设备电量不足', color: '#ff9500' })
      } else {
        alerts.push({ icon: '🔋', text: '设备电量正常', color: '#4CAF50' })
      }
    }

    // 3. 牛羊绑定状态
    if (livestockCache && livestockCache.livestockList) {
      const total = livestockCache.livestockList.length
      const bound = this.data.boundCount
      const unbound = total - bound
      if (unbound > 0) {
        alerts.push({ icon: '🐮', text: unbound + '头牛羊未绑定设备', color: '#ff9500' })
      } else {
        alerts.push({ icon: '🐮', text: '全部' + total + '头牛羊已绑定设备', color: '#4CAF50' })
      }
    }

    if (alerts.length > 0) {
      this.setData({ alerts })
    }
  },

  // 点击飘窗通知 → 跳设备管理页
  onIotNotifyTap() {
    this.setData({ showIotNotify: false })
    wx.navigateTo({ url: '/pages/device/device' })
  },

  // 跳转到功能页面
  goToFeatures() {
    wx.navigateTo({ url: '/pages/features/features' })
  },

  // 跳转到牛羊管理页面
  goToLivestock() {
    wx.navigateTo({ url: '/pages/livestock/livestock' })
  },

  // 跳转到蓝牙连接
  goToBluetooth() {
    wx.navigateTo({ url: '/pages/bluetooth/bluetooth' })
  },

  // 跳转到设备管理页面
  goToDevice() {
    wx.navigateTo({ url: '/pages/device/device' })
  },

  // 跳转到地图页面
  goToMap() {
    wx.navigateTo({ url: '/pages/map/map' })
  }
})
