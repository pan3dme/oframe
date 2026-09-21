// settings.js
const STORAGE_KEY_BLE_SOUND = 'setting_ble_sound'
const STORAGE_KEY_IS_ADMIN = 'setting_is_admin'
const STORAGE_KEY_SHOW_ALL_DEVICES = 'setting_show_all_devices'
const STORAGE_KEY_SHOW_CONVERTED = 'setting_show_converted'
const ADMIN_PASSWORD = '1234'
const dataCache = require('../../config/data-cache.js')
const timeWindowCodec = require('../../utils/time-window-codec.js')

// 中继DTU指令转发云函数地址（与 中继DTU指令页/地图中心一致）
const RELAY_FC_URL = 'https://gpsmoveinfo.cn/fc/sendtodtucmd'

Page({
  data: {
    bleSound: true,          // 默认开启蓝牙接收声音
    isAdmin: false,           // 默认不是管理员
    showAllDevices: false,     // 默认不显示所有设备（仅显示visible=true的）
    showConverted: true,       // 默认显示转换（对时/配置记录显示换算后的可读内容）
    mapCenterText: '—'         // 农场中心坐标（登录返回的 mapcenter 属性）
  },

  _readSettings() {
    // 读取本地存储的设置
    try {
      const bleSound = wx.getStorageSync(STORAGE_KEY_BLE_SOUND)
      if (bleSound !== '' && bleSound !== undefined && bleSound !== null) {
        this.setData({ bleSound: bleSound === true || bleSound === 'true' })
      }
    } catch (e) { /* 首次使用，保持默认值 */ }

    try {
      const isAdmin = wx.getStorageSync(STORAGE_KEY_IS_ADMIN)
      if (isAdmin !== '' && isAdmin !== undefined && isAdmin !== null) {
        this.setData({ isAdmin: isAdmin === true || isAdmin === 'true' })
      }
    } catch (e) { /* 首次使用，保持默认值 */ }

    try {
      const showAll = wx.getStorageSync(STORAGE_KEY_SHOW_ALL_DEVICES)
      if (showAll !== '' && showAll !== undefined && showAll !== null) {
        this.setData({ showAllDevices: showAll === true || showAll === 'true' })
      }
    } catch (e) { /* 首次使用，保持默认值 */ }

    try {
      const showConv = wx.getStorageSync(STORAGE_KEY_SHOW_CONVERTED)
      if (showConv !== '' && showConv !== undefined && showConv !== null) {
        this.setData({ showConverted: showConv === true || showConv === 'true' })
      }
    } catch (e) { /* 首次使用，保持默认值 */ }

    this._readMapCenter()
  },

  // 农场中心坐标：登录时服务器返回的 mapcenter 属性
  // serverData 结构 { status, msg, data: { primaryKey, attributes: [{columnName, columnValue}] } }
  _readMapCenter() {
    let serverData = null
    try {
      serverData = getApp().globalData.serverData || wx.getStorageSync('login_server_data')
    } catch (e) { /* ignore */ }
    if (!serverData || !serverData.data) {
      this.setData({ mapCenterText: '—' })
      return
    }
    let center = ''
    const attrs = serverData.data.attributes
    if (Array.isArray(attrs)) {
      const item = attrs.find(a => a && a.columnName === 'mapcenter')
      if (item && item.columnValue) center = String(item.columnValue)
    }
    // 兜底：直接挂在 data 上的 mapcenter 字段
    if (!center && serverData.data.mapcenter) {
      center = String(serverData.data.mapcenter)
    }
    this.setData({ mapCenterText: center || '—' })
  },

  // ==================== 农场坐标 → 下发所有中继 ====================

  // 点击坐标：解析 lat/lng（兼容 "lat,lng" / "lat|lng"），跳转小程序内地图查看该位置
  onMapCenterTap() {
    const center = this.data.mapCenterText
    if (!center || center === '—') {
      wx.showToast({ title: '未获取到农场坐标', icon: 'none' })
      return
    }
    const parts = String(center).split(/[｜|,，]\s*/)
    if (parts.length < 2) {
      wx.showToast({ title: '坐标格式有误', icon: 'none' })
      return
    }
    const lat = parseFloat(parts[0])
    const lng = parseFloat(parts[1])
    if (isNaN(lat) || isNaN(lng) || (lat === 0 && lng === 0)) {
      wx.showToast({ title: '坐标格式有误', icon: 'none' })
      return
    }
    // 跳转小程序内地图页（view=1 只读查看模式，隐藏"确定"按钮），
    // 传入 WGS-84 坐标，页面内自行转换为 GCJ-02 后居中显示
    console.log('[设置] 打开农场坐标查看:', lat, lng)
    wx.navigateTo({
      url: '/pages/places/picker/picker?view=1&title=' + encodeURIComponent('农场坐标') +
        '&lat=' + lat + '&lng=' + lng
    })
  },

  // 小按钮：向所有"工作期间"的中继发送 {"cmd":"mapcenter","value":坐标}
  onSendMapCenterTap() {
    const center = this.data.mapCenterText
    if (!center || center === '—') {
      wx.showToast({ title: '未获取到农场中心坐标', icon: 'none' })
      return
    }
    const cmdText = JSON.stringify({ cmd: 'mapcenter', value: center })
    const that = this
    wx.showLoading({ title: '查询中继...' })
    this._loadRelaysAndWorkPeriod((relays, workRelays) => {
      wx.hideLoading()
      if (relays.length === 0) {
        wx.showToast({ title: '未找到可用中继（无ProductKey/密钥）', icon: 'none' })
        return
      }
      if (workRelays.length === 0) {
        wx.showToast({ title: '当前没有处于工作期间的中继', icon: 'none' })
        return
      }
      wx.showModal({
        title: '下发农场中心坐标',
        content: '将向 ' + workRelays.length + ' 台工作期间的中继发送：\n' + cmdText + '\n（共 ' + relays.length + ' 台中继）',
        confirmText: '发送',
        cancelText: '取消',
        success: (res) => {
          if (res.confirm) that._broadcastCmdToRelays(workRelays, cmdText)
        }
      })
    })
  },

  // 加载设备列表 + 设备配置，筛出全部中继与当前"工作期间"的中继（与地图中心一致）
  _loadRelaysAndWorkPeriod(callback) {
    let devData = null
    let cfgData = null
    let done = 0
    const finish = () => {
      done++
      if (done < 2) return
      const recordList = (devData && devData.recordList) || []
      const configMap = (cfgData && cfgData.configMap) || {}
      const seen = {}
      const relays = []
      recordList.forEach(r => {
        if (!r.deviceId || r.deviceId === '-') return
        if (seen[r.deviceId]) return
        // 中继设备：带 ProductKey + DeviceName（云密钥）
        if (!r.ProductKey || r.ProductKey === '-' || !r.DeviceName) return
        seen[r.deviceId] = true
        relays.push(r)
      })
      const now = new Date()
      const workRelays = relays.filter(r => this._isRelayInWorkPeriod(r.deviceId, configMap, now))
      console.log('[设置] 中继总数:', relays.length, '，工作期间中继:', workRelays.length,
        workRelays.map(r => r.deviceId).join(','))
      callback(relays, workRelays)
    }
    dataCache.getDeviceList((d) => { devData = d; finish() })
    dataCache.getDeviceConfigAll((d) => { cfgData = d; finish() })
  },

  // 判断中继当前是否处于工作期间（开机时间窗口），无配置视为全天工作（与地图中心一致）
  _isRelayInWorkPeriod(deviceId, configMap, now) {
    const cfg = (configMap && configMap[deviceId]) || null
    const lorastr = (cfg && cfg.lorastr) || ''
    if (!lorastr) return true
    const parts = String(lorastr).split('|')
    if (parts.length < 3 || !parts[2]) return true
    const cfgParts = parts[2].split(',')
    if (cfgParts.length < 2 || !cfgParts[1]) return true
    const win = timeWindowCodec.parseTimeWindow(cfgParts[1].trim())
    if (!win) return true
    const d = now || new Date()
    const currentMinutes = d.getHours() * 60 + d.getMinutes()
    const startMinutes = win.start * 60
    // end=23 代表 23:59
    const endMinutes = win.end === 23 ? 23 * 60 + 59 : win.end * 60
    return currentMinutes >= startMinutes && currentMinutes < endMinutes
  },

  // 并发向多台中继下发同一指令（与地图中心一致），统一管理 loading 与成功/失败统计
  _broadcastCmdToRelays(relayList, cmdText) {
    const total = relayList.length
    let done = 0
    let okCount = 0
    const failed = []

    wx.showLoading({ title: '发送中(' + total + '台)...' })

    const finish = () => {
      if (done < total) return
      wx.hideLoading()
      if (failed.length === 0) {
        wx.showToast({ title: '已发送 ' + total + ' 台中继', icon: 'success' })
      } else if (failed.length === total) {
        wx.showToast({ title: '全部发送失败', icon: 'error' })
      } else {
        wx.showToast({ title: okCount + ' 台成功 ' + failed.length + ' 台失败', icon: 'none' })
      }
    }

    relayList.forEach(r => {
      // 解析指令并注入目标设备ID（即该中继自身）
      let msgObj
      try {
        msgObj = JSON.parse(cmdText)
      } catch (e) {
        msgObj = { text: cmdText }
      }
      msgObj.deviceId = r.deviceId
      const payload = {
        action: 'com',
        deviceName: r.DeviceName,
        productKey: r.ProductKey,
        msg: JSON.stringify(msgObj),
        timestamp: Date.now(),
        info: { wechatid: getApp().getWechatId() }
      }
      console.log('[设置] 中继指令 → ' + r.deviceId + ': ' + payload.msg)

      wx.request({
        url: RELAY_FC_URL,
        method: 'POST',
        data: payload,
        timeout: 10000,
        success: (res) => {
          done++
          okCount++
          console.log('[设置] 中继 ' + r.deviceId + ' 返回:', JSON.stringify(res.data))
          finish()
        },
        fail: (err) => {
          done++
          failed.push(r.deviceId)
          console.error('[设置] 中继 ' + r.deviceId + ' 发送失败:', err)
          finish()
        }
      })
    })
  },

  onLoad() {
    this._readSettings()
  },

  onShow() {
    // 从其他页面返回时重新读取设置，确保开关状态同步
    this._readSettings()
  },

  // 蓝牙接收声音开关
  onBleSoundChange(e) {
    const value = e.detail.value === true || e.detail.value === 'true'
    this.setData({ bleSound: value })
    wx.setStorageSync(STORAGE_KEY_BLE_SOUND, value)
    wx.showToast({ title: value ? '蓝牙声音已开启' : '蓝牙声音已关闭', icon: 'none', duration: 1000 })
  },

  // 显示所有设备开关
  onShowAllDevicesChange(e) {
    const value = e.detail.value === true || e.detail.value === 'true'
    this.setData({ showAllDevices: value })
    wx.setStorageSync(STORAGE_KEY_SHOW_ALL_DEVICES, value)
    wx.showToast({ title: value ? '显示所有设备' : '仅显示可见设备', icon: 'none', duration: 1000 })
  },

  // 显示转换开关：开启后对时记录(TYPE=2)显示换算的日期时间，关闭显示原始LORA数据
  onShowConvertedChange(e) {
    const value = e.detail.value === true || e.detail.value === 'true'
    this.setData({ showConverted: value })
    wx.setStorageSync(STORAGE_KEY_SHOW_CONVERTED, value)
    wx.showToast({ title: value ? '已显示转换时间' : '已恢复原始数据', icon: 'none', duration: 1000 })
  },

  // 管理员开关 — 开启需密码，关闭直接关
  onAdminChange(e) {
    const value = e.detail.value
    if (value) {
      // 打开管理员 → 弹出密码输入
      wx.showModal({
        title: '验证密码',
        content: '1234',
        editable: true,
        placeholderText: '请输入密码',
        success: (res) => {
          if (res.confirm && res.content === ADMIN_PASSWORD) {
            this._setAdmin(true)
          } else if (res.confirm) {
            wx.showToast({ title: '密码错误', icon: 'none' })
          }
        }
      })
    } else {
      // 直接关闭管理员
      this._setAdmin(false)
    }
  },

  _setAdmin(value) {
    this.setData({ isAdmin: value })
    wx.setStorageSync(STORAGE_KEY_IS_ADMIN, value)
    getApp().globalData.isAdmin = value
    wx.showToast({ title: value ? '已设为管理员' : '已取消管理员', icon: 'none', duration: 1000 })
  },

  // 退出登录：清除本地登录记录，回到登录页
  onLogout() {
    wx.showModal({
      title: '退出登录',
      content: '确定退出当前账号吗？将清除本地登录记录并返回登录页。',
      confirmText: '退出',
      confirmColor: '#fa5151',
      success: (res) => {
        if (!res.confirm) return
        try {
          wx.removeStorageSync('login_info')
        } catch (e) { /* ignore */ }
        try {
          wx.removeStorageSync('login_server_data')
        } catch (e) { /* ignore */ }
        const app = getApp()
        app.globalData.loginInfo = null
        app.globalData.serverData = null
        app.globalData.loginCode = null
        app.globalData.isLoggedIn = false
        app.globalData.sessionConfirmed = false
        wx.showToast({ title: '已退出登录', icon: 'none', duration: 1200 })
        setTimeout(() => {
          wx.reLaunch({ url: '/pages/login/login' })
        }, 800)
      }
    })
  },

  // 清理所有数据库缓存
  onClearCache() {
    wx.showModal({
      title: '确认清理',
      content: '将清除所有本地数据库缓存（设备、牛羊、LOT、道路、地名），下次打开页面将重新拉取最新数据。',
      success: (res) => {
        if (res.confirm) {
          dataCache.clearCache()
          wx.showToast({ title: '缓存已清理', icon: 'success', duration: 1500 })
        }
      }
    })
  }
})
