// features.js
const API_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')
 
Page({
  data: {
    receivedMsg: '',
    showInsertModal: false,
    insertDeviceId: '',
    insertLorastr: '',
    deviceIdOptions: ['v4-1'],
    selectedDeviceIndex: 0,
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
        label: '道路管理'
      },
      {
        id: 7,
        label: '地名管理'
      },
      {
        id: 8,
        label: '连接蓝牙'
      },
      {
        id: 9,
        label: '道路优化'
      },
      {
        id: 10,
        label: 'DTU发送指令'
      },
      {
        id: 11,
        label: '前往首页'
      },
    ]
  },

  onLoad() {
  },

  // 功能按钮事件
  onFeatureTap(e) {
    const id = e.currentTarget.dataset.id
    console.log('功能按钮 ' + id + ' 被点击')

    if (id === 1) {
      // 最近10条记录 — 跳转到云端记录页
      wx.navigateTo({ url: '/pages/cloud-records/cloud-records' })
    } else if (id === 2) {
      // 插入一条记录 — 先从缓存获取设备列表，再弹出选择框
      dataCache.getDeviceList((deviceData) => {
        const idSet = new Set()
        if (deviceData && deviceData.recordList) {
          deviceData.recordList.forEach(record => {
            if (record.deviceId && record.deviceId !== '-') idSet.add(record.deviceId)
          })
        }
        const options = idSet.size > 0 ? Array.from(idSet).sort() : ['v4-1']
        const defaultId = options[0]
        this.setData({
          showInsertModal: true,
          deviceIdOptions: options,
          selectedDeviceIndex: 0,
          insertDeviceId: defaultId,
          insertLorastr: '2|v4-6|2026/07/18 23:02:24.938|1.0|4.4|13'
        })
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
      // 道路管理
      wx.navigateTo({ url: '/pages/road/road' })
    } else if (id === 7) {
      // 地名管理
      wx.navigateTo({ url: '/pages/places/places' })
    } else if (id === 8) {
      // 连接蓝牙 - 跳转蓝牙页面
      wx.navigateTo({
        url: '/pages/bluetooth/bluetooth'
      })
    } else if (id === 9) {
      // 道路优化
      wx.navigateTo({ url: '/pages/road-optimize/road-optimize' })
    } else if (id === 10) {
      // DTU发送指令
      wx.navigateTo({ url: '/pages/dtu-cmd/dtu-cmd' })
    } else if (id === 11) {
      // 前往首页
      wx.reLaunch({ url: '/pages/index/index' })
    }
  },

  // ========== 插入记录弹窗 ==========
  onDevicePickerChange(e) {
    const idx = parseInt(e.detail.value)
    this.setData({
      selectedDeviceIndex: idx,
      insertDeviceId: this.data.deviceIdOptions[idx]
    })
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
          // lorastr: "2|v4-6|2026/07/18 23:02:24.938|1.0|4.4|13",
          upDateDevice:"wechat",
          time: getApp().formatTime(),
          rssi:  "0",
          snr:  "0",
          wechatid: getApp().getWechatId()
        }

      },
      success: (res) => {
        wx.hideLoading()
        console.log(JSON.stringify(res.data))
        wx.showToast({ title: '插入成功', icon: 'success', duration: 1500 })
        this.setData({
          receivedMsg: JSON.stringify(res.data)
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