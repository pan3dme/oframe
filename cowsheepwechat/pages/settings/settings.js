// settings.js
const STORAGE_KEY_BLE_SOUND = 'setting_ble_sound'
const STORAGE_KEY_SINGLE_LINE = 'setting_single_line_record'

Page({
  data: {
    bleSound: true,          // 默认开启蓝牙接收声音
    singleLineRecord: false  // 默认不单行显示
  },

  onLoad() {
    // 读取本地存储的设置
    try {
      const bleSound = wx.getStorageSync(STORAGE_KEY_BLE_SOUND)
      if (bleSound !== '' && bleSound !== undefined) {
        this.setData({ bleSound: !!bleSound })
      }
    } catch (e) { /* 首次使用，保持默认值 */ }

    try {
      const singleLine = wx.getStorageSync(STORAGE_KEY_SINGLE_LINE)
      if (singleLine !== '' && singleLine !== undefined) {
        this.setData({ singleLineRecord: !!singleLine })
      }
    } catch (e) { /* 首次使用，保持默认值 */ }
  },

  // 蓝牙接收声音开关
  onBleSoundChange(e) {
    const value = e.detail.value
    this.setData({ bleSound: value })
    wx.setStorageSync(STORAGE_KEY_BLE_SOUND, value)
    wx.showToast({ title: value ? '蓝牙声音已开启' : '蓝牙声音已关闭', icon: 'none', duration: 1000 })
  },

  // 单行显示记录开关
  onSingleLineChange(e) {
    const value = e.detail.value
    this.setData({ singleLineRecord: value })
    wx.setStorageSync(STORAGE_KEY_SINGLE_LINE, value)
    wx.showToast({ title: value ? '已切换单行显示' : '已恢复默认显示', icon: 'none', duration: 1000 })
  }
})
