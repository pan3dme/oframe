// bluetooth.js - 蓝牙连接页（纯UI层）
// 连接/收数/缓存/上报等核心逻辑在 utils/ble-manager.js 全局单例中，
// 离开本页连接保持、数据照常接收；本页只负责展示与操作入口。
const bleManager = require('../../utils/ble-manager.js')
const dataCache = require('../../config/data-cache')
const timeWindowCodec = require('../../utils/time-window-codec.js')

Page({
  data: {
    bluetoothConnected: false,
    bluetoothScanning: false,
    isSyncing: false,
    connectedDeviceName: '',
    devices: [],
    // 接收到的数据列表
    receivedList: [],
    displayList: [],       // 过滤后的显示列表（已连接）
    cacheDisplayList: [],  // 缓存数据显示列表（未连接时展示）
    filterType: '',        // ''=全部, 'gps', 'time', 'battery'
    // 是否显示转换（设置页开关控制）：开启后对时记录(TYPE=2)显示换算日期时间、配置记录(TYPE=6)显示时间窗
    showConverted: false,
    // 数据缓存（先存后上传）
    cacheQueue: [],
    cacheCount: 0,
    isCenterUploading: false, // 数据中心上传状态
    autoUpload: false,        // 自动上传模式（复选框勾选状态，重启保留）
    // 写入特征值信息（连接成功后缓存）
    writeDeviceInfo: null,
    // 发送指令弹窗
    showCmdModal: false,
    cmdMode: '',
    cmdDeviceRawList: [],      // 设备原始名称列表（发送时使用）
    cmdDeviceList: [],         // 显示用设备列表（带 RENAME 后缀）
    cmdDeviceIndex: 0,
    cmdText: '',
    cmdQuickSelected: 0  // 当前选中的快捷按钮（value值），0=未选中
  },

  _lastCacheTapTime: 0,   // 双击清空缓存用
  _scanTimer: null,       // 扫描超时定时器
  _lastCmdRaw: null,      // 上次指令设备列表引用（用于重置选中项）
  _deviceRenameMap: {},   // deviceId -> 别名（用于记录列表显示上传设备别名）

  // 每条记录生成不同的柔和浅色背景（风格参考设备详情记录）：
  // 色相按消息内容哈希，同一消息颜色稳定，不同消息颜色各不相同，便于列表逐条区分
  _recordPastel(msg) {
    const key = msg || ''
    let h = 0
    for (let i = 0; i < key.length; i++) {
      h = (h * 31 + key.charCodeAt(i)) % 360
    }
    const s = 35 + (h % 15)
    const l = 86 + (h % 10)
    return `hsl(${h}, ${s}%, ${l}%)`
  },

  // 按 upDateDevice 生成稳定文字颜色：同一设备始终同色，不同设备分配鲜艳颜色（与设备详情一致）
  _deviceColor(deviceName) {
    if (!deviceName || deviceName === '-') return '#999'
    const vividColors = [
      '#E53935', '#1E88E5', '#43A047', '#FB8C00', '#8E24AA', '#00ACC1',
      '#F4511E', '#D81B60', '#5E35B1', '#039BE5', '#2E7D32', '#C0CA33'
    ]
    let idx = 0
    for (let i = 0; i < deviceName.length; i++) {
      idx = (idx * 31 + deviceName.charCodeAt(i)) % vividColors.length
    }
    return vividColors[idx]
  },

  // ========== 页面生命周期 ==========
  onLoad() {
    // 初始化全局蓝牙管理（幂等，只注册一次数据接收回调）
    bleManager.init()
    // 加载设备别名映射（记录列表显示上传设备别名）
    this._loadRenameMap()
    // 读取"显示转换"设置（与设备详情一致）
    this._readSettings()
    // 开发工具不支持蓝牙，跳过自动连接避免 timeout
    const sysInfo = wx.getSystemInfoSync()
    if (sysInfo.platform === 'devtools') {
      console.log('开发工具环境，跳过蓝牙自动连接')
      return
    }
    // 已全局连接（从其他页面返回）则直接展示，否则自动扫描
    if (!bleManager.getState().connected) {
      this.connectBluetooth()
    }
  },

  onShow() {
    // 订阅全局状态：每次状态变化（收到数据、连接变化、上传进度）自动刷新
    if (!this._onBleUpdateBound) {
      this._onBleUpdateBound = this._onBleUpdate.bind(this)
    }
    bleManager.subscribe(this._onBleUpdateBound)
    // 读取"显示转换"设置（从设置页返回时刷新显示），并同步列表
    this._readSettings()
  },

  onHide() {
    if (this._onBleUpdateBound) bleManager.unsubscribe(this._onBleUpdateBound)
  },

  onUnload() {
    // 注意：不再断开蓝牙，连接与数据接收由全局 manager 保持
    if (this._onBleUpdateBound) bleManager.unsubscribe(this._onBleUpdateBound)
    if (this._scanTimer) { clearTimeout(this._scanTimer); this._scanTimer = null }
    wx.stopBluetoothDevicesDiscovery({ complete: () => {} })
    if (this._onDeviceFound) {
      try { wx.offBluetoothDeviceFound(this._onDeviceFound) } catch (e) { /* ignore */ }
    }
  },

  // 全局状态变更回调（订阅触发）
  _onBleUpdate() {
    const s = bleManager.getState()
    // 指令设备列表变化时重置选中项
    if (s.cmdDeviceRawList !== this._lastCmdRaw) {
      this._lastCmdRaw = s.cmdDeviceRawList
      if (this.data.showCmdModal) this.setData({ cmdDeviceIndex: 0 })
    }
    this._syncFromManager()
  },

  // 将全局状态同步到页面 data
  _syncFromManager() {
    const s = bleManager.getState()
    const ft = this.data.filterType
    const displayList = ft
      ? s.receivedList.filter(item => this._getMsgType(item) === ft)
      : s.receivedList
    const cacheList = (ft
      ? s.cacheQueue.filter(msg => this._getMsgType(msg) === ft)
      : s.cacheQueue
    ).slice().reverse() // 倒序显示，最新在前，与接收数据列表一致
    this.setData({
      bluetoothConnected: s.connected,
      connectedDeviceName: s.connectedDeviceName,
      isSyncing: s.isSyncing,
      isCenterUploading: s.isCenterUploading,
      autoUpload: s.autoUpload,
      cacheQueue: s.cacheQueue,
      cacheCount: s.cacheCount,
      writeDeviceInfo: s.writeDeviceInfo,
      cmdDeviceRawList: s.cmdDeviceRawList,
      cmdDeviceList: s.cmdDeviceList,
      displayList: displayList.map((msg, i) => this._buildRecordItem(msg, i)),
      cacheDisplayList: cacheList.map((msg, i) => this._buildRecordItem(msg, i))
    })
  },

  // 构建设备详情风格记录项（与 device-detail 数据记录显示一致）
  _buildRecordItem(msg, idx) {
    // 优先 JSON 解析
    let info = ''
    let rawTime = ''
    let upDateDevice = ''
    let rssi = ''
    let snr = ''
    try {
      const obj = JSON.parse(msg)
      info = obj.info || ''
      rawTime = obj.time || ''
      upDateDevice = obj.upDateDevice || ''
      rssi = obj.rssi !== undefined && obj.rssi !== null ? String(obj.rssi) : ''
      snr = obj.snr !== undefined && obj.snr !== null ? String(obj.snr) : ''
    } catch (e) {
      // 非 JSON（管道分隔等），整条作为 LORA 数据展示
      info = msg
    }

    // 类型：info 首段（1=定位 2=对时 3=电量 5=跟踪 6=设置）
    let msgType = '-'
    if (info) {
      const firstSeg = String(info).split('|')[0]
      if (firstSeg) msgType = firstSeg
    }

    const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']
    const alias = (this._deviceRenameMap && this._deviceRenameMap[upDateDevice]) || ''
    // 显示文本：默认显示原始LORA数据；仅当"显示转换"开启时，对时(TYPE=2)与配置(TYPE=6)显示换算内容（与设备详情一致）
    const rawLora = info || msg
    const displayLorastr = (this.data.showConverted && (msgType === '2' || msgType === '6'))
      ? this._buildDisplayLorastr(rawLora, msgType)
      : rawLora

    return {
      _key: msg + '_' + idx,
      date: date || '',
      time_part: time_part || '',
      upDateDevice,
      upDateDeviceAlias: alias,
      msgType,
      displayLorastr,
      rssi,
      snr,
      // 每条记录背景色各不相同（柔和浅色风格参考设备详情），按消息内容哈希生成
      bgColor: this._recordPastel(msg),
      deviceColor: this._deviceColor(upDateDevice)
    }
  },

  // ========== 显示转换（与设备详情数据记录一致） ==========
  // 读取本地设置："显示转换"开关
  _readSettings() {
    let showConverted = false
    try {
      const conv = wx.getStorageSync('setting_show_converted')
      if (conv !== '' && conv !== undefined && conv !== null) {
        showConverted = conv === true || conv === 'true' || conv === 1 || conv === '1'
      }
    } catch (e) { /* ignore */ }
    this.setData({ showConverted })
    // 开关变化后刷新已加载记录的显示文本（如从设置页返回时）
    this._syncFromManager()
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

  // ========== 扫描与连接 ==========
  connectBluetooth() {
    const that = this
    // 如果已连接，先断开再重新扫描
    if (bleManager.getState().connected) {
      bleManager.disconnect()
    }
    // 初始化蓝牙适配器
    wx.openBluetoothAdapter({
      success() {
        that.startScanDevices()
      },
      fail(err) {
        console.log('蓝牙初始化失败（开发工具无蓝牙属正常情况）:', err.errCode)
        if (err.errCode === 10001) {
          wx.showModal({
            title: '提示',
            content: '请先打开手机蓝牙和位置权限',
            showCancel: false
          })
        }
      }
    })
  },

  // 开始扫描设备
  startScanDevices() {
    const that = this
    this.setData({
      bluetoothScanning: true,
      devices: []
    })

    // 监听发现设备
    this._onDeviceFound = function (res) {
      res.devices.forEach(function (device) {
        const deviceName = device.localName || device.name
        if (deviceName && deviceName.includes('牛羊GPS')) {
          const devices = that.data.devices
          if (!devices.some(d => d.deviceId === device.deviceId)) {
            devices.push({
              deviceId: device.deviceId,
              name: deviceName,
              RSSI: device.RSSI
            })
            that.setData({ devices })
          }
        }
      })
    }
    wx.onBluetoothDeviceFound(this._onDeviceFound)

    // 开始搜索
    wx.startBluetoothDevicesDiscovery({
      allowDuplicatesKey: false,
      success() {
        wx.showToast({ title: '正在扫描设备...', icon: 'none', duration: 1000 })
        that._scanTimer = setTimeout(() => { that.stopScanDevices() }, 10000)
      },
      fail() {
        that.setData({ bluetoothScanning: false })
        wx.showToast({ title: '扫描启动失败', icon: 'error' })
      }
    })
  },

  // 停止扫描
  stopScanDevices() {
    if (this._scanTimer) { clearTimeout(this._scanTimer); this._scanTimer = null }
    wx.stopBluetoothDevicesDiscovery({
      success: () => { this.setData({ bluetoothScanning: false }) }
    })
  },

  // 点击设备进行连接
  connectToDevice(e) {
    const that = this
    const { deviceid, devicename } = e.currentTarget.dataset

    this.stopScanDevices()
    wx.showLoading({ title: '连接中...' })

    bleManager.connect(deviceid, devicename, () => {
      wx.hideLoading()
      that._syncFromManager()
      wx.showToast({ title: '蓝牙已连接', icon: 'success' })
    }, (err) => {
      wx.hideLoading()
      console.error('连接失败:', err)
      wx.showToast({ title: '设备连接失败', icon: 'error' })
    })
  },

  // 从首页缓存设备ID直连（跳过扫描）
  _reconnectDevice(deviceId) {
    const that = this
    wx.showLoading({ title: '正在重连蓝牙...' })
    bleManager.connect(deviceId, '已连接设备', () => {
      wx.hideLoading()
      that._syncFromManager()
      wx.showToast({ title: '蓝牙已连接', icon: 'success' })
    }, (err) => {
      wx.hideLoading()
      console.error('缓存设备直连失败:', err)
      wx.showModal({
        title: '连接失败',
        content: '缓存设备无法连接，是否重新扫描？',
        success: (res) => {
          if (res.confirm) that.connectBluetooth()
        }
      })
    })
  },

  // 断开蓝牙（手动断开按钮）
  disconnectBluetooth() {
    bleManager.disconnect(() => {
      this._syncFromManager()
    })
  },

  // ========== 显示相关 ==========
  // 加载 deviceId -> 别名映射（用于记录列表显示上传设备别名）
  _loadRenameMap() {
    const that = this
    dataCache.getDeviceList((cacheData) => {
      const recordList = (cacheData && cacheData.recordList) ? cacheData.recordList : []
      const map = {}
      recordList.forEach(v => { if (v.deviceId) map[v.deviceId] = v.rename || '' })
      if (JSON.stringify(that._deviceRenameMap) !== JSON.stringify(map)) {
        that._deviceRenameMap = map
        that._syncFromManager()
      }
    }, false)
  },

  // 点击数据记录：定位/跟踪记录跳转定位地图（与设备详情一致）
  onRecordTap(e) {
    const index = e.currentTarget.dataset.index
    const list = this.data.bluetoothConnected ? this.data.displayList : this.data.cacheDisplayList
    const record = list[index]
    if (!record) return
    if (record.msgType !== '1' && record.msgType !== '5') return

    let lat = null, lng = null
    if (record.displayLorastr && record.displayLorastr !== '-') {
      const segs = record.displayLorastr.split(/[｜|]/)
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
        '&deviceId=' + encodeURIComponent(record.upDateDevice || '') +
        '&time=' + encodeURIComponent((record.date + ' ' + record.time_part).trim()) +
        '&lorastr=' + encodeURIComponent(record.displayLorastr || '') +
        '&upDateDevice=' + encodeURIComponent(record.upDateDevice || '')
    })
  },

  // 从原始消息文本判断类型：'gps' | 'time' | 'other' | ''
  // 'other' = 除 GPS、对时外的其它所有信息
  _getMsgType(text) {
    try {
      const obj = JSON.parse(text)
      const info = obj.info || ''
      const parts = info.split('|')
      if (parts[0] === '1') return 'gps'
      if (parts[0] === '2') return 'time'
      if (parts[0]) return 'other'
    } catch (e) { /* 非JSON */ }
    // 回退：| 分隔格式，首段为类型
    const idx = text.indexOf('|')
    if (idx !== -1) {
      const typeStr = text.substring(0, idx)
      if (typeStr === '1') return 'gps'
      if (typeStr === '2') return 'time'
      if (typeStr) return 'other'
    }
    return ''
  },

  // 筛选按钮：GPS 切换
  onToggleFilterGps() {
    const next = this.data.filterType === 'gps' ? '' : 'gps'
    this.setData({ filterType: next }, () => {
      this._syncFromManager()
    })
  },
  // 筛选按钮：对时 切换
  onToggleFilterTime() {
    const next = this.data.filterType === 'time' ? '' : 'time'
    this.setData({ filterType: next }, () => {
      this._syncFromManager()
    })
  },
  // 筛选按钮：其它（除GPS、对时外的所有信息）切换
  onToggleFilterOther() {
    const next = this.data.filterType === 'other' ? '' : 'other'
    this.setData({ filterType: next }, () => {
      this._syncFromManager()
    })
  },

  // ========== 自动上传按钮（切换：开启 / 暂停，重启保留） ==========
  onToggleAutoUpload() {
    bleManager.toggleAutoUpload()
  },

  // ========== 数据同步 ==========
  syncData() {
    bleManager.toggleSync((newState) => {
      wx.showToast({ title: newState ? '同步已开始' : '同步已停止', icon: 'success' })
    })
  },

  // ========== 缓存管理 ==========
  // 双击缓存文本 → 清空缓存
  onTapCacheText() {
    const now = Date.now()
    if (now - this._lastCacheTapTime < 350) {
      // 双击触发清空
      this._lastCacheTapTime = 0
      const count = bleManager.getState().cacheCount
      if (count === 0) {
        wx.showToast({ title: '缓存已为空', icon: 'none' })
        return
      }
      wx.showModal({
        title: '清空缓存',
        content: '确定清空 ' + count + ' 条缓存数据？',
        success: (res) => {
          if (res.confirm) {
            bleManager.clearCache()
            wx.showToast({ title: '缓存已清空', icon: 'success' })
          }
        }
      })
    } else {
      this._lastCacheTapTime = now
    }
  },

  // 清空缓存（按钮）
  clearCache() {
    const count = bleManager.getState().cacheCount
    if (count === 0) {
      wx.showToast({ title: '缓存已为空', icon: 'none' })
      return
    }
    wx.showModal({
      title: '确认清空',
      content: '确定清空 ' + count + ' 条缓存数据？',
      success: (res) => {
        if (res.confirm) {
          bleManager.clearCache()
          wx.showToast({ title: '缓存已清空', icon: 'success' })
        }
      }
    })
  },

  // ========== 发送指令弹窗 ==========
  openCmdModalDirectly() {
    this._openCmdModalWithPreset('', 'custom')
  },

  // 打开指令弹窗，以缓存中所有设备作为设备列表
  _openCmdModalWithPreset(presetText, mode) {
    const that = this
    dataCache.getDeviceList((cacheData) => {
      const recordList = (cacheData && cacheData.recordList) ? cacheData.recordList : []
      // 从缓存中提取所有设备 ID（去重）
      const idSet = new Set()
      recordList.forEach(r => {
        if (r.deviceId && r.deviceId !== '-') idSet.add(r.deviceId)
      })
      let rawList = Array.from(idSet)
      // 兜底：缓存为空时用当前连接设备
      if (rawList.length === 0) {
        rawList = [this.data.connectedDeviceName || '未知设备']
      }
      bleManager.resolveDeviceDisplayNames(rawList, (displayList) => {
        that.setData({
          showCmdModal: true,
          cmdDeviceRawList: rawList,
          cmdDeviceList: displayList,
          cmdDeviceIndex: 0,
          cmdText: presetText || '',
          cmdMode: mode || 'custom'
        })
      })
    }, false)
  },

  // 同步时间 — 只选设备，确定时以即时时间为准
  onPresetSyncTime() {
    this._openCmdModalWithPreset('', 'synctime')
  },

  // 改变频率预填
  onPresetSetFreq() {
    this._openCmdModalWithPreset(JSON.stringify({ cmd: 'setfreq', value: 5 }), 'custom')
  },

  // 重启设备预填
  onPresetReboot() {
    this._openCmdModalWithPreset(JSON.stringify({ cmd: 'reboot' }), 'custom')
  },

  closeCmdModal() {
    this.setData({ showCmdModal: false })
  },

  // 快捷填充：上报GPS
  onQuickReportGps() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'setfreq', value: 5, tm: 0 }), cmdQuickSelected: 5 })
  },
  // 快捷填充：30分钟上报
  onQuickReport30Min() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'setfreq', value: 6, tm: 30 }), cmdQuickSelected: 7 })
  },
  // 快捷填充：持续跟踪
  onQuickTrack() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'setfreq', value: 5, tm: 10 }), cmdQuickSelected: 10 })
  },

  onCmdDeviceChange(e) {
    this.setData({ cmdDeviceIndex: e.detail.value })
  },

  onCmdInput(e) {
    this.setData({ cmdText: e.detail.value, cmdQuickSelected: 0 })
  },

  onCmdSend() {
    const deviceId = this.data.cmdDeviceRawList[this.data.cmdDeviceIndex]
    let cmdText = this.data.cmdText.trim()

    // 同步时间模式：以点击确定时的即时时间为准
    if (this.data.cmdMode === 'synctime') {
      cmdText = JSON.stringify({ cmd: 'synctime', time: getApp().formatTime() })
    }

    if (!cmdText) {
      wx.showToast({ title: '请输入指令内容', icon: 'none' })
      return
    }
    // 尝试解析为JSON，解析失败则作为纯文本发送
    let sendText = cmdText
    try {
      const obj = JSON.parse(cmdText)
      // 确保包含deviceId
      obj.deviceId = deviceId
      sendText = JSON.stringify(obj)
    } catch (e) {
      // 非JSON，直接发送原始文本
    }

    if (!bleManager.getState().writeDeviceInfo) {
      wx.showToast({ title: '未找到可写入特征值', icon: 'error' })
      return
    }
    bleManager.send(sendText, () => {
      wx.showToast({ title: '指令已发送 → ' + deviceId, icon: 'success' })
      console.log('BLE指令已发送:', sendText)
      this.setData({ showCmdModal: false })
    }, (err) => {
      console.error('BLE指令发送失败:', err)
      wx.showToast({ title: '发送失败', icon: 'error' })
    })
  }
})
