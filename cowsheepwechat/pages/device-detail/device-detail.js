// device-detail.js - 设备详情
const API_URL = getApp().globalData.api_device_Url
const API_COWSHEEP_URL = getApp().globalData.api_cowsheep_Url
const dataCache = require('../../config/data-cache.js')
const { compressImage } = require('../../utils/image-compress.js')
const { uploadToOSS } = require('../../utils/oss-upload.js')

// DTU 指令转发云函数地址
const FC_URL = 'https://gpsmoveinfo.cn/fc/sendtodtucmd'

Page({
  data: {
    deviceId: '',
    deviceInfo: null,
    // 查看设备轨迹
    showTrackModal: false,
    trackDate: '',
    // 列出数据（表格展示）
    showRecordTable: false,
    recordList: [],
    // 编辑设备弹窗
    showEditModal: false,
    editOldDeviceKey: '',
    editDeviceCode: '',
    editRename: '',
    editProductKey: '',
    editDeviceName: '',
    editDeviceSecret: '',
    editPicurl: '',
    editPicFilePath: '',
    editVisible: false,
    // 连接牛羊弹窗
    showBindModal: false,
    bindDeviceId: '',
    bindNameIndex: 0,
    livestockNames: [],
    // 管理员模式
    isAdmin: false
  },

  // 按设备名生成稳定的浅色背景色：同一设备始终同色，不同设备不同色
  _devicePastel(deviceName) {
    if (!deviceName || deviceName === '-') return 'hsl(0, 0%, 95%)'
    let h = 0
    for (let i = 0; i < deviceName.length; i++) {
      h = (h * 31 + deviceName.charCodeAt(i)) % 360
    }
    const s = 35 + (h % 15)
    const l = 86 + (h % 10)
    return `hsl(${h}, ${s}%, ${l}%)`
  },

  // 按 upDateDevice 生成稳定文字颜色：同一设备始终同色，不同设备分配鲜艳颜色
  _deviceColor(deviceName) {
    if (!deviceName || deviceName === '-') return '#999'
    // 预定义 12 个鲜艳且对比度高的颜色，确保浅色背景上清晰可见
    const vividColors = [
      '#E53935', // 鲜红
      '#1E88E5', // 亮蓝
      '#43A047', // 鲜绿
      '#FB8C00', // 橙色
      '#8E24AA', // 紫色
      '#00ACC1', // 青色
      '#F4511E', // 深橙
      '#D81B60', // 玫红
      '#5E35B1', // 深紫
      '#039BE5', // 天蓝
      '#2E7D32', // 深绿
      '#C0CA33', // 黄绿
    ]
    let idx = 0
    for (let i = 0; i < deviceName.length; i++) {
      idx = (idx * 31 + deviceName.charCodeAt(i)) % vividColors.length
    }
    return vividColors[idx]
  },

  onLoad(options) {
    const deviceId = options.deviceId || ''
    // 读取管理员设置
    let isAdmin = false
    try {
      const adminVal = wx.getStorageSync('setting_is_admin')
      isAdmin = !!(getApp().globalData.isAdmin || adminVal)
    } catch (e) { /* ignore */ }
    this.setData({ deviceId, trackDate: '', isAdmin })
    if (deviceId) {
      this.loadDeviceInfo(deviceId)
    }
  },

  loadDeviceInfo(deviceId) {
    let deviceItem = null
    let lotDataRes = null
    let done = 0
    const merge = () => {
      done++
      if (done < 2) return

      if (!deviceItem) {
        wx.showToast({ title: '未找到设备', icon: 'none' })
        setTimeout(() => wx.navigateBack(), 1500)
        return
      }

      // 从LOT表取最新记录
      let lotRec = null
      if (lotDataRes && lotDataRes.lotList) {
        lotRec = lotDataRes.lotList.find(v => v.deviceId === deviceId)
      }

      // 从 lotRec 的 lorastr 中提取电量: 格式 "1|v4-22|26.52968,109.39078|1.0|5.1" 第4段(index 3)是电量
      let batInfo = null
      if (lotRec && lotRec.lorastr) {
        const parts = lotRec.lorastr.split('|')
        if (parts.length >= 4) {
          const batVal = parts[3]
          if (batVal) {
            batInfo = { battery: batVal, rawTime: lotRec.rawTime }
          }
        }
      }

      const enriched = { ...deviceItem }

      // 保存设备表原始时间作为"上次充电时间"
      const devDate = deviceItem.date && deviceItem.date !== '-' ? deviceItem.date : ''
      const devTime = deviceItem.time_part && deviceItem.time_part !== '-' ? deviceItem.time_part : ''
      enriched.chargeTime = devDate || devTime ? (devDate + ' ' + devTime).trim() : ''

      // 对比设备表、LOT表（含电量）时间，取最新的
      let bestRawTime = deviceItem.rawTime
      let bestDate = deviceItem.date
      let bestTimePart = deviceItem.time_part
      let bestLorastr = deviceItem.lorastr

      const deviceTime = new Date(deviceItem.rawTime || '').getTime()
      const lotTime = (lotRec && lotRec.rawTime) ? new Date(lotRec.rawTime).getTime() : NaN
      const batTime = (batInfo && batInfo.rawTime) ? new Date(batInfo.rawTime).getTime() : NaN

      let newestTime = isNaN(deviceTime) ? 0 : deviceTime

      if (lotRec) {
        bestLorastr = lotRec.lorastr || deviceItem.lorastr
      }
      if (!isNaN(lotTime) && lotTime > newestTime) {
        newestTime = lotTime
        bestRawTime = lotRec.rawTime
        bestDate = lotRec.date
        bestTimePart = lotRec.time_part
        bestLorastr = lotRec.lorastr || bestLorastr
      }
      if (!isNaN(batTime) && batTime > newestTime) {
        bestRawTime = batInfo.rawTime
        bestDate = batInfo.date
        bestTimePart = batInfo.time_part
      }

      enriched.lorastr = bestLorastr
      enriched.date = bestDate
      enriched.time_part = bestTimePart
      enriched.rawTime = bestRawTime

      // 电量显示
      if (batInfo) {
        enriched.battery = batInfo.battery
      }

      this._loadBindName(enriched)
    }

    // 并行拉取两表数据
    dataCache.getDeviceList((deviceData) => {
      if (deviceData && deviceData.recordList) {
        deviceItem = deviceData.recordList.find(v => v.deviceId === deviceId) || null
      }
      merge()
    })
    dataCache.getDeviceLotRefresh((data) => { lotDataRes = data; merge() })
  },

  _loadBindName(item) {
    if (!item.link_cowsheep_id) {
      this.setData({ deviceInfo: { ...item, bindName: '' } })
      this.loadTodayRecords()
      return
    }
    dataCache.getLivestockList((livestockData) => {
      let bindName = ''
      const list = (livestockData && livestockData.livestockList) ? livestockData.livestockList : []
      const found = list.find(v => v.cowsheepId === item.link_cowsheep_id)
      if (found) bindName = found.name
      this.setData({ deviceInfo: { ...item, bindName } })
      this.loadTodayRecords()
    })
  },

  // 自动加载轨迹记录（首入时不传日期=查全部，选择日期后传 curdate 查对应日期）
  loadTodayRecords(callback) {
    const deviceId = this.data.deviceId
    if (!deviceId) return
    const info = { limit: 10, deviceId: deviceId }
    if (this.data.trackDate) {
      info.curdate = this.data.trackDate
    }
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: info,
        time: getApp().formatTime()
      },
      success: (res) => {
        console.log('设备当天轨迹查询返回:', JSON.stringify(res.data))
        const recordList = this._parseRecords(res.data)
        this.setData({ recordList, showRecordTable: recordList.length > 0 })
        if (callback) callback()
      },
      fail: (err) => {
        console.error('设备轨迹查询失败:', err)
        if (callback) callback()
      }
    })
  },

  getTodayStr() {
    const d = new Date()
    return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0')
  },

  // ========== 编辑设备弹窗 ==========
  onDetailEdit() {
    const info = this.data.deviceInfo
    if (!info) return
    this.setData({
      showEditModal: true,
      editOldDeviceKey: info.deviceId,
      editDeviceCode: info.device_key || '',
      editRename: info.rename || '',
      editProductKey: info.ProductKey || '',
      editDeviceName: info.DeviceName || '',
      editDeviceSecret: info.DeviceSecret || '',
      editPicurl: info.picurl || '',
      editPicFilePath: '',
      editVisible: info.visible === true || info.visible === 'true' || info.visible === 1
    })
  },

  onEditDeviceCodeInput(e) {
    this.setData({ editDeviceCode: e.detail.value })
  },

  onEditRenameInput(e) {
    this.setData({ editRename: e.detail.value })
  },

  onEditProductKeyInput(e) {
    this.setData({ editProductKey: e.detail.value })
  },

  onEditDeviceNameInput(e) {
    this.setData({ editDeviceName: e.detail.value })
  },

  onEditDeviceSecretInput(e) {
    this.setData({ editDeviceSecret: e.detail.value })
  },

  onEditVisibleChange(e) {
    this.setData({ editVisible: e.detail.value })
  },

  onEditPic() {
    wx.chooseMedia({
      count: 1,
      mediaType: ['image'],
      sourceType: ['album', 'camera'],
      sizeType: ['compressed'],
      success: (res) => {
        const file = res.tempFiles[0]
        this.setData({
          editPicFilePath: file.tempFilePath,
          editPicurl: file.tempFilePath
        })
      },
      fail: () => { console.log('取消选择图片') }
    })
  },

  onEditClose() {
    this.setData({ showEditModal: false })
  },

  onEditConfirm() {
    const oldKey = this.data.editOldDeviceKey
    const device_key = this.data.editDeviceCode.trim()
    const rename = this.data.editRename.trim()
    const ProductKey = this.data.editProductKey.trim()
    const DeviceName = this.data.editDeviceName.trim()
    const DeviceSecret = this.data.editDeviceSecret.trim()
    const visible = this.data.editVisible
    const picFilePath = this.data.editPicFilePath

    this.setData({ showEditModal: false })

    if (picFilePath) {
      wx.showLoading({ title: '压缩上传...' })
      const objectKey = 'device/' + (device_key || oldKey) + '_' + Date.now() + '.jpg'
      compressImage(picFilePath).then((compressedPath) => {
        return uploadToOSS(compressedPath, objectKey, 'device/')
      }).then((ossUrl) => {
        this._doEditConfirm(oldKey, device_key, rename, ProductKey, DeviceName, DeviceSecret, visible, ossUrl)
      }).catch((err) => {
        wx.hideLoading()
        console.error('OSS 上传失败:', err)
        wx.showToast({ title: '上传失败', icon: 'error', duration: 2000 })
      })
    } else {
      this._doEditConfirm(oldKey, device_key, rename, ProductKey, DeviceName, DeviceSecret, visible, this.data.editPicurl)
    }
  },

  _doEditConfirm(oldKey, device_key, rename, ProductKey, DeviceName, DeviceSecret, visible, picurl) {
    wx.showLoading({ title: '更新中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'updateDevice',
        info: { deviceId: oldKey, device_key, rename, ProductKey, DeviceName, DeviceSecret, visible, picurl }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('编辑设备返回:', JSON.stringify(res.data))
        let result = res.data
        if (typeof result === 'string') {
          try { result = JSON.parse(result) } catch (e) {}
        }
        if (result && result.status === 'success') {
          wx.showToast({ title: result.msg || '更新成功', icon: 'success', duration: 1500 })
          dataCache.clearCache()
          this.loadDeviceInfo(this.data.deviceId)
        } else {
          wx.showToast({ title: (result && result.msg) || '更新失败', icon: 'none', duration: 2500 })
        }
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('编辑设备失败:', err)
        wx.showToast({ title: '网络请求失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // ========== 连接牛羊弹窗 ==========
  onDetailBind() {
    const info = this.data.deviceInfo
    if (!info) return
    let nameIdx = 0
    if (info.link_cowsheep_id && info.bindName) {
      nameIdx = this.data.livestockNames.indexOf(info.bindName)
      if (nameIdx < 0) nameIdx = 0
    }

    // 加载牛名列表
    dataCache.getLivestockList((livestockData) => {
      const names = (livestockData && livestockData.livestockNames) ? livestockData.livestockNames : []
      this.setData({
        showBindModal: true,
        bindDeviceId: info.deviceId,
        bindNameIndex: nameIdx,
        livestockNames: names
      })
    })
  },

  onBindNameChange(e) {
    this.setData({ bindNameIndex: parseInt(e.detail.value) })
  },

  onBindClose() {
    this.setData({ showBindModal: false })
  },

  onBindConfirm() {
    const name = this.data.livestockNames[this.data.bindNameIndex]
    const deviceId = this.data.bindDeviceId
    if (!name || !deviceId) {
      wx.showToast({ title: '请选择牛羊', icon: 'none' })
      return
    }

    dataCache.getLivestockList((livestockData) => {
      const item = (livestockData.livestockList || []).find(v => v.name === name)
      if (!item || !item.cowsheepId) {
        wx.showToast({ title: '未找到对应牛羊', icon: 'none' })
        return
      }

      this.setData({ showBindModal: false })
      wx.showLoading({ title: '绑定中...' })

      wx.request({
        url: API_COWSHEEP_URL,
        method: 'POST',
        data: {
          action: 'bindDeviceCow',
          info: { deviceId, cowsheepId: item.cowsheepId }
        },
        success: (res) => {
          wx.hideLoading()
          console.log('设备绑定返回:', JSON.stringify(res.data))
          let result = res.data
          if (typeof result === 'string') {
            try { result = JSON.parse(result) } catch (e) {}
          }
          if (result && result.status === 'success') {
            wx.showToast({ title: result.msg || '绑定成功', icon: 'success', duration: 1500 })
            this.loadDeviceInfo(this.data.deviceId)
          } else {
            wx.showToast({ title: (result && result.msg) || '绑定失败', icon: 'none', duration: 2500 })
          }
        },
        fail: (err) => {
          wx.hideLoading()
          console.error('设备绑定失败:', err)
          wx.showToast({ title: '网络请求失败', icon: 'error', duration: 2000 })
        }
      })
    })
  },

  // 查看设备轨迹 — 弹日期选择 → 查GPS数据 → 跳转地图页
  // 点击发送指令图标 → 跳转DTU发送指令页
  onSendCmdTap() {
    const deviceId = this.data.deviceId
    if (!deviceId) return
    wx.navigateTo({
      url: '/pages/dtu-cmd/dtu-cmd?deviceId=' + encodeURIComponent(deviceId)
    })
  },

  // 点击电池图标 → 跳转电量分析页
  onBatteryAnalysisTap() {
    const deviceId = this.data.deviceId
    if (!deviceId) return
    wx.navigateTo({
      url: '/pages/battery-analysis/battery-analysis?deviceId=' + encodeURIComponent(deviceId)
    })
  },

  // ========== 获取定位（快捷DTU指令） ==========
  onGetLocationTap() {
    const deviceId = this.data.deviceId
    if (!deviceId) return
    const cmdText = JSON.stringify({ cmd: 'upgps', value: 0 })
    const deviceInfo = this.data.deviceInfo

    // 设备已有密钥，直接发送
    if (deviceInfo && deviceInfo.ProductKey && deviceInfo.DeviceName) {
      this._doSendDTU(deviceInfo, deviceId, cmdText)
    } else {
      // 设备缺少密钥，通过 getDeviceLogbyId 查找上传设备获取密钥
      wx.showLoading({ title: '查询上传设备...' })
      this._queryUploadDevice(deviceId, cmdText)
    }
  },

  // 通过 getDeviceLogbyId 查询目标设备的最新记录，找到信号最佳的上传设备以获取密钥
  _queryUploadDevice(targetDeviceId, cmdText) {
    const that = this
    const todayStr = this._getTodayStr()
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: { limit: 2, deviceId: targetDeviceId },
        time: getApp().formatTime()
      },
      success: (res) => {
        wx.hideLoading()
        console.log('[获取定位] getDeviceLogbyId 返回:', JSON.stringify(res.data))

        let rawList = []
        if (res.data && res.data.data && Array.isArray(res.data.data)) {
          rawList = res.data.data
        } else if (Array.isArray(res.data)) {
          rawList = res.data
        }

        if (rawList.length === 0) {
          wx.showToast({ title: '该设备当天无记录，无法获取上传设备', icon: 'none', duration: 2500 })
          return
        }

        const parsedRecords = rawList.map(record => {
          const attr = {}
          if (record.attributes) {
            record.attributes.forEach(item => { attr[item.columnName] = item.columnValue })
          }
          if (record.primaryKey) {
            record.primaryKey.forEach(item => { attr[item.name] = item.value })
          }
          const upDateDevice = attr.upDateDevice || attr.updatedevice || record.upDateDevice || record.updatedevice || ''
          const rssi = this._parseRssi(attr.rssi || attr.RSSI || record.rssi || record.RSSI)
          return { upDateDevice, rssi }
        }).filter(r => r.upDateDevice && r.upDateDevice !== '-')

        if (parsedRecords.length === 0) {
          wx.showToast({ title: '记录中未找到有效上传设备', icon: 'none', duration: 2500 })
          return
        }

        const deviceBestRssi = {}
        const deviceCount = {}
        parsedRecords.forEach(r => {
          if (!deviceBestRssi[r.upDateDevice] || r.rssi > deviceBestRssi[r.upDateDevice]) {
            deviceBestRssi[r.upDateDevice] = r.rssi
          }
          deviceCount[r.upDateDevice] = (deviceCount[r.upDateDevice] || 0) + 1
        })

        let bestDevice = null
        let bestRssi = 999
        let bestCount = 0
        Object.keys(deviceBestRssi).forEach(devId => {
          const r = deviceBestRssi[devId]
          const c = deviceCount[devId]
          const hasRssi = r < 999
          if (hasRssi) {
            if (r < bestRssi || (r === bestRssi && c > bestCount)) {
              bestRssi = r; bestCount = c; bestDevice = devId
            }
          } else if (bestRssi >= 999) {
            if (c > bestCount) { bestCount = c; bestDevice = devId }
          }
        })

        console.log('[获取定位] 候选上传设备:', JSON.stringify(deviceBestRssi),
          '出现次数:', JSON.stringify(deviceCount),
          '最佳设备:', bestDevice, 'RSSI:', bestRssi)

        // 从设备缓存中查找上传设备
        dataCache.getDeviceList((deviceData) => {
          const allDevices = (deviceData && deviceData.recordList) ? deviceData.recordList : []
          const uploadDevice = allDevices.find(d => d.deviceId === bestDevice)
          if (!uploadDevice) {
            wx.showToast({ title: '上传设备 ' + bestDevice + ' 不在设备列表中', icon: 'none', duration: 2500 })
            return
          }
          if (!uploadDevice.ProductKey || !uploadDevice.DeviceName) {
            wx.showToast({ title: '上传设备 ' + bestDevice + ' 也缺少密钥', icon: 'none', duration: 2500 })
            return
          }
          const rssiInfo = bestRssi > -999 ? ' RSSI:' + bestRssi : ''
          that._doSendDTU(uploadDevice, targetDeviceId, cmdText)
        }, false)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('[获取定位] getDeviceLogbyId 失败:', err)
        wx.showToast({ title: '查询上传设备失败', icon: 'error' })
      }
    })
  },

  // 实际执行DTU发送
  _doSendDTU(credDevice, targetDeviceId, cmdText) {
    let msgObj
    try {
      msgObj = JSON.parse(cmdText)
    } catch (e) {
      msgObj = { text: cmdText }
    }
    msgObj.deviceId = targetDeviceId
    const finalMsg = JSON.stringify(msgObj)

    const payload = {
      action: 'com',
      deviceName: credDevice.DeviceName,
      productKey: credDevice.ProductKey,
      msg: finalMsg,
      timestamp: Date.now()
    }

    console.log('[获取定位] 发送DTU:', JSON.stringify(payload))
    wx.showLoading({ title: '发送定位指令...' })

    wx.request({
      url: FC_URL,
      method: 'POST',
      data: payload,
      timeout: 10000,
      success: (res) => {
        wx.hideLoading()
        console.log('[获取定位] DTU返回:', JSON.stringify(res.data))
        wx.showToast({ title: '定位指令已发送 → ' + targetDeviceId, icon: 'success' })
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('[获取定位] DTU发送失败:', err)
        wx.showToast({ title: '发送失败', icon: 'error' })
      }
    })
  },

  // 解析 RSSI
  _parseRssi(val) {
    if (val === undefined || val === null || val === '' || val === '-') return -999
    const n = parseInt(val, 10)
    return isNaN(n) ? -999 : n
  },

  // 获取今天日期字符串 yyyy-MM-dd
  _getTodayStr() {
    const d = new Date()
    return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0')
  },

  // 点击设备图片放大预览
  onPreviewImage() {
    const picurl = this.data.deviceInfo && this.data.deviceInfo.picurl
    if (picurl) {
      wx.previewImage({
        current: picurl,
        urls: [picurl]
      })
    }
  },

  onViewRecords() {
    this.setData({
      showTrackModal: true,
      trackDate: this.getTodayStr()
    })
  },

  onTrackDateChange(e) {
    this.setData({ trackDate: e.detail.value })
  },

  onTrackClose() {
    this.setData({ showTrackModal: false })
  },

  onTrackConfirm() {
    this.setData({ showTrackModal: false })
    wx.showLoading({ title: '查询中...' })
    this.loadTodayRecords(() => wx.hideLoading())
  },

  // 轨迹地图：跳转到地图页展示GPS轨迹
  onTrackShowMap() {
    const deviceId = this.data.deviceId
    this.setData({ showTrackModal: false })
    wx.showLoading({ title: '查询中...' })
    const info = { limit: 30, deviceId: deviceId }
    if (this.data.trackDate) {
      info.curdate = this.data.trackDate
    }
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: info,
        time: getApp().formatTime()
      },
      success: (res) => {
        wx.hideLoading()
        console.log('设备轨迹查询返回:', JSON.stringify(res.data))
        const recordList = this._parseRecordsForMap(res.data)
        if (recordList.length === 0) {
          wx.showToast({ title: '该设备当天无轨迹数据', icon: 'none' })
          return
        }
        getApp().globalData.trackData = recordList
        wx.navigateTo({ url: '/pages/trackmap/trackmap' })
      },
      fail: (err) => {
        wx.hideLoading()
        wx.showToast({ title: '查询失败', icon: 'error' })
      }
    })
  },

  // 列出数据的解析：和features页parseRecordList格式一致
  _parseRecords(data) {
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
      const upDateDevice = attr.upDateDevice || attr.updatedevice || record.upDateDevice || record.updatedevice || '-'
      const lorastr = attr.lorastr || record.lorastr || '-'
      const rawTime = attr.time || record.time || '-'
      const rssi = attr.rssi != null ? attr.rssi : (record.rssi != null ? record.rssi : '')
      const snr = attr.snr != null ? attr.snr : (record.snr != null ? record.snr : '')
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']

      // 如果 rssi/snr 为空，尝试从 lorastr 末尾段提取
      let finalRssi = rssi
      let finalSnr = snr
      if (lorastr && lorastr !== '-') {
        const parts = lorastr.split('|')
        if (parts.length >= 2) {
          const lastPart = parts[parts.length - 1]
          const secLastPart = parts[parts.length - 2]
          if (finalRssi === '' && /^-?\d+$/.test(lastPart)) {
            finalRssi = lastPart
          }
          if (finalSnr === '' && /^-?\d+(\.\d+)?$/.test(secLastPart)) {
            finalSnr = secLastPart
          }
        }
      }

      // 解析 lorastr 类型：格式为 type|deviceId|data
      // 1=定位  2=对时  3=电量
      let msgType = '-'
      if (lorastr && lorastr !== '-') {
        const parts = lorastr.split('|')
        msgType = parts[0] || '-'
      }

      return { deviceId, upDateDevice, lorastr, msgType, rssi: finalRssi, snr: finalSnr, date: date || '-', time_part: time_part || '', rawTime, bgColor: this._devicePastel(upDateDevice), deviceColor: this._deviceColor(upDateDevice) }
    })
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

  // 地图轨迹解析：输出gps/crow_id供trackmap使用
  _parseRecordsForMap(data) {
    let rawList = []
    if (data && data.data && Array.isArray(data.data)) {
      rawList = data.data
    } else if (Array.isArray(data)) {
      rawList = data
    }
    return rawList.map(record => {
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
      return {
        deviceId: attr.deviceId || record.deviceId || '-',
        auto_id: attr.auto_id || record.auto_id || '-',
        gps: attr.gps || record.gps || '',
        lorastr: attr.lorastr || record.lorastr || '',
        crow_id: attr.crow_idx || record.crow_idx || '',
        time: attr.time || record.time || ''
      }
    })
  }
})
