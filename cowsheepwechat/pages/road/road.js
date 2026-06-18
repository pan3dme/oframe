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
    formRoadDesc: '',
    formRoadPoints: '',
    pathPointCount: 0
  },

  onLoad() {
    this._loadRoadList(false)
  },

  onShow() {
    // 从路径录制页返回，填充坐标
    const pathStr = getApp().globalData._roadRecordedPath
    if (pathStr && this.data.showModal) {
      const pts = pathStr.split(';').filter(p => p.includes(','))
      this.setData({
        formRoadPoints: pathStr,
        pathPointCount: pts.length
      })
      getApp().globalData._roadRecordedPath = null
    }

    // 每次显示时，如果列表为空则从缓存取
    if (this.data.roadList.length === 0) {
      this._loadRoadList(false)
    }
  },

  // ========== 获取道路列表（内存缓存优先） ==========
  _loadRoadList(forceRefresh) {
    this.setData({ loading: true })
    dataCache.getRoadListFromCache((cachedData) => {
      this.setData({ loading: false })
      const roadList = (cachedData.roadList || []).map(item => ({
        roadId: item.route_id,
        name: item.roadname,
        points: item.roadinfo,
        desc: ''
      }))
      this.setData({ roadList })
      if (forceRefresh) {
        wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
      }
    }, forceRefresh)
  },

  // ========== 新增 ==========
  onAdd() {
    this.setData({
      showModal: true,
      modalTitle: '新增道路',
      editRoadId: '',
      formRoadName: '',
      formRoadDesc: '',
      formRoadPoints: '',
      pathPointCount: 0
    })
  },

  onModalClose() {
    this.setData({ showModal: false })
  },

  onNameInput(e) {
    this.setData({ formRoadName: e.detail.value })
  },

  onDescInput(e) {
    this.setData({ formRoadDesc: e.detail.value })
  },

  onPointsInput(e) {
    this.setData({ formRoadPoints: e.detail.value })
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

    const editRoadId = this.data.editRoadId
    if (editRoadId) {
      this._doUpdate(editRoadId, name, this.data.formRoadDesc.trim(), this.data.formRoadPoints.trim())
    } else {
      this._doAdd(name, this.data.formRoadDesc.trim(), this.data.formRoadPoints.trim())
    }
  },

  _doAdd(name, desc, points) {
    this.setData({ showModal: false })
    wx.showLoading({ title: '保存中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'addRoad',
        info: {
          name,
          desc,
          points,
          time: getApp().formatTime()
        }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('新增道路返回:', JSON.stringify(res.data))
        wx.showToast({ title: '新增成功', icon: 'success', duration: 1500 })
        this._loadRoadList(true)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('新增道路失败:', err)
        wx.showToast({ title: '提交失败', icon: 'error' })
      }
    })
  },

  // ========== 编辑 ==========
  onItemEdit(e) {
    const roadId = e.currentTarget.dataset.roadid
    const item = this.data.roadList.find(v => v.roadId === roadId)
    if (!item) return

    const pts = (item.points || '').split(';').filter(p => p.includes(','))

    this.setData({
      showModal: true,
      modalTitle: '编辑道路',
      editRoadId: roadId,
      formRoadName: item.name || '',
      formRoadDesc: item.desc || '',
      formRoadPoints: item.points || '',
      pathPointCount: pts.length
    })
  },

  _doUpdate(roadId, name, desc, points) {
    this.setData({ showModal: false })
    wx.showLoading({ title: '更新中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'updateRoad',
        info: {
          roadId,
          name,
          desc,
          points,
          time: getApp().formatTime()
        }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('更新道路返回:', JSON.stringify(res.data))
        wx.showToast({ title: '更新成功', icon: 'success', duration: 1500 })
        this._loadRoadList(true)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('更新道路失败:', err)
        wx.showToast({ title: '更新失败', icon: 'error' })
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
      data: {
        action: 'deleteRoad',
        info: { roadId }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('删除道路返回:', JSON.stringify(res.data))
        wx.showToast({ title: '删除成功', icon: 'success', duration: 1500 })
        this._loadRoadList(true)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('删除道路失败:', err)
        wx.showToast({ title: '删除失败', icon: 'error' })
      }
    })
  },

  // ========== 下拉刷新 ==========
  onPullDownRefresh() {
    this._loadRoadList(true)
    setTimeout(() => wx.stopPullDownRefresh(), 1000)
  }
})
