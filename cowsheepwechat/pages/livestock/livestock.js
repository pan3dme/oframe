// livestock.js - 管理牛羊
const API_URL = getApp().globalData.api_cowsheep_Url
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    showAddModal: false,
    addCowsheepId: '',
    addBirthday: '',
    addGenderIndex: 0,
    genderOptions: ['公', '母'],

    livestockNames: [],

    // 牛羊列表
    livestockList: [],
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
    this.fetchLivestockList()
  },

  onShow() {
    this._readSettings()
  },

  // ========== 列表项点击 → 跳转详情页 ==========
  onItemTap(e) {
    const name = e.currentTarget.dataset.name
    if (!name) return
    const item = this.data.livestockList.find(v => v.name === name)
    if (!item) return
    wx.navigateTo({
      url: '/pages/detail/detail?name=' + encodeURIComponent(item.name) +
        '&cowsheepId=' + encodeURIComponent(item.cowsheepId || '') +
        '&birthday=' + encodeURIComponent(item.birthday || '') +
        '&gender=' + encodeURIComponent(item.gender || '') +
        '&avatar=' + encodeURIComponent(item.avatar || '')
    })
  },

  // 从服务器获取牛羊列表（优先使用缓存）
  fetchLivestockList(forceRefresh, onComplete) {
    dataCache.getLivestockList((cachedData) => {
      // 同时获取设备列表，建立 cowsheep_id → deviceId 映射
      dataCache.getDeviceList((deviceData) => {
        const deviceBindMap = {}  // cowsheep_id → deviceId
        if (deviceData && deviceData.recordList) {
          deviceData.recordList.forEach(record => {
            if (record.link_cowsheep_id && record.deviceId && record.deviceId !== '-') {
              // 一个牛羊可能绑多个设备，只显示第一个
              if (!deviceBindMap[record.link_cowsheep_id]) {
                deviceBindMap[record.link_cowsheep_id] = record.deviceId
              }
            }
          })
        }

        // 给每条牛羊附上连接设备信息 + 年龄
        const enrichedList = cachedData.livestockList.map(item => ({
          ...item,
          connectedDevice: deviceBindMap[item.cowsheepId] || '',
          age: this._calcAge(item.birthday)
        }))

        this.setData({
          livestockList: enrichedList,
          livestockNames: cachedData.livestockNames
        })
        if (forceRefresh) {
          wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
        }
        if (onComplete) onComplete()
      }, forceRefresh)
    }, forceRefresh)
  },

  // 强制刷新牛羊列表
  refreshLivestockList() {
    this.fetchLivestockList(true)
  },

  onPullDownRefresh() {
    this.setData({ refresherTriggered: true })
    this.fetchLivestockList(true, () => {
      this.setData({ refresherTriggered: false })
    })
  },

  // 计算年龄：生日到今天，返回 "1年3个月" 格式
  _calcAge(birthdayStr) {
    if (!birthdayStr || birthdayStr === '-') return '-'
    const str = birthdayStr.replace(/\//g, '-')
    const birth = new Date(str)
    if (isNaN(birth.getTime())) return birthdayStr

    const today = new Date()
    let years = today.getFullYear() - birth.getFullYear()
    let months = today.getMonth() - birth.getMonth()
    // 如果还没到生日月，年-1，月+12
    if (months < 0) {
      years -= 1
      months += 12
    }

    if (years > 0) {
      return years + '年' + (months > 0 ? months + '个月' : '')
    } else {
      return months > 0 ? months + '个月' : '不足1个月'
    }
  },

  // 获取当天日期字符串
  getTodayStr() {
    const d = new Date()
    const y = d.getFullYear()
    const m = String(d.getMonth() + 1).padStart(2, '0')
    const day = String(d.getDate()).padStart(2, '0')
    return y + '-' + m + '-' + day
  },

  // ========== 新增牛羊 ==========
  onAdd() {
    this.setData({
      showAddModal: true,
      addCowsheepId: '',
      addBirthday: this.getTodayStr(),
      addGenderIndex: 0
    })
  },

  
  // 表单输入事件
  onAddCowsheepIdInput(e) {
    this.setData({ addCowsheepId: e.detail.value })
  },
  onAddBirthdayChange(e) {
    this.setData({ addBirthday: e.detail.value })
  },
  /**
 * 新增面板 - 性别选择器变更回调，更新选中性别索引
 * @param {Object} e - 选择器变更事件对象
 * @param {Object} e.detail - 事件详情
 * @param {string|number} e.detail.value - 选中项的索引值
 */
onAddGenderChange(e) {
    this.setData({ addGenderIndex: parseInt(e.detail.value) })
  },

  // 关闭弹窗
  onAddClose() {
    this.setData({ showAddModal: false })
  },

  // 确认新增 — 提交到服务器
  onAddConfirm() {
    const cowsheepId = this.data.addCowsheepId.trim()
    const birthday = this.data.addBirthday
    const gender = this.data.addGenderIndex === 0  // 0=公=true, 1=母=false

    if (!cowsheepId) {
      wx.showToast({ title: '请输入唯一编号', icon: 'none' })
      return
    }
    if (!birthday) {
      wx.showToast({ title: '请选择生日', icon: 'none' })
      return
    }

    this.setData({ showAddModal: false })
    wx.showLoading({ title: '提交中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'addLivestock',
        info: {
          cowsheep_id: cowsheepId,
          birthday: birthday,
          gender: gender
        }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('新增牛羊返回:', JSON.stringify(res.data))
        wx.showToast({ title: '新增成功', icon: 'success', duration: 1500 })
        // 强制刷新列表
        this.fetchLivestockList(true)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('新增牛羊失败:', err)
        wx.showToast({ title: '提交失败', icon: 'error', duration: 2000 })
      }
    })
  },
})
