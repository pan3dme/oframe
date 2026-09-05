// index.js - 首页：展示最近选中设备的详情（每天打开小程序先到首页）
// 设备列表点击某台设备 → 缓存选中设备 → 切回首页展示对应详情
// 无缓存选中设备时不展示设备，引导去设备列表选择
const API_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')
const batterySwap = require('../../config/battery-swap.js')
const timeWindowCodec = require('../../utils/time-window-codec.js')

// DTU 指令转发云函数地址
const FC_URL = 'https://gpsmoveinfo.cn/fc/sendtodtucmd'

Page({
  data: {
    navTitle: '设备详情',
    // 是否已选择设备（缓存了上次选中的设备ID）
    hasSelected: false,
    selectedDeviceId: '',
    deviceInfo: null,
    loading: false,
    // 数据记录列表
    showRecordTable: false,
    recordList: [],
    recordLimit: 10,
    recordOffset: 0,
    hasMore: true,
    isLoadingMore: false,
    isRefreshing: false,
    // 是否显示转换（设置页开关控制）：开启后对时/配置记录显示可读内容
    showConverted: false,
    // 设备功能按钮（每行3个）
    featureBtns: [
      { id: 'location', label: '实时定位', color: '#F56C6C', icon: '📍' },
      { id: 'track',    label: '轨迹地图', color: '#F0A020', icon: '🗺' },
      { id: 'records',  label: '数据列表', color: '#00ACC1', icon: '📋' },
      { id: 'setting',  label: '设备设置', color: '#26A69A', icon: '⚙' },
      { id: 'alarm',    label: '报警信息', color: '#1E88E5', icon: '🔔' },
      { id: 'password', label: '修改密码', color: '#5C6BC0', icon: '🔑' },
      { id: 'fence',    label: '电子栅栏', color: '#42A5F5', icon: '📡' }
    ]
  },

  onLoad() {
    console.log('设备详情首页 onLoad')
    if (!this._checkLogin()) return
    this._readSettings()
    this._loadSelection(false)
  },

  onShow() {
    if (!this._checkLogin()) return
    this._readSettings()
    // 从"完整管理"页返回：设备可能已被编辑（改名/换图/绑定牛羊），静默刷新详情
    if (this._pendingManage) {
      this._pendingManage = false
      const id = dataCache.getHomeSelectedDevice()
      if (id) {
        this._loadAll(id, false, () => {})
      }
      return
    }
    // 可能从设备列表切回，或上次加载失败重新进入，重新比对选中设备
    const cached = dataCache.getHomeSelectedDevice()
    if (cached !== this.data.selectedDeviceId || (cached && !this.data.deviceInfo && !this._loading)) {
      this._loadSelection(false)
      return
    }
    if (!cached && this.data.hasSelected) {
      this.setData({ hasSelected: false, selectedDeviceId: '', deviceInfo: null, recordList: [], showRecordTable: false, navTitle: '设备详情' })
      return
    }
    // 刷新换电相对时间显示
    if (this._swapTimeTs && this.data.deviceInfo) {
      const rel = batterySwap.formatRelativeTime(this._swapTimeTs)
      this.setData({ 'deviceInfo.swapTimeRel': rel })
    }
  },

  onHide() {
    // 无后台定时器
  },

  // 检查登录状态：本次会话未确认登录则跳转登录页
  _checkLogin() {
    const app = getApp()
    if (app.globalData.sessionConfirmed) return true
    if (app.globalData.autoLogin) {
      try {
        const loginInfo = wx.getStorageSync('login_info')
        const serverData = wx.getStorageSync('login_server_data')
        if (loginInfo && loginInfo.isLoggedIn && serverData && serverData.data && serverData.data.primaryKey) {
          app.globalData.loginInfo = loginInfo
          app.globalData.serverData = serverData
          app.globalData.sessionConfirmed = true
          return true
        }
      } catch (e) { /* ignore */ }
    }
    wx.reLaunch({ url: '/pages/login/login' })
    return false
  },

  // 读取本地设置（管理员无需在此判断，管理按钮统一跳完整详情页）
  _readSettings() {
    let showConverted = false
    try {
      const conv = wx.getStorageSync('setting_show_converted')
      if (conv !== '' && conv !== undefined && conv !== null) {
        showConverted = conv === true || conv === 'true' || conv === 1 || conv === '1'
      }
    } catch (e) { /* ignore */ }
    this.setData({ showConverted })
    this._refreshDisplayLorastr()
  },

  // 根据当前"显示转换"开关刷新已加载记录的显示文本
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

  // ========== 加载选中设备 ==========
  _loadSelection(force) {
    const id = dataCache.getHomeSelectedDevice()
    if (!id) {
      this.setData({ hasSelected: false, selectedDeviceId: '', deviceInfo: null, recordList: [], showRecordTable: false, navTitle: '设备详情', loading: false })
      this._loading = false
      return
    }
    this._loading = true
    this.setData({ hasSelected: true, selectedDeviceId: id, navTitle: '设备详情', loading: true })
    this._loadAll(id, force, () => {
      this._loading = false
      this.setData({ loading: false })
    })
  },

  // 并行加载设备信息/配置/换电时间/当天记录
  _loadAll(deviceId, force, done) {
    const that = this
    const finish = () => {
      if (done) done()
    }
    // 设备信息（含LOT最新记录）就绪后再拉记录列表
    this.loadDeviceInfo(deviceId, force, () => {
      that.loadTodayRecords(0, () => finish())
    })
    this.loadDeviceConfig(deviceId, force)
    this.loadSwapTime(deviceId)
  },

  // ========== 设备信息 ==========
  loadDeviceInfo(deviceId, force, done) {
    let deviceItem = null
    let lotDataRes = null
    let merged = 0
    const merge = () => {
      merged++
      if (merged < 2) return

      if (!deviceItem) {
        // 选中设备已不存在（可能被删除/隐藏），退回空状态引导重新选择
        this.setData({ hasSelected: false, selectedDeviceId: deviceId, deviceInfo: null, recordList: [], showRecordTable: false })
        if (done) done()
        return
      }

      // 从LOT表取最新记录
      let lotRec = null
      if (lotDataRes && lotDataRes.lotList) {
        lotRec = lotDataRes.lotList.find(v => v.deviceId === deviceId)
      }

      // 从 lotRec 的 lorastr 提取电量: "1|v4-22|26.52968,109.39078|1.0|5.1" 第4段(index 3)
      let batInfo = null
      if (lotRec && lotRec.lorastr) {
        const parts = lotRec.lorastr.split('|')
        if (parts.length >= 4 && parts[3]) {
          batInfo = { battery: parts[3], rawTime: lotRec.rawTime }
        }
      }

      const enriched = Object.assign({}, deviceItem)

      // 设备表原始时间作为"上次充电时间"
      const devDate = deviceItem.date && deviceItem.date !== '-' ? deviceItem.date : ''
      const devTime = deviceItem.time_part && deviceItem.time_part !== '-' ? deviceItem.time_part : ''
      enriched.chargeTime = devDate || devTime ? (devDate + ' ' + devTime).trim() : ''

      // 对比设备表、LOT表（含电量）时间取最新
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
      if (batInfo) {
        enriched.battery = batInfo.battery
      }

      // 解析显示字段默认值（等待配置接口刷新）
      enriched.reportInterval = '-'
      enriched.mainPeriod = 0
      enriched.powerOnTime = '-'
      enriched.gpsReportTime = '-'
      enriched.inPowerOn = true
      enriched.swapTime = ''
      enriched.swapTimeRel = ''

      const that = this
      const showInfo = () => {
        that.setData({
          deviceInfo: Object.assign({}, enriched, { bindName: that._bindName || '' }),
          hasSelected: true,
          navTitle: enriched.rename ? (deviceId + ' · ' + enriched.rename) : deviceId
        })
        if (done) done()
      }

      if (!deviceItem.link_cowsheep_id) {
        that._bindName = ''
        showInfo()
        return
      }
      dataCache.getLivestockList((livestockData) => {
        const list = (livestockData && livestockData.livestockList) ? livestockData.livestockList : []
        const found = list.find(v => v.cowsheepId === deviceItem.link_cowsheep_id)
        that._bindName = found ? found.name : ''
        showInfo()
      }, force)
    }

    dataCache.getDeviceList((deviceData) => {
      if (deviceData && deviceData.recordList) {
        deviceItem = deviceData.recordList.find(v => v.deviceId === deviceId) || null
        // 缓存全部设备id → 别名映射，用于记录列表显示上传设备别名
        const renameMap = {}
        deviceData.recordList.forEach(v => {
          if (v.deviceId) renameMap[v.deviceId] = v.rename || ''
        })
        this._deviceRenameMap = renameMap
      }
      merge()
    }, force)
    dataCache.getDeviceLotRefresh((data) => { lotDataRes = data; merge() }, force)
  },

  // ========== 最近换电时间（来自对时电量跳升检测缓存） ==========
  loadSwapTime(deviceId) {
    const that = this
    const apply = (swap) => {
      const timeStr = swap ? (swap.timeStr || '') : ''
      const ts = swap && swap.time ? swap.time : (timeStr ? new Date(timeStr).getTime() : 0)
      // 2025年之前视为历史脏数据
      if (ts && ts < batterySwap.MIN_VALID_SWAP_TIME) {
        that._swapTime = ''
        that._swapTimeTs = 0
        if (that.data.deviceInfo) {
          that.setData({ 'deviceInfo.swapTime': '', 'deviceInfo.swapTimeRel': '' })
        }
        return
      }
      that._swapTime = timeStr
      that._swapTimeTs = ts
      const rel = batterySwap.formatRelativeTime(ts)
      if (that.data.deviceInfo) {
        that.setData({ 'deviceInfo.swapTime': timeStr, 'deviceInfo.swapTimeRel': rel })
      }
    }
    const cached = batterySwap.getLastSwap(deviceId, (latest) => {
      apply(latest)
    })
    apply(cached)
  },

  // ========== 设备配置（上报周期/开机时间/GPS工作时间） ==========
  // 统一走 dataCache 的全局配置缓存：先读缓存立刻显示，再按 force 决定是否刷新网络
  loadDeviceConfig(deviceId, force) {
    if (!deviceId) return
    const that = this
    dataCache.getDeviceConfigAll((configData) => {
      const configMap = (configData && configData.configMap) ? configData.configMap : {}
      const config = configMap[deviceId]
      if (config && config.lorastr) {
        that._applyConfigFromLorastr(config.lorastr)
      } else {
        // 配置表无该设备记录：清空显示
        if (that.data.deviceInfo) {
          const updated = Object.assign({}, that.data.deviceInfo, {
            reportInterval: '-', mainPeriod: 0, powerOnTime: '-', gpsReportTime: '-', inPowerOn: true
          })
          that.setData({ deviceInfo: updated })
        }
      }
    }, force)
  },

  // 解析配置 lorastr：上报周期/开机时间/GPS工作时间/主周期
  _applyConfigFromLorastr(configLorastr) {
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
        if (configParts.length >= 4) {
          const mp = parseInt(configParts[3].trim(), 10)
          if (!isNaN(mp) && mp >= 1 && mp <= 10) mainPeriod = mp
        }
      }
    }
    if (this.data.deviceInfo) {
      const updated = Object.assign({}, this.data.deviceInfo, {
        reportInterval, mainPeriod, powerOnTime, gpsReportTime, inPowerOn: this._isInPowerOnNow(configLorastr)
      })
      this.setData({ deviceInfo: updated })
    }
  },

  // 当前是否在开机时间窗口内（仅按配置窗口判断）
  _isInPowerOnNow(configLorastr) {
    if (!configLorastr) return true
    const parts = configLorastr.split('|')
    if (parts.length < 3 || !parts[2]) return true
    const configParts = parts[2].split(',')
    if (configParts.length < 2 || !configParts[1]) return true
    const win = timeWindowCodec.parseTimeWindow(configParts[1].trim())
    if (!win) return true
    const now = new Date()
    const currentMinutes = now.getHours() * 60 + now.getMinutes()
    const startMinutes = win.start * 60
    const endMinutes = win.end === 23 ? 23 * 60 + 59 : win.end * 60
    return currentMinutes >= startMinutes && currentMinutes < endMinutes
  },

  // ========== 数据记录 ==========
  loadTodayRecords(offset, callback) {
    const deviceId = this.data.selectedDeviceId
    if (!deviceId) {
      if (callback) callback()
      return
    }
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
        const newRecords = this._parseRecords(res.data)
        if (offset > 0) {
          const merged = this.data.recordList.concat(newRecords)
          this.setData({
            recordList: merged,
            recordOffset: offset + newRecords.length,
            hasMore: newRecords.length >= this.data.recordLimit,
            isLoadingMore: false,
            showRecordTable: merged.length > 0
          })
        } else {
          this.setData({
            recordList: newRecords,
            recordOffset: newRecords.length,
            hasMore: newRecords.length >= this.data.recordLimit,
            isLoadingMore: false,
            isRefreshing: false,
            showRecordTable: newRecords.length > 0
          })
        }
        this._refreshDisplayLorastr()
        if (callback) callback()
      },
      fail: (err) => {
        console.error('设备轨迹查询失败:', err)
        this.setData({ isLoadingMore: false, isRefreshing: false })
        if (callback) callback()
      }
    })
  },

  // 下拉刷新：强制刷新各数据源并重新加载
  onRefreshNow() {
    if (this.data.isRefreshing) return
    this.setData({ isRefreshing: true })
    const id = dataCache.getHomeSelectedDevice()
    if (!id) {
      this.setData({ isRefreshing: false })
      return
    }
    this._loadAll(id, true, () => {
      this.setData({ isRefreshing: false })
      wx.showToast({ title: '已刷新', icon: 'success', duration: 800 })
    })
  },

  // 滚动到底部加载更多记录
  onLoadMoreRecords() {
    if (this.data.isLoadingMore || !this.data.hasMore) return
    this.setData({ isLoadingMore: true })
    this.loadTodayRecords(this.data.recordOffset)
  },

  // ========== 顶部操作：发送指令 / 获取定位 / 轨迹 / 预览图片 ==========
  onSendCmdTap() {
    const deviceId = this.data.selectedDeviceId
    if (!deviceId) return
    const deviceInfo = this.data.deviceInfo
    if (deviceInfo && deviceInfo.ProductKey) {
      wx.navigateTo({ url: '/pages/relay-dtu-cmd/relay-dtu-cmd?deviceId=' + encodeURIComponent(deviceId) })
    } else {
      wx.navigateTo({ url: '/pages/dtu-cmd/dtu-cmd?deviceId=' + encodeURIComponent(deviceId) })
    }
  },

  onViewTrackTap() {
    wx.navigateTo({ url: '/pages/trackmap/trackmap?deviceId=' + encodeURIComponent(this.data.selectedDeviceId || '') })
  },

  onPreviewImage() {
    const picurl = this.data.deviceInfo && this.data.deviceInfo.picurl
    if (picurl) {
      wx.previewImage({ current: picurl, urls: [picurl] })
    }
  },

  // ========== 获取定位（快捷DTU指令） ==========
  onGetLocationTap() {
    const deviceId = this.data.selectedDeviceId
    if (!deviceId) return
    const cmdText = JSON.stringify({ cmd: 'upgps', value: 0 })
    const deviceInfo = this.data.deviceInfo
    if (deviceInfo && deviceInfo.ProductKey && deviceInfo.DeviceName) {
      this._doSendDTU(deviceInfo, deviceId, cmdText)
    } else {
      wx.showLoading({ title: '查询上传设备...' })
      this._queryUploadDevice(deviceId, cmdText)
    }
  },

  _queryUploadDevice(targetDeviceId, cmdText) {
    const that = this
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceBestRssibyId',
        info: { limit: 2, deviceId: targetDeviceId, wechatid: getApp().getWechatId() }
      },
      success: (res) => {
        wx.hideLoading()
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
          if (r > -999) {
            if (r > bestRssi || (r === bestRssi && c > bestCount)) {
              bestRssi = r; bestCount = c; bestDevice = devId
            }
          } else if (bestRssi <= -999) {
            if (c > bestCount) { bestCount = c; bestDevice = devId }
          }
        })
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
          that._doSendDTU(uploadDevice, targetDeviceId, cmdText)
        }, false)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('[获取定位] getDeviceBestRssibyId 失败:', err)
        wx.showToast({ title: '查询上传设备失败', icon: 'error' })
      }
    })
  },

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
    wx.showLoading({ title: '发送定位指令...' })
    wx.request({
      url: FC_URL,
      method: 'POST',
      data: payload,
      timeout: 10000,
      success: () => {
        wx.hideLoading()
        wx.showToast({ title: '定位指令已发送 → ' + targetDeviceId, icon: 'success' })
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('[获取定位] DTU发送失败:', err)
        wx.showToast({ title: '发送失败', icon: 'error' })
      }
    })
  },

  _parseRssi(val) {
    if (val === undefined || val === null || val === '' || val === '-') return -999
    const n = parseInt(val, 10)
    return isNaN(n) ? -999 : n
  },

  // 点击数据记录：定位/跟踪记录跳转定位地图
  onRecordTap(e) {
    const index = e.currentTarget.dataset.index
    const record = this.data.recordList[index]
    if (!record) return
    if (record.msgType !== '1' && record.msgType !== '5') return
    let lat = null
    let lng = null
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

  // ========== 记录解析（与 device-detail 一致） ==========
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
        record.attributes.forEach(item => { attr[item.columnName] = item.columnValue })
      }
      if (record.primaryKey) {
        record.primaryKey.forEach(item => { attr[item.name] = item.value })
      }
      const deviceId = attr.deviceId || attr.deviceid || record.deviceId || record.deviceid || '-'
      const upDateDevice = attr.upDateDevice || attr.updatedevice || record.upDateDevice || record.updatedevice || '-'
      const upDateDeviceAlias = (this._deviceRenameMap && this._deviceRenameMap[upDateDevice]) || ''
      const lorastr = attr.lorastr || record.lorastr || '-'
      const rawTime = attr.time || record.time || '-'
      const rssi = attr.rssi != null ? attr.rssi : (record.rssi != null ? record.rssi : '')
      const snr = attr.snr != null ? attr.snr : (record.snr != null ? record.snr : '')
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']

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

      let msgType = '-'
      if (lorastr && lorastr !== '-') {
        const parts = lorastr.split('|')
        msgType = parts[0] || '-'
      }
      const displayLorastr = (this.data.showConverted && (msgType === '2' || msgType === '6'))
        ? this._buildDisplayLorastr(lorastr, msgType)
        : lorastr

      return {
        _key: rawTime + '_' + idx,
        deviceId,
        upDateDevice,
        upDateDeviceAlias,
        lorastr,
        displayLorastr,
        msgType,
        rssi: finalRssi,
        snr: finalSnr,
        date: date || '-',
        time_part: time_part || '',
        rawTime,
        bgColor: this._devicePastel(upDateDevice),
        deviceColor: this._deviceColor(upDateDevice)
      }
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

  // 对时/配置记录显示转换
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

  _formatConfigLorastr(lorastr) {
    if (!lorastr || lorastr === '-') return lorastr
    const parts = String(lorastr).split('|')
    if (parts.length < 3 || !parts[2]) return lorastr
    const segs = parts[2].split(',')
    if (segs.length < 3) return lorastr
    const workTime = timeWindowCodec.formatTimeRange(segs[1])
    const gpsTime = timeWindowCodec.formatTimeRange(segs[2])
    const workDisplay = workTime === '-' ? segs[1] : workTime
    const gpsDisplay = gpsTime === '-' ? segs[2] : gpsTime
    let mainPeriodPart = ''
    if (segs.length >= 4 && /^\d{1,2}$/.test(segs[3])) {
      mainPeriodPart = ',' + segs[3]
    }
    parts[2] = segs[0] + ',' + workDisplay + ' ' + gpsDisplay + mainPeriodPart
    return parts.join('|')
  },

  _formatTsToDateTime(raw) {
    if (raw === undefined || raw === null || raw === '') return ''
    const s = String(raw).trim()
    let ms = NaN
    if (/^\d{10}$/.test(s)) {
      ms = parseInt(s, 10) * 1000
    } else if (/^\d{13}$/.test(s)) {
      ms = parseInt(s, 10)
    } else if (/^\d{10}\.\d+$/.test(s)) {
      ms = Math.round(parseFloat(s) * 1000)
    }
    if (isNaN(ms)) return ''
    const d = new Date(ms)
    if (isNaN(d.getTime())) return ''
    const pad = (n) => String(n).padStart(2, '0')
    return d.getUTCFullYear() + '-' + pad(d.getUTCMonth() + 1) + '-' + pad(d.getUTCDate()) +
      ' ' + pad(d.getUTCHours()) + ':' + pad(d.getUTCMinutes()) + ':' + pad(d.getUTCSeconds())
  },

  // 稳定浅色背景（按上传设备名）
  _devicePastel(deviceName) {
    if (!deviceName || deviceName === '-') return 'hsl(0, 0%, 95%)'
    let h = 0
    for (let i = 0; i < deviceName.length; i++) {
      h = (h * 31 + deviceName.charCodeAt(i)) % 360
    }
    const s = 35 + (h % 15)
    const l = 86 + (h % 10)
    return 'hsl(' + h + ', ' + s + '%, ' + l + '%)'
  },

  // 稳定鲜艳文字颜色（按上传设备名）
  _deviceColor(deviceName) {
    if (!deviceName || deviceName === '-') return '#999'
    const vividColors = ['#E53935', '#1E88E5', '#43A047', '#FB8C00', '#8E24AA', '#00ACC1', '#F4511E', '#D81B60', '#5E35B1', '#039BE5', '#2E7D32', '#C0CA33']
    let idx = 0
    for (let i = 0; i < deviceName.length; i++) {
      idx = (idx * 31 + deviceName.charCodeAt(i)) % vividColors.length
    }
    return vividColors[idx]
  },

  // ========== 设备功能按钮 ==========
  onFeatureBtnTap(e) {
    const id = e.currentTarget.dataset.id
    const btn = this.data.featureBtns.find(b => b.id === id)
    console.log('功能按钮点击:', id, btn && btn.label)
    if (id === 'location') {
      this.onRealtimeLocateTap()
      return
    }
    if (id === 'track') {
      this.onViewTrackTap()
      return
    }
    if (id === 'setting') {
      // 设备设置：中继设备(有ProductKey)进入中继DTU指令页 relay-dtu-cmd，普通设备进入 dtu-cmd
      this.onSendCmdTap()
      return
    }
    if (id === 'records') {
      this.onDataListTap()
      return
    }
    // TODO: 根据 id 跳转到对应子页
    wx.showToast({ title: '功能开发中', icon: 'none' })
  },

  // ========== 数据列表：打开设备记录子页（不显示设备信息） ==========
  onDataListTap() {
    const deviceId = this.data.selectedDeviceId
    if (!deviceId) return
    // 标记从管理页返回后需要刷新
    this._pendingManage = true
    wx.navigateTo({ url: '/pages/device-detail/device-detail?deviceId=' + encodeURIComponent(deviceId) + '&mode=records' })
  },

  // ========== 实时定位：从LOT最新表取该设备上报坐标 → 打开单设备定位地图（类似地图中心） ==========
  onRealtimeLocateTap() {
    const deviceId = this.data.selectedDeviceId
    if (!deviceId) return
    const that = this
    // 打开单设备定位地图，仅以该设备坐标为中心显示
    const open = (coord, rawTime, lorastr, upDateDevice) => {
      wx.navigateTo({
        url: '/pages/location-map/location-map' +
          '?lat=' + coord.lat +
          '&lng=' + coord.lng +
          '&deviceId=' + encodeURIComponent(deviceId) +
          '&time=' + encodeURIComponent(rawTime || '') +
          '&lorastr=' + encodeURIComponent(lorastr || '') +
          '&upDateDevice=' + encodeURIComponent(upDateDevice || '') +
          '&autoUpgps=1'
      })
    }

    wx.showLoading({ title: '获取最新定位...' })
    dataCache.refreshDeviceLotRefresh((lotData) => {
      wx.hideLoading()
      const lotList = (lotData && lotData.lotList) ? lotData.lotList : []
      const lotRec = lotList.find(v => v.deviceId === deviceId)
      const info = that.data.deviceInfo || {}
      // 优先LOT表刚上报的坐标；无匹配时回退首页已加载的最新记录（同样源自LOT/设备表）
      let lorastr = info.lorastr || ''
      let rawTime = info.rawTime || ''
      if (lotRec) {
        if (lotRec.lorastr) {
          lorastr = lotRec.lorastr
          rawTime = lotRec.rawTime || ''
        } else if (!lorastr) {
          rawTime = lotRec.rawTime || ''
        }
      }
      const coord = that._parseCoordFromLora(lorastr)
      if (coord) {
        open(coord, rawTime, lorastr, '')
        return
      }
      // 兜底：从首页已加载的定位/跟踪记录中取最新一条
      const list = that.data.recordList || []
      for (let i = 0; i < list.length; i++) {
        const r = list[i]
        const c = (r.msgType === '1' || r.msgType === '5') ? that._parseCoordFromLora(r.lorastr) : null
        if (c) {
          open(c, r.rawTime || '', r.lorastr || '', r.upDateDevice || '')
          return
        }
      }
      wx.showToast({ title: '该设备暂无有效定位点，请稍后再试', icon: 'none', duration: 2500 })
    })
  },

  // 从 lorastr 第3段解析经纬度，如 "1|v4-22|26.52968,109.39078|1.0|5.1"
  _parseCoordFromLora(lorastr) {
    if (!lorastr || lorastr === '-') return null
    const segs = String(lorastr).split(/[｜|]/)
    if (segs.length < 3 || !segs[2]) return null
    const parts = segs[2].split(/[,，]\s*/)
    if (parts.length < 2) return null
    const lat = parseFloat(parts[0])
    const lng = parseFloat(parts[1])
    if (isNaN(lat) || isNaN(lng)) return null
    if (lat < -90 || lat > 90 || lng < -180 || lng > 180) return null
    return { lat: lat, lng: lng }
  },

  // ========== 跳转 ==========
  // 无选中设备/想换设备 → 去设备列表
  goDeviceTab() {
    wx.switchTab({ url: '/pages/device/device' })
  },

  // 完整管理（编辑/绑定/发指令等都在原设备详情页）
  goFullManage() {
    const id = this.data.selectedDeviceId
    if (!id) return
    // 标记从管理页返回后需要刷新
    this._pendingManage = true
    wx.navigateTo({ url: '/pages/device-detail/device-detail?deviceId=' + encodeURIComponent(id) })
  },

  // ========== 分享 ==========
  onShareAppMessage() {
    return {
      title: '牛羊GPS定位管理 - 实时掌握每头牛羊的位置',
      path: '/pages/index/index'
    }
  },

  onShareTimeline() {
    return {
      title: '牛羊GPS定位管理 - 实时掌握每头牛羊的位置'
    }
  }
})
