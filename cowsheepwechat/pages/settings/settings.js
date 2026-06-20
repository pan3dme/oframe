// settings.js
const STORAGE_KEY_BLE_SOUND = 'setting_ble_sound'
const STORAGE_KEY_SINGLE_LINE = 'setting_single_line_record'
const STORAGE_KEY_IS_ADMIN = 'setting_is_admin'
const ADMIN_PASSWORD = '1234'

Page({
  data: {
    bleSound: true,          // 默认开启蓝牙接收声音
    singleLineRecord: false,  // 默认不单行显示
    isAdmin: false            // 默认不是管理员
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
      const singleLine = wx.getStorageSync(STORAGE_KEY_SINGLE_LINE)
      if (singleLine !== '' && singleLine !== undefined && singleLine !== null) {
        this.setData({ singleLineRecord: singleLine === true || singleLine === 'true' })
      }
    } catch (e) { /* 首次使用，保持默认值 */ }

    try {
      const isAdmin = wx.getStorageSync(STORAGE_KEY_IS_ADMIN)
      if (isAdmin !== '' && isAdmin !== undefined && isAdmin !== null) {
        this.setData({ isAdmin: isAdmin === true || isAdmin === 'true' })
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

  // 单行显示记录开关
  onSingleLineChange(e) {
    const value = e.detail.value === true || e.detail.value === 'true'
    this.setData({ singleLineRecord: value })
    wx.setStorageSync(STORAGE_KEY_SINGLE_LINE, value)
    wx.showToast({ title: value ? '已切换单行显示' : '已恢复默认显示', icon: 'none', duration: 1000 })
  },

  // 管理员开关 — 开启需密码，关闭直接关
  onAdminChange(e) {
    const value = e.detail.value
    if (value) {
      // 打开管理员 → 弹出密码输入
      wx.showModal({
        title: '验证密码',
        content: '请输入管理员密码',
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
  }
})
