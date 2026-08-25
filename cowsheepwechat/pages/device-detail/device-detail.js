// device-detail.js - 设备详情
const API_URL = getApp().globalData.api_device_Url
const API_COWSHEEP_URL = getApp().globalData.api_cowsheep_Url
const dataCache = require('../../config/data-cache.js')
const batterySwap = require('../../config/battery-swap.js')
const { compressImage } = require('../../utils/image-compress.js')
const { uploadToOSS } = require('../../utils/oss-upload.js')
const timeWindowCodec = require('../../utils/time-window-codec.js')

// DTU 指令转发云函数地址
const FC_URL = 'https://gpsmoveinfo.cn/fc/sendtodtucmd'

Page({
  data: {
    deviceId: '',
    deviceInfo: null,
    // 列出数据（表格展示）
    showRecordTable: false,
    recordList: [],
    recordLimit: 10,
    recordOffset: 0,
    hasMore: true,
    isLoadingMore: false,
    isRefreshing: false,
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
    // 是否展开中继配置输入框（ProductKey/DeviceName/DeviceSecret）：
    // 设备已有 ProductKey 时默认展开；否则隐藏，点击"设为中继"后才展开
    showRelayFields: false,
    // 连接牛羊弹窗
    showBindModal: false,
    bindDeviceId: '',
    bindNameIndex: 0,
    livestockNames: [],
    // 管理员模式
    isAdmin: false,
    // 设备配置（getDeviceConfigAll）
    deviceConfig: null,
    // 是否显示转换（设置页开关控制）：开启后对时记录(TYPE=2)显示换算日期时间，关闭显示原始LORA数据
    showConverted: false
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

  // 读取本地设置（管理员、LORA显示转换）
  _readSettings() {
    let isAdmin = false
    let showConverted = false
    try {
      const adminVal = wx.getStorageSync('setting_is_admin')
      isAdmin = !!(getApp().globalData.isAdmin || adminVal)
    } catch (e) { /* ignore */ }
    try {
      const conv = wx.getStorageSync('setting_show_converted')
      if (conv !== '' && conv !== undefined && conv !== null) {
        showConverted = conv === true || conv === 'true' || conv === 1 || conv === '1'
      }
    } catch (e) { /* ignore */ }
    this.setData({ isAdmin, showConverted })
    // 开关变化后刷新已加载记录的显示文本（如从设置页返回时）
    this._refreshDisplayLorastr()
  },

  // 根据当前"显示转换"开关刷新已加载记录的显示文本
  // 默认：显示原始LORA数据；开启：对时记录(TYPE=2)显示换算时间、配置记录(TYPE=6)显示时间窗，其余类型仍显示原始数据
  _refreshDisplayLorastr() {
    const records = this.data.recordList || []
    if (!records.length) return
    const showConv = this.data.showConverted
    let changed = false
    const updated = records.map(item => {
      const dl = (showConv && (item.msgType === '2' || item.msgType === '6'))
        ? this._buildDisplayLorastr(item.lorastr, item.msgType)
        : item.lorastr
      if (dl !== item.displayLorastr) changed = true
      return Object.assign({}, item, { displayLorastr: dl })
    })
    if (changed) {
      this.setData({ recordList: updated })
    }
  },

  onLoad(options) {
    const deviceId = options.deviceId || ''
    // 读取管理员设置与LORA显示设置
    this._readSettings()
    this.setData({ deviceId })
    this._swapTime = ''
    if (deviceId) {
      this.loadDeviceInfo(deviceId)
      this.loadDeviceConfig(deviceId)
      this.loadSwapTime(deviceId)
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
        // 缓存全部设备 id -> 别名映射，用于记录列表显示上传设备别名
        const renameMap = {}
        deviceData.recordList.forEach(v => {
          if (v.deviceId) renameMap[v.deviceId] = v.rename || ''
        })
        this._deviceRenameMap = renameMap
      }
      merge()
    })
    dataCache.getDeviceLotRefresh((data) => { lotDataRes = data; merge() })
  },

  _loadBindName(item) {
    const swapTime = this._swapTime || ''
    const swapTimeRel = this._swapTimeTs ? batterySwap.formatRelativeTime(this._swapTimeTs) : ''
    if (!item.link_cowsheep_id) {
      this.setData({ deviceInfo: { ...item, bindName: '', swapTime, swapTimeRel } })
      this._updateDormant()
      this.loadTodayRecords(0)
      return
    }
    dataCache.getLivestockList((livestockData) => {
      let bindName = ''
      const list = (livestockData && livestockData.livestockList) ? livestockData.livestockList : []
      const found = list.find(v => v.cowsheepId === item.link_cowsheep_id)
      if (found) bindName = found.name
      this.setData({ deviceInfo: { ...item, bindName, swapTime, swapTimeRel } })
      this._updateDormant()
      this.loadTodayRecords(0)
    })
  },

  // 分析并缓存最近换电时间（对时记录电量跳升检测）
  loadSwapTime(deviceId) {
    const that = this
    const apply = (swap) => {
      const timeStr = swap ? (swap.timeStr || '') : ''
      const ts = swap && swap.time ? swap.time : (timeStr ? new Date(timeStr).getTime() : 0)
      that._swapTime = timeStr
      that._swapTimeTs = ts
      const rel = batterySwap.formatRelativeTime(ts)
      if (that.data.deviceInfo) {
        that.setData({ 'deviceInfo.swapTime': timeStr, 'deviceInfo.swapTimeRel': rel })
      }
    }
    // 缓存中的结果同步返回，可先展示
    const cached = batterySwap.getLastSwap(deviceId, (latest) => {
      apply(latest)
    })
    apply(cached)
  },

  // 页面重新展示时刷新相对时间（如返回前台），并重新读取设置
  onShow() {
    this._readSettings()
    if (!this._swapTimeTs) return
    const rel = batterySwap.formatRelativeTime(this._swapTimeTs)
    if (this.data.deviceInfo) {
      this.setData({ 'deviceInfo.swapTimeRel': rel })
    }
  },

  // 根据已加载的 deviceConfig 和 deviceInfo 重新计算 isDormant
  _updateDormant() {
    const deviceConfig = this.data.deviceConfig
    const deviceInfo = this.data.deviceInfo
    if (deviceConfig && deviceInfo) {
      const configLorastr = deviceConfig.lorastr || ''
      const rawTime = deviceInfo.rawTime || ''
      const isDormant = this._isDormantNow(configLorastr, rawTime)
      this.setData({ 'deviceInfo.isDormant': isDormant })
    }
  },

  // 拉取设备配置（上报周期/开机时间/GPS工作时间等）
  loadDeviceConfig(deviceId) {
    if (!deviceId) return
    const that = this
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceConfigAll',
        info: { deviceId: deviceId, wechatid: getApp().getWechatId() }
      },
      success: (res) => {
        console.log('设备配置查询返回:', JSON.stringify(res.data))
        let rawList = []
        if (res.data && res.data.data && Array.isArray(res.data.data)) {
          rawList = res.data.data
        } else if (Array.isArray(res.data)) {
          rawList = res.data
        }
        if (rawList.length > 0) {
          // 按 deviceId 匹配当前设备
          const record = rawList.find(r => {
            const devId = r.deviceId || (r.primaryKey && r.primaryKey.find(p => p.name === 'deviceId') ? r.primaryKey.find(p => p.name === 'deviceId').value : null)
            return devId === deviceId
          })
          if (record) {
            const attr = {}
            if (record.attributes) {
              record.attributes.forEach(item => { attr[item.columnName] = item.columnValue })
            }
            if (record.primaryKey) {
              record.primaryKey.forEach(item => { attr[item.name] = item.value })
            }
            // 也取直接的属性
            if (record.lorastr) attr.lorastr = record.lorastr
            const configLorastr = attr.lorastr || ''
            // 解析配置：格式 6|v4-16|30,0M,38|1.0|4.2|18
            // 第3段(按|分)再按,分: 上报周期,开机时间,GPS工作时间（两位代号，兼容旧格式 8-6）
            // 第4段可选: 主周期（小时 1-4）
            let reportInterval = '-'
            let mainPeriod = 0
            let powerOnTime = '-'
            let gpsReportTime = '-'
            if (configLorastr) {
              const parts = configLorastr.split('|')
              if (parts.length >= 3 && parts[2]) {
                const configParts = parts[2].split(',')
                if (configParts.length >= 1) reportInterval = configParts[0].trim()
                if (configParts.length >= 2) powerOnTime = timeWindowCodec.formatTimeRange(configParts[1].trim())
                if (configParts.length >= 3) gpsReportTime = timeWindowCodec.formatTimeRange(configParts[2].trim())
                // 主周期（小时 1-4），仅当存在且有效时记录，用于上报周期后显示（n小时）
                if (configParts.length >= 4) {
                  const mp = parseInt(configParts[3].trim(), 10)
                  if (!isNaN(mp) && mp >= 1 && mp <= 4) mainPeriod = mp
                }
              }
            }
            // 更新 deviceInfo 中的配置信息
            if (that.data.deviceInfo) {
              // 判断是否在休眠时间内（含最后上报时间>1小时的判断）
              const rawTime = that.data.deviceInfo.rawTime || ''
              const isDormant = that._isDormantNow(configLorastr, rawTime)
              const updated = { ...that.data.deviceInfo, configLorastr, reportInterval, mainPeriod, powerOnTime, gpsReportTime, isDormant }
              that.setData({ deviceInfo: updated, deviceConfig: attr })
            } else {
              that.setData({ deviceConfig: attr })
            }
          } else {
            // 未匹配到当前设备，清空显示
            if (that.data.deviceInfo) {
              const updated = { ...that.data.deviceInfo, reportInterval: '-', mainPeriod: 0, powerOnTime: '-', gpsReportTime: '-' }
              that.setData({ deviceInfo: updated })
            }
          }
        } else {
          // 无配置数据，清空显示
          if (that.data.deviceInfo) {
            const updated = { ...that.data.deviceInfo, reportInterval: '-', mainPeriod: 0, powerOnTime: '-', gpsReportTime: '-' }
            that.setData({ deviceInfo: updated })
          }
        }
      },
      fail: (err) => {
        console.error('设备配置查询失败:', err)
      }
    })
  },

  // 根据设备配置lorastr和最后上报时间判断当前是否休眠
  // 规则：最后上报时间超过1小时 → 直接判定为休眠；否则按配置的开机时间段判断
  _isDormantNow(configLorastr, rawTime) {
    // 如果最后上报时间超过1小时，直接判定为休眠
    if (rawTime) {
      const lastTime = new Date(rawTime).getTime()
      if (!isNaN(lastTime)) {
        const oneHourAgo = Date.now() - 3600000
        if (lastTime < oneHourAgo) {
          return true
        }
      }
    }
    // 如果上报时间在1小时内，按配置的开机时间段判断（两位代号，兼容旧格式 8-6）
    if (!configLorastr) return false
    const parts = configLorastr.split('|')
    if (parts.length < 3 || !parts[2]) return false
    const configParts = parts[2].split(',')
    if (configParts.length < 2 || !configParts[1]) return false
    const powerRaw = configParts[1].trim()
    const win = timeWindowCodec.parseTimeWindow(powerRaw)
    if (!win) return false
    const now = new Date()
    const currentMinutes = now.getHours() * 60 + now.getMinutes()
    const startMinutes = win.start * 60
    // end=23 代表 23:59
    const endMinutes = win.end === 23 ? 23 * 60 + 59 : win.end * 60
    return currentMinutes < startMinutes || currentMinutes >= endMinutes
  },

  // 自动加载轨迹记录
  loadTodayRecords(offset, callback) {
    const deviceId = this.data.deviceId
    if (!deviceId) return
    const info = { limit: this.data.recordLimit, deviceId: deviceId, offset: offset || 0, wechatid: getApp().getWechatId() }
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: info,
        time: getApp().formatTime()
      },
      success: (res) => {
        console.log('设备轨迹查询返回:', JSON.stringify(res.data))
        const newRecords = this._parseRecords(res.data)
        if (offset > 0) {
          // 加载更多：追加到列表末尾
          const merged = this.data.recordList.concat(newRecords)
          this.setData({
            recordList: merged,
            recordOffset: offset + newRecords.length,
            hasMore: newRecords.length >= this.data.recordLimit,
            isLoadingMore: false,
            showRecordTable: merged.length > 0
          })
        } else {
          // 首次加载或刷新：替换整个列表
          this.setData({
            recordList: newRecords,
            recordOffset: newRecords.length,
            hasMore: newRecords.length >= this.data.recordLimit,
            isLoadingMore: false,
            isRefreshing: false,
            showRecordTable: newRecords.length > 0
          })
        }
        if (callback) callback()
      },
      fail: (err) => {
        console.error('设备轨迹查询失败:', err)
        this.setData({ isLoadingMore: false, isRefreshing: false })
        if (callback) callback()
      }
    })
  },

  // 下拉刷新
  onRefreshRecords() {
    if (this.data.isRefreshing) return
    this.setData({ isRefreshing: true })
    this.loadTodayRecords(0, () => {
      wx.showToast({ title: '刷新成功', icon: 'none', duration: 1000 })
    })
  },

  // 滚动到底部加载更多
  onLoadMoreRecords() {
    if (this.data.isLoadingMore || !this.data.hasMore) return
    this.setData({ isLoadingMore: true })
    this.loadTodayRecords(this.data.recordOffset)
  },


  // ========== 编辑设备弹窗 ==========
  onDetailEdit() {
    const info = this.data.deviceInfo
    if (!info) return
    this.setData({
      showEditModal: true,
      editOldDeviceKey: info.deviceId,
      editRename: info.rename || '',
      editProductKey: info.ProductKey || '',
      editDeviceName: info.DeviceName || '',
      editDeviceSecret: info.DeviceSecret || '',
      editPicurl: info.picurl || '',
      editPicFilePath: '',
      editVisible: info.visible === true || info.visible === 'true' || info.visible === 1,
      // 已有 ProductKey 的设备：直接显示中继配置输入框（保留原样式）
      // 没有 ProductKey 的设备：隐藏，需点击"设为中继"才展开
      showRelayFields: !!(info.ProductKey)
    })
  },

  // 点击"设为中继"：展开 ProductKey/DeviceName/DeviceSecret 输入框（弹框自动变长）
  onSetRelayTap() {
    this.setData({ showRelayFields: true })
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
    const rename = this.data.editRename.trim()
    const ProductKey = this.data.editProductKey.trim()
    const DeviceName = this.data.editDeviceName.trim()
    const DeviceSecret = this.data.editDeviceSecret.trim()
    const visible = this.data.editVisible
    const picFilePath = this.data.editPicFilePath

    this.setData({ showEditModal: false })

    if (picFilePath) {
      wx.showLoading({ title: '压缩上传...' })
      const objectKey = 'device/' + oldKey + '_' + Date.now() + '.jpg'
      compressImage(picFilePath).then((compressedPath) => {
        return uploadToOSS(compressedPath, objectKey, 'device/')
      }).then((ossUrl) => {
        this._doEditConfirm(oldKey, rename, ProductKey, DeviceName, DeviceSecret, visible, ossUrl)
      }).catch((err) => {
        wx.hideLoading()
        console.error('OSS 上传失败:', err)
        wx.showToast({ title: '上传失败', icon: 'error', duration: 2000 })
      })
    } else {
      this._doEditConfirm(oldKey, rename, ProductKey, DeviceName, DeviceSecret, visible, this.data.editPicurl)
    }
  },

  _doEditConfirm(oldKey, rename, ProductKey, DeviceName, DeviceSecret, visible, picurl) {
    wx.showLoading({ title: '更新中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'updateDevice',
        info: { deviceId: oldKey,  rename, ProductKey, DeviceName, DeviceSecret, visible, picurl, wechatid: getApp().getWechatId() }
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
        info: { deviceId, cowsheepId: item.cowsheepId, wechatid: getApp().getWechatId() }
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
  // 点击发送指令图标 → 根据是否有ProductKey跳转不同页面
  onSendCmdTap() {
    const deviceId = this.data.deviceId
    if (!deviceId) return
    const deviceInfo = this.data.deviceInfo
    // 有 ProductKey 则走中继DTU指令页
    if (deviceInfo && deviceInfo.ProductKey) {
      wx.navigateTo({
        url: '/pages/relay-dtu-cmd/relay-dtu-cmd?deviceId=' + encodeURIComponent(deviceId)
      })
    } else {
      wx.navigateTo({
        url: '/pages/dtu-cmd/dtu-cmd?deviceId=' + encodeURIComponent(deviceId)
      })
    }
  },

  // 查看定位轨迹 → 跳转轨迹地图（轨迹页自行加载数据）
  onViewTrackTap() {
    wx.navigateTo({ url: '/pages/trackmap/trackmap?deviceId=' + encodeURIComponent(this.data.deviceId || '') })
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
        action: 'getDeviceBestRssibyId',
        info: { limit: 3, deviceId: targetDeviceId, wechatid: getApp().getWechatId() } 
      },
      success: (res) => {
        wx.hideLoading()
        console.log('[获取定位] getDeviceBestRssibyId 返回:', JSON.stringify(res.data))

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
        let bestRssi = -999
        let bestCount = 0
        Object.keys(deviceBestRssi).forEach(devId => {
          const r = deviceBestRssi[devId]
          const c = deviceCount[devId]
          const hasRssi = r > -999
          if (hasRssi) {
            // RSSI 越大（越接近0）信号越好，用 > 比较
            if (r > bestRssi || (r === bestRssi && c > bestCount)) {
              bestRssi = r; bestCount = c; bestDevice = devId
            }
          } else if (bestRssi <= -999) {
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
      timestamp: Date.now(),
      info: { wechatid: getApp().getWechatId() }
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

  // 点击数据记录：如果是定位记录(msgType=1)，跳转到定位地图页
  onRecordTap(e) {
    const index = e.currentTarget.dataset.index
    const record = this.data.recordList[index]
    if (!record) return
    // 定位(msgType=1)和跟踪(msgType=5)才响应点击
    if (record.msgType !== '1' && record.msgType !== '5') return

    // 从 lorastr 中提取 GPS 坐标：格式 type|deviceId|lat,lng|...
    let lat = null, lng = null
    if (record.lorastr && record.lorastr !== '-') {
      const segs = record.lorastr.split(/[｜|]/)
      if (segs.length >= 3 && segs[2]) {
        const parts = segs[2].split(/[,，]\s*/)
        if (parts.length >= 2) {
          lat = parseFloat(parts[0])
          lng = parseFloat(parts[1])
        }
      }
    }
    if (isNaN(lat) || isNaN(lng)) {
      wx.showToast({ title: '该记录无有效坐标', icon: 'none' })
      return
    }

    // 跳转到定位地图页
    wx.navigateTo({
      url: '/pages/location-map/location-map' +
        '?lat=' + lat +
        '&lng=' + lng +
        '&deviceId=' + encodeURIComponent(record.deviceId || '') +
        '&time=' + encodeURIComponent(record.rawTime || '') +
        '&lorastr=' + encodeURIComponent(record.lorastr || '') +
        '&upDateDevice=' + encodeURIComponent(record.upDateDevice || '')
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



  // 列出数据的解析：和features页parseRecordList格式一致
  _parseRecords(data) {
    let rawList = []
    if (data && data.data && Array.isArray(data.data)) {
      rawList = data.data
    } else if (Array.isArray(data)) {
      rawList = data
    }
    const records = rawList.map((record, idx) => {
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
      const upDateDeviceAlias = (this._deviceRenameMap && this._deviceRenameMap[upDateDevice]) || ''
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
      // 1=定位  2=对时  3=电量  5=跟踪  6=设置
      let msgType = '-'
      if (lorastr && lorastr !== '-') {
        const parts = lorastr.split('|')
        msgType = parts[0] || '-'
      }

      // 显示文本：默认显示原始LORA数据；仅当设置"显示转换"时，对时记录(TYPE=2)与配置记录(TYPE=6)显示换算内容，其余类型仍保持原始数据
      const displayLorastr = (this.data.showConverted && (msgType === '2' || msgType === '6')) ? this._buildDisplayLorastr(lorastr, msgType) : lorastr

      return { _key: rawTime + '_' + idx, deviceId, upDateDevice, upDateDeviceAlias, lorastr, displayLorastr, msgType, rssi: finalRssi, snr: finalSnr, date: date || '-', time_part: time_part || '', rawTime, bgColor: this._devicePastel(upDateDevice), deviceColor: this._deviceColor(upDateDevice) }
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

  // 对时/配置记录显示转换：按消息类型分派
  // TYPE=2 对时：把第3段秒级时间戳替换为日期时间（按UTC显示）
  //   如 "2|v4-0|1787230505|93|2" → "2|v4-0|2026-08-20 12:55:05|93|2"
  // TYPE=6 配置：把第3段的工作时间/GPS工作时间代号转换为可读时间窗
  //   如 "6|v4-26|5,0M,30|1" → "6|v4-26|5,00:00-23:59 10:00-12:00|1"
  //   带主周期: "6|v4-26|5,0M,30,2|1" → "6|v4-26|5,00:00-23:59 10:00-12:00,2|1"
  // 无法转换时原样返回
  _buildDisplayLorastr(lorastr, msgType) {
    if (!lorastr || lorastr === '-') return lorastr
    if (msgType === '2') {
      const parts = String(lorastr).split('|')
      if (parts.length < 3 || !parts[2]) return lorastr
      const fmt = this._formatTsToDateTime(parts[2])
      if (!fmt) return lorastr
      parts[2] = fmt
      return parts.join('|')
    }
    if (msgType === '6') {
      return this._formatConfigLorastr(lorastr)
    }
    return lorastr
  },

  // TYPE=6 配置记录转换：第3段格式 "周期,开机时间代号,GPS工作时间代号[,主周期]"
  // → "周期,工作时间 GPS工作时间[,主周期]"（换算后不再保留原始代号；第4段主周期原样保留显示）
  _formatConfigLorastr(lorastr) {
    if (!lorastr || lorastr === '-') return lorastr
    const parts = String(lorastr).split('|')
    if (parts.length < 3 || !parts[2]) return lorastr
    const segs = parts[2].split(',')
    if (segs.length < 3) return lorastr
    const workTime = timeWindowCodec.formatTimeRange(segs[1])
    const gpsTime = timeWindowCodec.formatTimeRange(segs[2])
    // 解析失败时保留原代号
    const workDisplay = workTime === '-' ? segs[1] : workTime
    const gpsDisplay = gpsTime === '-' ? segs[2] : gpsTime
    // 第4段主周期（大周期，小时 1-4），存在则追加在两个时间后面显示
    let mainPeriodPart = ''
    if (segs.length >= 4 && /^\d{1,2}$/.test(segs[3])) {
      mainPeriodPart = ',' + segs[3]
    }
    parts[2] = segs[0] + ',' + workDisplay + ' ' + gpsDisplay + mainPeriodPart
    return parts.join('|')
  },

  // 秒级时间戳（兼容13位毫秒）→ "YYYY-MM-DD HH:mm:ss"（UTC时间）；无法转换返回空串
  _formatTsToDateTime(raw) {
    if (raw === undefined || raw === null || raw === '') return ''
    const s = String(raw).trim()
    let ms = NaN
    if (/^\d{10}$/.test(s)) {
      // 秒级时间戳
      ms = parseInt(s, 10) * 1000
    } else if (/^\d{13}$/.test(s)) {
      // 毫秒级时间戳
      ms = parseInt(s, 10)
    } else if (/^\d{10}\.\d+$/.test(s)) {
      // 带小数的秒级时间戳
      ms = Math.round(parseFloat(s) * 1000)
    }
    if (isNaN(ms)) return ''
    const d = new Date(ms)
    if (isNaN(d.getTime())) return ''
    const pad = (n) => String(n).padStart(2, '0')
    return d.getUTCFullYear() + '-' + pad(d.getUTCMonth() + 1) + '-' + pad(d.getUTCDate()) +
      ' ' + pad(d.getUTCHours()) + ':' + pad(d.getUTCMinutes()) + ':' + pad(d.getUTCSeconds())
  },

})
