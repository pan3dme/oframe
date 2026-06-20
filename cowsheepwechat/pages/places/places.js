// places.js - 地名管理
const API_URL = getApp().globalData.api_route_place_Url
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    placeList: [],
    loading: false,

    // 新增/编辑弹窗
    showModal: false,
    modalTitle: '新增地名',
    editPlaceId: '',
    formPlaceName: '',
    formPlaceGps: '',
    formPlaceLevel: '1',
    levelOptions: ['1', '2', '3'],
    levelIndex: 0,
    isAdmin: false
  },

  _readSettings() {
    try {
      const adminVal = wx.getStorageSync('setting_is_admin')
      const isAdmin = !!(getApp().globalData.isAdmin || adminVal)
      this.setData({ isAdmin })
    } catch (e) { /* ignore */ }
  },

  onLoad() {
    this._readSettings()
    this._loadPlaceList(false)
  },

  onShow() {
    this._readSettings()
    // 检查是否从坐标选择页返回了 GPS
    const picked = getApp().globalData._placePickedGps
    if (picked) {
      this.setData({ formPlaceGps: picked })
      getApp().globalData._placePickedGps = null
    }
    if (this.data.placeList.length === 0) {
      this._loadPlaceList(false)
    }
  },

  // ========== 获取地名列表（内存缓存优先，一天只请求一次网络） ==========
  _loadPlaceList(forceRefresh) {
    this.setData({ loading: true })
    dataCache.getPlaceListFromCache((cachedData) => {
      this.setData({ loading: false })
      const placeList = (cachedData.placeList || []).map(item => ({
        placeId: item.placeid,
        name: item.name,
        gps: item.gps,
        level: item.level || '1'
      }))
      this.setData({ placeList })
      if (forceRefresh) {
        wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
      }
    }, forceRefresh)
  },

  // ========== 新增 ==========
  onAdd() {
    this.setData({
      showModal: true,
      modalTitle: '新增地名',
      editPlaceId: '',
      formPlaceName: '',
      formPlaceGps: '26.529413,109.390511',
      formPlaceLevel: '1',
      levelIndex: 0
    })
  },

  onModalClose() {
    this.setData({ showModal: false })
  },

  onNameInput(e) {
    this.setData({ formPlaceName: e.detail.value })
  },

  onGpsInput(e) {
    this.setData({ formPlaceGps: e.detail.value })
  },

  onLevelChange(e) {
    const idx = parseInt(e.detail.value, 10)
    this.setData({
      levelIndex: idx,
      formPlaceLevel: this.data.levelOptions[idx]
    })
  },

  onModalConfirm() {
    const name = this.data.formPlaceName.trim()
    const gps = this.data.formPlaceGps.trim()
    const level = this.data.levelOptions[this.data.levelIndex] || '1'
    if (!name) {
      wx.showToast({ title: '请输入地名', icon: 'none' })
      return
    }
    if (!gps) {
      wx.showToast({ title: '请输入GPS坐标', icon: 'none' })
      return
    }

    const editPlaceId = this.data.editPlaceId
    if (editPlaceId) {
      this._doUpdate(editPlaceId, name, gps, level)
    } else {
      this._doAdd(name, gps, level)
    }
  },

  _doAdd(name, gps, level) {
    this.setData({ showModal: false })
    wx.showLoading({ title: '保存中...' })

    const placeid = 'pl_' + Date.now()

    wx.request({
      url: API_URL,
      method: 'POST',
      timeout: 15000,
      data: {
        action: 'addPlace',
        info: { placeid, name, gps, level, time: getApp().formatTime() }
      },
      success: (res) => {
        console.log('新增地名返回:', JSON.stringify(res.data))
        wx.showToast({ title: '新增成功', icon: 'success', duration: 1500 })
        this._loadPlaceList(true)
      },
      fail: (err) => {
        console.error('新增地名失败:', err)
        wx.showToast({ title: '提交失败', icon: 'error' })
      },
      complete: () => {
        wx.hideLoading()
      }
    })
  },

  // ========== 查看地图 ==========
  onItemMap(e) {
    const placeId = e.currentTarget.dataset.placeid
    const item = this.data.placeList.find(v => v.placeId === placeId)
    if (!item) return
    getApp().globalData._placeDetailItem = item
    wx.navigateTo({ url: '/pages/places/detail/detail' })
  },

  // ========== 选坐标 ==========
  onPickGps() {
    wx.navigateTo({ url: '/pages/places/picker/picker' })
  },

  // ========== 编辑 ==========
  onItemEdit(e) {
    const placeId = e.currentTarget.dataset.placeid
    const item = this.data.placeList.find(v => v.placeId === placeId)
    if (!item) return

    const levelStr = (item.level || '1').toString()
    const levelIndex = Math.max(0, this.data.levelOptions.indexOf(levelStr))
    this.setData({
      showModal: true,
      modalTitle: '编辑地名',
      editPlaceId: placeId,
      formPlaceName: item.name || '',
      formPlaceGps: item.gps || '',
      formPlaceLevel: levelStr,
      levelIndex
    })
  },

  _doUpdate(placeId, name, gps, level) {
    this.setData({ showModal: false })
    wx.showLoading({ title: '更新中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      timeout: 15000,
      data: {
        action: 'updatePlace',
        info: { placeId, name, gps, level, time: getApp().formatTime() }
      },
      success: (res) => {
        console.log('更新地名返回:', JSON.stringify(res.data))
        wx.showToast({ title: '更新成功', icon: 'success', duration: 1500 })
        this._loadPlaceList(true)
      },
      fail: (err) => {
        console.error('更新地名失败:', err)
        wx.showToast({ title: '更新失败', icon: 'error' })
      },
      complete: () => {
        wx.hideLoading()
      }
    })
  },

  // ========== 删除 ==========
  onItemDelete(e) {
    const placeId = e.currentTarget.dataset.placeid
    const item = this.data.placeList.find(v => v.placeId === placeId)
    const name = item ? item.name : placeId

    wx.showModal({
      title: '确认删除',
      content: '确定删除地名「' + name + '」吗？',
      success: (res) => {
        if (res.confirm) {
          this._doDelete(placeId)
        }
      }
    })
  },

  _doDelete(placeid) {
    wx.showLoading({ title: '删除中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      timeout: 15000,
      data: {
        action: 'deletePlace',
        info: { placeid }
      },
      success: (res) => {
        console.log('删除地名返回:', JSON.stringify(res.data))
        wx.showToast({ title: '删除成功', icon: 'success', duration: 1500 })
        this._loadPlaceList(true)
      },
      fail: (err) => {
        console.error('删除地名失败:', err)
        wx.showToast({ title: '删除失败', icon: 'error' })
      },
      complete: () => {
        wx.hideLoading()
      }
    })
  },

  // ========== 下拉刷新 ==========
  onPullDownRefresh() {
    this._loadPlaceList(true)
    setTimeout(() => wx.stopPullDownRefresh(), 1000)
  }
})
