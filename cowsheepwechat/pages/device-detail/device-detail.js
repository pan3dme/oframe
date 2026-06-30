// device-detail.js - 设备详情
const API_URL = getApp().globalData.api_device_Url
const API_COWSHEEP_URL = getApp().globalData.api_cowsheep_Url
const dataCache = require('../../config/data-cache.js')
const { compressImage } = require('../../utils/image-compress.js')
const { uploadToOSS } = require('../../utils/oss-upload.js')

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
    editPicurl: '',
    editPicFilePath: '',
    // 连接牛羊弹窗
    showBindModal: false,
    bindDeviceId: '',
    bindNameIndex: 0,
    livestockNames: [],
    // 管理员模式
    isAdmin: false
  },

  // 生成随机浅色背景色（HSL 浅色调，饱和度低，亮度高）
  _randomPastel() {
    const h = Math.floor(Math.random() * 360)
    const s = 30 + Math.floor(Math.random() * 20)
    const l = 88 + Math.floor(Math.random() * 8)
    return `hsl(${h}, ${s}%, ${l}%)`
  },

  onLoad(options) {
    const deviceId = options.deviceId || ''
    const today = this.getTodayStr()
    // 读取管理员设置
    let isAdmin = false
    try {
      const adminVal = wx.getStorageSync('setting_is_admin')
      isAdmin = !!(getApp().globalData.isAdmin || adminVal)
    } catch (e) { /* ignore */ }
    this.setData({ deviceId, trackDate: today, isAdmin })
    if (deviceId) {
      this.loadDeviceInfo(deviceId)
    }
  },

  loadDeviceInfo(deviceId) {
    let deviceItem = null
    let lotDataRes = null
    let batteryDataRes = null
    let done = 0
    const merge = () => {
      done++
      if (done < 3) return

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

      // 从电量表取最新记录
      let batInfo = null
      if (batteryDataRes && batteryDataRes.batteryMap) {
        batInfo = batteryDataRes.batteryMap[deviceId]
      }

      const enriched = { ...deviceItem }

      // 保存设备表原始时间作为"上次充电时间"
      const devDate = deviceItem.date && deviceItem.date !== '-' ? deviceItem.date : ''
      const devTime = deviceItem.time_part && deviceItem.time_part !== '-' ? deviceItem.time_part : ''
      enriched.chargeTime = devDate || devTime ? (devDate + ' ' + devTime).trim() : ''

      // 对比设备表、LOT表、电量表三者时间，取最新的
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

    // 并行拉取三表数据
    dataCache.getDeviceList((deviceData) => {
      if (deviceData && deviceData.recordList) {
        deviceItem = deviceData.recordList.find(v => v.deviceId === deviceId) || null
      }
      merge()
    })
    dataCache.getDeviceLotRefresh((data) => { lotDataRes = data; merge() })
    dataCache.getDeviceBatteryAll((data) => { batteryDataRes = data; merge() })
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

  // 自动加载当天轨迹记录
  loadTodayRecords(callback) {
    const deviceId = this.data.deviceId
    if (!deviceId) return
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: {
          limit:30,
          deviceId: deviceId,
          curdate: this.data.trackDate
        },
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
      editPicurl: info.picurl || '',
      editPicFilePath: ''
    })
  },

  onEditDeviceCodeInput(e) {
    this.setData({ editDeviceCode: e.detail.value })
  },

  onEditRenameInput(e) {
    this.setData({ editRename: e.detail.value })
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
    const picFilePath = this.data.editPicFilePath

    this.setData({ showEditModal: false })

    if (picFilePath) {
      wx.showLoading({ title: '压缩上传...' })
      const objectKey = 'device/' + (device_key || oldKey) + '_' + Date.now() + '.jpg'
      compressImage(picFilePath).then((compressedPath) => {
        return uploadToOSS(compressedPath, objectKey, 'device/')
      }).then((ossUrl) => {
        this._doEditConfirm(oldKey, device_key, rename, ossUrl)
      }).catch((err) => {
        wx.hideLoading()
        console.error('OSS 上传失败:', err)
        wx.showToast({ title: '上传失败', icon: 'error', duration: 2000 })
      })
    } else {
      this._doEditConfirm(oldKey, device_key, rename, this.data.editPicurl)
    }
  },

  _doEditConfirm(oldKey, device_key, rename, picurl) {
    wx.showLoading({ title: '更新中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'updateDevice',
        info: { deviceId: oldKey, device_key, rename, picurl }
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
  // 点击电池图标 → 跳转电量分析页
  onBatteryAnalysisTap() {
    const deviceId = this.data.deviceId
    if (!deviceId) return
    wx.navigateTo({
      url: '/pages/battery-analysis/battery-analysis?deviceId=' + encodeURIComponent(deviceId)
    })
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
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: {
          limit:30,
          deviceId: deviceId,
          curdate: this.data.trackDate
        },
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
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']

      // 解析 lorastr 类型：格式为 type|deviceId|data
      // 1=定位  2=对时  3=电量
      let msgType = '-'
      if (lorastr && lorastr !== '-') {
        const parts = lorastr.split('|')
        msgType = parts[0] || '-'
      }

      return { deviceId, upDateDevice, lorastr, msgType, date: date || '-', time_part: time_part || '', rawTime, bgColor: this._randomPastel() }
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
