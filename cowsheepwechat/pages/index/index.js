// index.js
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    deviceCount: 0,
    deviceUpdateTime: '',
    livestockCount: 0,
    boundCount: 0,
    // 上报周期≤5分钟的定位设备数量（无ProductKey + visible=true）
    fastReportCount: 0,
    // 对应设备的 id(别名) 列表字符串，方便快速定位
    fastReportDevices: '',
    refresherTriggered: false,
    // 统计告警（动态计算）
    alerts: [
      { icon: '📶', text: '暂无设备离线数据', color: '#999999' },
      { icon: '🐮', text: '暂无绑定数据', color: '#999999' }
    ]
  },

  onLoad() {
    console.log('牛羊GPS小程序首页')
    // 未登录则跳转登录页，不加载数据
    if (!this._checkLogin()) return
    // 默认读缓存（有缓存则不请求网络），下拉刷新时才会强制刷新
    this._preloadData(false)
  },

  onShow() {
    // 从登录页返回时校验登录状态
    if (!this._checkLogin()) return
    this._preloadData(false)
  },

  // 检查登录状态：本次会话未确认登录则跳转登录页
  _checkLogin() {
    const app = getApp()
    // 本次会话已确认登录 → 放行
    if (app.globalData.sessionConfirmed) return true
    // 启用自动登录且有本地记录，且本地有服务器返回的用户数据时才免密放行；
    // 服务器数据缺失则不允许放行，需重新登录获取
    if (app.globalData.autoLogin) {
      try {
        const loginInfo = wx.getStorageSync('login_info')
        const serverData = wx.getStorageSync('login_server_data')
        if (loginInfo && loginInfo.isLoggedIn && serverData && serverData.data && serverData.data.primaryKey) {
          app.globalData.loginInfo = loginInfo
          app.globalData.serverData = serverData
          app.globalData.sessionConfirmed = true
          return true
        }
      } catch (e) { /* ignore */ }
    }
    wx.reLaunch({ url: '/pages/login/login' })
    return false
  },

  // 预加载设备列表和牛羊列表到全局缓存，并更新首页摘要
  // force=true 强制请求网络；onComplete 在所有数据回调完成后触发
  _preloadData(force, onComplete) {
    let done = 0
    const total = 4
    const finish = () => {
      done++
      if (done >= total && onComplete) onComplete()
    }

    dataCache.getDeviceList((data) => {
      const deviceCount = data.deviceIdOptions ? data.deviceIdOptions.length - 1 : 0 // 去掉"未连接"
      this.setData({ deviceCount })
      console.log('设备表缓存已就绪:', deviceCount + '个设备')
      // 尝试计算绑定数
      this._calcBoundCount()
      // 尝试计算快周期定位设备数
      this._calcFastReportCount()
      finish()
    }, force)

    dataCache.getLivestockList((data) => {
      const livestockCount = data.livestockList ? data.livestockList.length : 0
      this.setData({ livestockCount })
      console.log('牛羊表缓存已就绪:', livestockCount + '头牛羊')
      // 尝试计算绑定数
      this._calcBoundCount()
      finish()
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
      finish()
    }, force)

    // 加载设备配置表（上报周期等），用于统计快周期定位设备
    dataCache.getDeviceConfigAll((data) => {
      const configMap = data.configMap || {}
      console.log('设备配置表缓存已就绪:', Object.keys(configMap).length + '条记录')
      this._calcFastReportCount()
      finish()
    }, force)
  },

  // 下拉刷新：先展示缓存数据，再强制请求网络更新
  onRefresh() {
    this.setData({ refresherTriggered: true })
    // 第一步：走缓存立即渲染
    this._preloadData(false)
    // 第二步：强制刷新网络数据，完成后收起刷新动画
    this._preloadData(true, () => {
      this.setData({ refresherTriggered: false })
    })
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

  // 统计无ProductKey（定位设备）+ visible=true + 上报周期≤5的设备数量
  // 同时记录这些设备的 id，方便快速定位
  _calcFastReportCount() {
    const app = getApp()
    const deviceCache = app.globalData.deviceCache
    const configCache = app.globalData.deviceConfigCache
    if (!deviceCache || !configCache) return

    const recordList = deviceCache.recordList || []
    const configMap = configCache.configMap || {}
    const matched = []
    recordList.forEach(r => {
      // 无ProductKey（定位设备）
      if (r.ProductKey && r.ProductKey !== '-') return
      // 仅统计可见设备
      if (r.visible !== true) return
      // 上报周期需能解析且≤5分钟
      const cfg = configMap[r.deviceId]
      if (!cfg || typeof cfg.reportInterval !== 'number') return
      if (cfg.reportInterval <= 5) matched.push(r)
    })
    // 最多显示两台设备 id，超过两台显示前两台加"等"，避免一行过长
    const deviceStr = matched.length > 0
      ? matched.slice(0, 2).map(r => r.deviceId).join('、') + (matched.length > 2 ? ' 等' : '')
      : ''
    this.setData({ fastReportCount: matched.length, fastReportDevices: deviceStr })
    this._updateAlerts()
  },

  // 动态更新告警：根据 LOT 数据（离线时间）计算
  _updateAlerts() {
    const app = getApp()
    const lotCache = app.globalData.deviceLotCache
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

    // 2. 牛羊绑定状态
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
