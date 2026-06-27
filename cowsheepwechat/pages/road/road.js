// road.js - 道路管理
const API_URL = getApp().globalData.api_route_place_Url
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    roadList: [],
    loading: false,

    // 新增/编辑弹窗
    showModal: false,
    modalTitle: '新增道路',
    editRoadId: '',       // 编辑时有值
    formRoadName: '',
    formRoadPoints: '',
    formRoadPointsDisplay: '',
    pathPointCount: 0,
    formRoadLevel: '1',
    formRoadLevelIndex: 0,
    levelOptions: ['1', '2', '3'],
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
    this._loadRoadList(false)
  },

  onShow() {
    this._readSettings()
    // 从路径录制页返回，填充坐标
    const pathStr = getApp().globalData._roadRecordedPath
    if (pathStr && this.data.showModal) {
      const parts = pathStr.split(',')
      const ptCount = parts.length >= 2 ? parts.length / 2 : 0
      const display = pathStr.length > 20 ? pathStr.substring(0, 20) + '...' : pathStr
      this.setData({
        formRoadPoints: pathStr,
        formRoadPointsDisplay: display,
        pathPointCount: ptCount
      })
      getApp().globalData._roadRecordedPath = null
    }

    // 每次显示时，如果列表为空则从缓存取
    if (this.data.roadList.length === 0) {
      this._loadRoadList(false)
    }
  },

  // ========== 获取道路列表（内存缓存优先） ==========
  _loadRoadList(forceRefresh, onComplete) {
    this.setData({ loading: true })
    dataCache.getRoadListFromCache((cachedData) => {
      this.setData({ loading: false })
      const roadList = (cachedData.roadList || []).map(item => ({
        roadId: item.route_id,
        name: item.roadname,
        points: item.roadinfo,
        level: item.level || '1'
      }))
      this.setData({ roadList })
      if (forceRefresh) {
        wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
      }
      if (onComplete) onComplete()
    }, forceRefresh)
  },

  // ========== 新增 ==========
  onAdd() {
    this.setData({
      showModal: true,
      modalTitle: '新增道路',
      editRoadId: '',
      formRoadName: '',
      formRoadPoints: '',
      formRoadPointsDisplay: '',
      pathPointCount: 0,
      formRoadLevel: '1',
      formRoadLevelIndex: 0
    })
  },

  onModalClose() {
    this.setData({ showModal: false })
  },

  onNameInput(e) {
    this.setData({ formRoadName: e.detail.value })
  },

  onPointsInput(e) {
    this.setData({ formRoadPoints: e.detail.value })
  },

  onLevelChange(e) {
    const idx = parseInt(e.detail.value)
    this.setData({
      formRoadLevelIndex: idx,
      formRoadLevel: this.data.levelOptions[idx]
    })
  },

  // ========== 打开路径录制 ==========
  onGetPath() {
    wx.navigateTo({ url: '/pages/road/record/record' })
  },

  onModalConfirm() {
    const name = this.data.formRoadName.trim()
    if (!name) {
      wx.showToast({ title: '请输入道路名称', icon: 'none' })
      return
    }

    if (this.data.pathPointCount < 2) {
      wx.showToast({ title: '请录制至少2个坐标点', icon: 'none' })
      return
    }

    const level = this.data.formRoadLevel
    const editRoadId = this.data.editRoadId
    if (editRoadId) {
      this._doUpdate(editRoadId, name, this.data.formRoadPoints.trim(), level)
    } else {
      this._doAdd(name, this.data.formRoadPoints.trim(), level)
    }
  },

  _doAdd(name, points, level) {
    this.setData({ showModal: false })
    wx.showLoading({ title: '保存中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      timeout: 15000,
      data: {
        action: 'addRoad',
        info: {
          route_id: 'rd_' + Date.now(),
          roadname: name,
          roadinfo: points,
          level: level,
          time: getApp().formatTime()
        }
      },
      success: (res) => {
        console.log('新增道路返回:', JSON.stringify(res.data))
        wx.showToast({ title: '新增成功', icon: 'success', duration: 1500 })
        this._loadRoadList(true)
      },
      fail: (err) => {
        console.error('新增道路失败:', err)
        wx.showToast({ title: '提交失败', icon: 'error' })
      },
      complete: () => {
        wx.hideLoading()
      }
    })
  },

  // ========== 编辑 ==========
  onItemEdit(e) {
    const roadId = e.currentTarget.dataset.roadid
    const item = this.data.roadList.find(v => v.roadId === roadId)
    if (!item) return

    const rawPoints = item.points || ''
    const parts = rawPoints.split(',')
    const pts = parts.length >= 2 ? parts.length / 2 : 0
    const display = rawPoints.length > 20 ? rawPoints.substring(0, 20) + '...' : rawPoints
    const levelStr = String(item.level || '1')
    const levelIdx = ['1', '2', '3'].indexOf(levelStr)
    const formRoadLevelIndex = levelIdx >= 0 ? levelIdx : 0

    this.setData({
      showModal: true,
      modalTitle: '编辑道路',
      editRoadId: roadId,
      formRoadName: item.name || '',
      formRoadPoints: rawPoints,
      formRoadPointsDisplay: display,
      pathPointCount: pts,
      formRoadLevel: levelStr,
      formRoadLevelIndex
    })
  },

  _doUpdate(roadId, name, points, level) {
    this.setData({ showModal: false })
    wx.showLoading({ title: '更新中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      timeout: 15000,
      data: {
        action: 'updateRoad',
        info: {
          route_id: roadId,
          roadname: name,
          roadinfo: points,
          level: level,
          time: getApp().formatTime()
        }
      },
      success: (res) => {
        console.log('更新道路返回:', JSON.stringify(res.data))
        wx.showToast({ title: '更新成功', icon: 'success', duration: 1500 })
        this._loadRoadList(true)
      },
      fail: (err) => {
        console.error('更新道路失败:', err)
        wx.showToast({ title: '更新失败', icon: 'error' })
      },
      complete: () => {
        wx.hideLoading()
      }
    })
  },

  // ========== 查看地图 ==========
  onItemMap(e) {
    const roadId = e.currentTarget.dataset.roadid
    const item = this.data.roadList.find(v => v.roadId === roadId)
    if (!item) return
    getApp().globalData._roadDetailItem = item
    wx.navigateTo({ url: '/pages/road/detail/detail' })
  },

  // ========== 删除 ==========
  onItemDelete(e) {
    const roadId = e.currentTarget.dataset.roadid
    const item = this.data.roadList.find(v => v.roadId === roadId)
    const name = item ? item.name : roadId

    wx.showModal({
      title: '确认删除',
      content: '确定删除道路「' + name + '」吗？',
      success: (res) => {
        if (res.confirm) {
          this._doDelete(roadId)
        }
      }
    })
  },

  _doDelete(roadId) {
    wx.showLoading({ title: '删除中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      timeout: 15000,
      data: {
        action: 'deleteRoad',
        info: { route_id: roadId }
      },
      success: (res) => {
        console.log('删除道路返回:', JSON.stringify(res.data))
        wx.showToast({ title: '删除成功', icon: 'success', duration: 1500 })
        this._loadRoadList(true)
      },
      fail: (err) => {
        console.error('删除道路失败:', err)
        wx.showToast({ title: '删除失败', icon: 'error' })
      },
      complete: () => {
        wx.hideLoading()
      }
    })
  },

  // ========== 下拉刷新 ==========
  onPullDownRefresh() {
    this._loadRoadList(true, () => {
      wx.stopPullDownRefresh()
    })
  }
})
