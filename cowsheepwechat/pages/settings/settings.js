// settings.js
const STORAGE_KEY_BLE_SOUND = 'setting_ble_sound'
const STORAGE_KEY_IS_ADMIN = 'setting_is_admin'
const STORAGE_KEY_SHOW_ALL_DEVICES = 'setting_show_all_devices'
const STORAGE_KEY_SHOW_CONVERTED = 'setting_show_converted'
const ADMIN_PASSWORD = '1234'
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    bleSound: true,          // 默认开启蓝牙接收声音
    isAdmin: false,           // 默认不是管理员
    showAllDevices: false,     // 默认不显示所有设备（仅显示visible=true的）
    showConverted: false       // 默认不显示转换（显示原始数据）
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
