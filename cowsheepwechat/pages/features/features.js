// features.js
const API_URL = getApp().globalData.api_device_Url
 
Page({
  data: {
    receivedMsg: '',
    recordList: [],
    showRecordTable: false,
    showInsertModal: false,
    insertDeviceId: '',
    insertLorastr: '',
    deviceIdList: [],
    featureBtns: [{
        id: 1,
        label: '最近10条记录'
      },
      {
        id: 2,
        label: '上报设备LORA'
      },
      {
        id: 3,
        label: '管理设备'
      },
      {
        id: 4,
        label: '设置'
      },
      {
        id: 5,
        label: '管理牛羊'
      },
      {
        id: 6,
        label: '连接蓝牙'
      },
    ]
  },

  // 统一解析接口返回数据，提取 deviceId / lorastr / time
  parseRecordList(data) {
    let rawList = []
    if (data && data.data && Array.isArray(data.data)) {
      rawList = data.data
    } else if (Array.isArray(data)) {
      rawList = data
    }
    const records = rawList.map(record => {
      const attr = {}
      if (record.attributes) {
        record.attributes.forEach(item => {
          attr[item.columnName] = item.columnValue
        })
      }
      if (record.primaryKey) {
        record.primaryKey.forEach(item => {
          attr[item.name] = item.value
        })
      }
      const deviceId = attr.deviceId || attr.deviceid || record.deviceId || record.deviceid || '-'
      const lorastr = attr.lorastr || record.lorastr || '-'
      const rawTime = attr.time || record.time || '-'
      // 拆分日期和时间，用于两行显示
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']
      return {
        deviceId,
        lorastr,
        date: date || '-',
        time_part: time_part || '',
        rawTime
      }
    })
    // 按时间降序排列，最新的在最上面
    records.sort((a, b) => {
      const ta = new Date(a.rawTime).getTime()
      const tb = new Date(b.rawTime).getTime()
      if (isNaN(ta) && isNaN(tb)) return 0
      if (isNaN(ta)) return 1
      if (isNaN(tb)) return -1
      return tb - ta
    })
    return records
  },

  onLoad() {
  },

  // 功能按钮事件
  onFeatureTap(e) {
    const id = e.currentTarget.dataset.id
    console.log('功能按钮 ' + id + ' 被点击')

    if (id === 1) {
      // 最近10条记录
      wx.request({
        url: API_URL,
        method: 'POST',
        data: {
          action: 'getlastlog',
          info: {
            limit: 10
          },
          time: getApp().formatTime()
        },
        success: (res) => {
          console.log('最近10条返回:', JSON.stringify(res.data))
          wx.showToast({
            title: '获取成功',
            icon: 'success',
            duration: 1500
          })
          const recordList = this.parseRecordList(res.data)
          // 提取去重设备编号列表
          const idSet = new Set()
          recordList.forEach(r => { if (r.deviceId && r.deviceId !== '-') idSet.add(r.deviceId) })
          const deviceIdList = Array.from(idSet).sort()
          this.setData({
            recordList,
            deviceIdList,
            showRecordTable: recordList.length > 0
          })
        },
        fail: (err) => {
          console.error('获取失败:', err)
          wx.showToast({
            title: '获取失败',
            icon: 'error',
            duration: 2000
          })
        }
      })
    } else if (id === 2) {
      // 插入一条记录 — 弹出输入框
      this.setData({
        showInsertModal: true,
        insertDeviceId: 'v4-1',
        insertLorastr: '1|v3-1|26.530033, 109.390391|wechat'
      })

    } else if (id === 3) {
      // 设备最新数据 - 直接跳转设备列表页
      wx.navigateTo({ url: '/pages/device/device' })
    } else if (id === 4) {
      // 设置 → 跳转设置页面
      wx.navigateTo({ url: '/pages/settings/settings' })
    } else if (id === 5) {
      // 管理牛羊 - 跳转管理页面
      wx.navigateTo({
        url: '/pages/livestock/livestock'
      })
    } else if (id === 6) {
      // 连接蓝牙 - 跳转蓝牙页面
      wx.navigateTo({
        url: '/pages/bluetooth/bluetooth'
      })
    }
  },

  // ========== 插入记录弹窗 ==========
  onInsertDeviceIdInput(e) {
    this.setData({ insertDeviceId: e.detail.value })
  },
  onInsertLorastrInput(e) {
    this.setData({ insertLorastr: e.detail.value })
  },
  onInsertClose() {
    this.setData({ showInsertModal: false })
  },
  onInsertConfirm() {
    const deviceId = this.data.insertDeviceId.trim()
    const lorastr = this.data.insertLorastr.trim()
    if (!deviceId) {
      wx.showToast({ title: '请输入设备ID', icon: 'none' })
      return
    }
    if (!lorastr) {
      wx.showToast({ title: '请输入LoRa数据', icon: 'none' })
      return
    }
    this.setData({ showInsertModal: false })
    wx.showLoading({ title: '插入中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'insertlog',
        info: {
          deviceId: deviceId,
          lorastr: lorastr,
          upDateDevice:"wechat",
          time: getApp().formatTime()
        }

      },
      success: (res) => {
        wx.hideLoading()
        console.log(JSON.stringify(res.data))
        wx.showToast({ title: '插入成功', icon: 'success', duration: 1500 })
        this.setData({
          receivedMsg: JSON.stringify(res.data),
          showRecordTable: false
        })
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('插入失败:', err)
        wx.showToast({ title: '插入失败', icon: 'error', duration: 2000 })
      }
    })
  },

})