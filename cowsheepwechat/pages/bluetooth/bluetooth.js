// bluetooth.js
const STORAGE_KEY_BLE_SOUND = 'setting_ble_sound'
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    bluetoothConnected: false,
    bluetoothScanning: false,
    isSyncing: false,
    connectedDeviceName: '',
    devices: [],
    // 接收到的数据列表
    receivedList: [],
    // 数据缓存（先存后上传）
    cacheQueue: [],
    cacheCount: 0,
    // GPS 数据上传队列
    gpsQueue: [],
    uploading: false,
    uploadedCount: 0,
    isCenterUploading: false, // 数据中心上传状态
    // 写入特征值信息（连接成功后缓存）
    writeDeviceInfo: null,
    // 发送指令弹窗
    showCmdPanel: false,
    showCmdModal: false,
    cmdMode: '',
    cmdDeviceList: [],
    cmdDeviceIndex: 0,
    cmdText: ''
  },

  _lastCacheTapTime: 0,  // 双击清空缓存用
  _storageKey: 'bt_cache_queue',  // 本地存储 key

  // 生成随机浅色背景色（HSL 浅色调，饱和度低，亮度高）
  _randomPastel() {
    const h = Math.floor(Math.random() * 360)           // 色相随机
    const s = 30 + Math.floor(Math.random() * 20)       // 饱和度 30-50%（低饱和=柔和）
    const l = 88 + Math.floor(Math.random() * 8)        // 亮度 88-96%（很高=浅色）
    return `hsl(${h}, ${s}%, ${l}%)`
  },

  // 播放蓝牙接收提示音（开关在设置页面控制）
  // 类似系统通知音的单音"叮"声，约300ms，指数衰减更自然
  _playBleSound() {
    try {
      const enabled = wx.getStorageSync(STORAGE_KEY_BLE_SOUND)
      if (enabled === false || enabled === 'false') return
    } catch (e) { /* 读取失败则播放（默认开启） */ }

    try {
      const sampleRate = 8000
      const freq = 1200                     // 清脆的中高频
      const duration = 0.28                 // 280ms
      const numSamples = Math.floor(sampleRate * duration)
      const dataLen = numSamples
      const fileLen = 44 + dataLen
      const buf = new ArrayBuffer(fileLen)
      const v = new DataView(buf)

      const ws = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)) }
      ws(0, 'RIFF'); v.setUint32(4, fileLen - 8, true); ws(8, 'WAVE')
      ws(12, 'fmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true)
      v.setUint16(22, 1, true); v.setUint32(24, sampleRate, true)
      v.setUint32(28, sampleRate, true); v.setUint16(32, 1, true); v.setUint16(34, 8, true)
      ws(36, 'data'); v.setUint32(40, dataLen, true)

      const attackSamples = Math.floor(sampleRate * 0.01)    // 10ms 快速起音
      for (let i = 0; i < numSamples; i++) {
        // 指数衰减包络，模拟自然铃声
        let env = Math.exp(-i / (sampleRate * 0.15))
        // 快速起音
        if (i < attackSamples) env *= i / attackSamples
        v.setUint8(44 + i, 128 + Math.floor(90 * env * Math.sin(2 * Math.PI * freq * i / sampleRate)))
      }

      const fs = wx.getFileSystemManager()
      const tmpPath = wx.env.USER_DATA_PATH + '/ble_chime.wav'
      fs.writeFile({
        filePath: tmpPath,
        data: buf,
        success: () => {
          const audio = wx.createInnerAudioContext()
          audio.src = tmpPath
          audio.volume = 0.7
          audio.play()
          audio.onEnded(() => audio.destroy())
          audio.onError(() => audio.destroy())
        },
        fail: () => {}
      })
    } catch (e) { /* 播放失败不阻塞 */ }
  },

  // ========== 蓝牙连接 ==========
  onLoad() {
    // 恢复本地缓存的蓝牙数据
    this._loadCacheFromStorage()
    // 开发工具不支持蓝牙，跳过自动连接避免 timeout
    const sysInfo = wx.getSystemInfoSync()
    if (sysInfo.platform === 'devtools') {
      console.log('开发工具环境，跳过蓝牙自动连接')
      return
    }
    // 打开小程序自动连接蓝牙
    this.connectBluetooth()
  },

  // 从本地存储加载缓存
  _loadCacheFromStorage() {
    try {
      const saved = wx.getStorageSync(this._storageKey)
      if (saved && Array.isArray(saved)) {
        this.setData({ cacheQueue: saved, cacheCount: saved.length })
      }
    } catch (e) {
      console.error('读取缓存失败:', e)
    }
  },

  // 保存缓存到本地存储
  _saveCacheToStorage() {
    try {
      wx.setStorageSync(this._storageKey, this.data.cacheQueue)
    } catch (e) {
      console.error('保存缓存失败:', e)
    }
  },

  connectBluetooth() {
    const that = this
    // 如果已连接，先断开再重新连接
    if (this.data.bluetoothConnected) {
      this.disconnectBluetooth()
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
    wx.onBluetoothDeviceFound(function (res) {
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
    })

    // 开始搜索
    wx.startBluetoothDevicesDiscovery({
      allowDuplicatesKey: false,
      success() {
        wx.showToast({ title: '正在扫描设备...', icon: 'none', duration: 1000 })
        setTimeout(() => { that.stopScanDevices() }, 10000)
      },
      fail() {
        that.setData({ bluetoothScanning: false })
        wx.showToast({ title: '扫描启动失败', icon: 'error' })
      }
    })
  },

  // 停止扫描
  stopScanDevices() {
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

    wx.createBLEConnection({
      deviceId: deviceid,
      success() {
        wx.hideLoading()
        that.setData({
          bluetoothConnected: true,
          connectedDeviceName: devicename,
          devices: []
        })
        wx.showToast({ title: '蓝牙已连接', icon: 'success' })
        that.getBLEDeviceServices(deviceid)
      },
      fail(err) {
        wx.hideLoading()
        console.error('连接失败:', err)
        wx.showToast({ title: '设备连接失败', icon: 'error' })
      }
    })
  },

  // 获取设备服务
  getBLEDeviceServices(deviceId) {
    const that = this
    wx.getBLEDeviceServices({
      deviceId,
      success(res) {
        console.log('设备服务列表:', res.services)
        if (res.services.length > 0) {
          that.getBLEDeviceCharacteristics(deviceId, res.services[0].uuid)
        }
      },
      fail(err) { console.error('获取服务失败:', err) }
    })
  },

  // 获取特征值
  getBLEDeviceCharacteristics(deviceId, serviceId) {
    const that = this
    wx.getBLEDeviceCharacteristics({
      deviceId,
      serviceId,
      success(res) {
        console.log('特征值列表:', res.characteristics)
        let writeChar = null
        for (let i = 0; i < res.characteristics.length; i++) {
          const char = res.characteristics[i]
          if (char.properties.notify || char.properties.indicate) {
            wx.notifyBLECharacteristicValueChange({
              deviceId,
              serviceId,
              characteristicId: char.uuid,
              state: true,
              success() { console.log('已启用数据通知:', char.uuid) }
            })
          }
          if (char.properties.write || char.properties.writeNoResponse) {
            if (!writeChar) writeChar = char
          }
        }
        if (writeChar) {
          that.data.writeDeviceInfo = {
            deviceId, serviceId, characteristicId: writeChar.uuid
          }
          console.log('可写入特征值:', writeChar.uuid)
        }
        // 监听数据变化
        wx.onBLECharacteristicValueChange(function (res) {
          that._playBleSound()  // 播放接收提示音
          const msg = that.abToText(res.value)
          // 打印原始数据，方便对消息分类
          const rawHex = that.abToHex(res.value)
          console.log('═════════════════════════════════')
          console.log('[蓝牙] 原始长度:', res.value.byteLength, 'bytes')
          console.log('[蓝牙] 原始HEX:', rawHex)
          console.log('[蓝牙] 文本内容:', msg)

          // 尝试 JSON 解析，判断是否 tip 指令
          let isTip = false
          let tipInfo = ''
          try {
            const obj = JSON.parse(msg)
            console.log('[蓝牙] JSON解析，cmd:', obj.cmd || '(无)', 'info:', obj.info || '(无)')
            if (obj.cmd === 'tip') {
              isTip = true
              tipInfo = obj.info || ''
            }
          } catch (e) { /* 非JSON，非管道 */ }

          // tip 消息：弹框提示，不缓存
          if (isTip) {
            console.log('[蓝牙] ⚠️ 收到tip消息，弹框提示，不缓存')
            wx.showModal({
              title: '设备提示',
              content: tipInfo,
              showCancel: false,
              confirmText: '知道了'
            })
            return
          }

          // 预判管道类型
          const firstPipe = msg.indexOf('|')
          if (firstPipe !== -1) {
            const typeId = msg.substring(0, firstPipe)
            console.log('[蓝牙] 管道分隔，首段typeId:', typeId)
          }
          console.log('═════════════════════════════════')
          const now = getApp().formatTime()
          const displayResult = that._parseDisplay(msg)
          const newItem = {
            text: msg,
            time: now.substring(11, 19),
            displayParts: displayResult,
            bgColor: that._randomPastel()  // 随机浅色背景
          }
          const newCache = that.data.cacheQueue.concat([msg])
          // 最新数据插到最前面，方便直接看到
          const newList = [newItem].concat(that.data.receivedList)
          const trimmedList = newList.length > 200 ? newList.slice(0, 200) : newList
          that.setData({
            cacheQueue: newCache,
            cacheCount: newCache.length,
            receivedList: trimmedList
          })
          that._saveCacheToStorage()
        })
      },
      fail(err) { console.error('获取特征值失败:', err) }
    })
  },

  // 根据类型编号返回图标
  _getTypeIcon(typeStr) {
    if (typeStr === '1') return { text: '◉', color: '#1989fa' }   // 靶心图标 = GPS定位
    if (typeStr === '2') return { text: '🕐', color: '#666' }       // 时钟 = 对时
    return null
  },

  // 解析显示格式：优先 JSON，回退 | 分隔
  _parseDisplay(msg) {
    // 尝试 JSON 解析
    try {
      const obj = JSON.parse(msg)
      if (obj && typeof obj === 'object') {
        return this._parseJsonDisplay(obj)
      }
    } catch (e) { /* 非 JSON，继续尝试 | 格式 */ }

    // 回退：| 分隔格式
    const idx = msg.indexOf('|')
    if (idx !== -1) {
      const typeStr = msg.substring(0, idx)
      const icon = this._getTypeIcon(typeStr)
      const parts = [
        { text: msg.substring(0, idx), color: '#e74c3c', bold: true },
        { text: msg.substring(idx), color: '#07c160', bold: true }
      ]
      if (icon) parts.unshift({ text: icon.text + ' ', color: icon.color })
      return parts
    }
    // 纯文本
    return [{ text: msg, color: '#333' }]
  },

  // 解析 JSON 显示
  _parseJsonDisplay(obj) {
    const parts = []
    const info = obj.info || ''
    const infoParts = info.split('|')
    const dev = obj.upDateDevice || ''
    const timeFull = obj.time || ''
    const rssi = obj.rssi !== undefined ? obj.rssi : ''
    const snr = obj.snr !== undefined ? obj.snr : ''

    // 元数据行
    parts.push({ text: 'rssi:', color: '#999' })
    parts.push({ text: rssi, color: '#e74c3c', bold: true })
    parts.push({ text: '  snr:', color: '#999' })
    parts.push({ text: snr, color: '#07c160', bold: true })
    parts.push({ text: '\n', color: '#333' })

    // 类型图标（infoParts[0] 为类型编号：1=GPS定位，2=对时）
    const typeIcon = this._getTypeIcon(infoParts[0])
    if (typeIcon) parts.push({ text: typeIcon.text + ' ', color: typeIcon.color })

    // info 各段：类型编号、设备ID、末尾数字标红
    for (let i = 0; i < infoParts.length; i++) {
      const isRed = (i === 0) || (i === 1) || (i === infoParts.length - 1)
      parts.push({ text: infoParts[i], color: isRed ? '#e74c3c' : '#333', bold: isRed })
      if (i < infoParts.length - 1) parts.push({ text: '|', color: '#333' })
    }

    // 时间 + 设备名称（第三行）
    const thirdLine = []
    if (timeFull) thirdLine.push({ text: timeFull, color: '#333' })
    if (timeFull && dev) thirdLine.push({ text: ' ', color: '#333' })
    if (dev) thirdLine.push({ text: '(' + dev + ')', color: '#333' })
    if (thirdLine.length > 0) {
      parts.push({ text: '\n', color: '#333' })
      parts.push(...thirdLine)
    }

    return parts
  },

  // ArrayBuffer 转可读文本
  abToText(buffer) {
    if (!buffer) return ''
    try {
      const uint8 = new Uint8Array(buffer)
      let str = ''
      for (let i = 0; i < uint8.length; i++) {
        str += '%' + ('00' + uint8[i].toString(16)).slice(-2)
      }
      const text = decodeURIComponent(str)
      if (/[\x00-\x1F]/.test(text) && !/[\u4e00-\u9fa5a-zA-Z0-9]/.test(text)) {
        return 'HEX: ' + this.abToHex(buffer)
      }
      return text
    } catch (e) {
      return 'HEX: ' + this.abToHex(buffer)
    }
  },

  // ArrayBuffer 转 Hex
  abToHex(buffer) {
    if (!buffer) return ''
    const hexArr = Array.prototype.map.call(
      new Uint8Array(buffer),
      function (bit) { return ('00' + bit.toString(16)).slice(-2) }
    )
    return hexArr.join(' ')
  },

  // 文本转 ArrayBuffer
  textToAb(text) {
    if (!text) return new ArrayBuffer(0)
    const uint8 = new Uint8Array(text.length)
    for (let i = 0; i < text.length; i++) { uint8[i] = text.charCodeAt(i) }
    return uint8.buffer
  },

  // 断开蓝牙
  disconnectBluetooth() {
    wx.closeBluetoothAdapter({
      success: () => {
        this.setData({
          bluetoothConnected: false,
          connectedDeviceName: '',
          receivedList: [],
          writeDeviceInfo: null,
          isSyncing: false,
          // cacheQueue/cacheCount 保留，不随断开清空
          gpsQueue: [],
          uploading: false,
          uploadedCount: 0,
          isCenterUploading: false
        })
      }
    })
  },

  // 双击缓存文本 → 清空缓存
  onTapCacheText() {
    const now = Date.now()
    if (now - this._lastCacheTapTime < 350) {
      // 双击触发清空
      this._lastCacheTapTime = 0
      if (this.data.cacheCount === 0) {
        wx.showToast({ title: '缓存已为空', icon: 'none' })
        return
      }
      wx.showModal({
        title: '清空缓存',
        content: '确定清空 ' + this.data.cacheCount + ' 条缓存数据？',
        success: (res) => {
          if (res.confirm) {
            this.setData({ cacheQueue: [], cacheCount: 0 })
            wx.setStorageSync(this._storageKey, [])
            wx.showToast({ title: '缓存已清空', icon: 'success' })
          }
        }
      })
    } else {
      this._lastCacheTapTime = now
    }
  },

  // ========== 上传数据中心（切换） ==========
  uploadToCenter() {
    if (this.data.isCenterUploading) {
      // 正在上传 → 取消
      this.cancelCenterUpload()
      return
    }
    const cacheLen = this.data.cacheQueue.length
    if (cacheLen === 0) {
      wx.showToast({ title: '暂无缓存数据', icon: 'none' })
      return
    }

    // 提取所有记录中的设备ID并校验
    this._validateDevices(this.data.cacheQueue, (unknownDevices) => {
      if (unknownDevices.length > 0) {
        wx.showModal({
          title: '设备未注册',
          content: '以下设备未在系统中找到：\n' + unknownDevices.join('、') + '\n\n是否继续上传？',
          confirmText: '继续上传',
          cancelText: '取消',
          success: (res) => {
            if (res.confirm) {
              this._doUploadToCenter()
            }
          }
        })
      } else {
        this._doUploadToCenter()
      }
    })
  },

  // 提取缓存数据中的deviceId并对比设备缓存
  _validateDevices(cacheQueue, callback) {
    // 先从缓存数据中提取所有deviceId
    const deviceIds = new Set()
    cacheQueue.forEach(item => {
      try {
        const obj = JSON.parse(item)
        const info = obj.info || ''
        const parts = info.split('|')
        if (parts.length >= 2 && parts[0] === '1') {
          const deviceId = parts[1].trim()
          if (deviceId) deviceIds.add(deviceId)
        }
      } catch (e) {
        // 非JSON数据，跳过
      }
    })

    if (deviceIds.size === 0) {
      // 没有提取到设备ID，直接放行
      callback([])
      return
    }

    const idList = Array.from(deviceIds)
    // 检查设备缓存
    dataCache.getDeviceList((deviceData) => {
      const knownSet = new Set()
      if (deviceData && deviceData.recordList) {
        deviceData.recordList.forEach(r => {
          if (r.deviceId && r.deviceId !== '-') knownSet.add(r.deviceId)
        })
      }
      const unknown = idList.filter(id => !knownSet.has(id))
      callback(unknown)
    })
  },

  // 实际执行上传
  _doUploadToCenter() {
    // 将缓存数据全部移入上传队列
    const allData = [...this.data.cacheQueue]
    this.setData({
      cacheQueue: [],
      cacheCount: 0,
      gpsQueue: this.data.gpsQueue.concat(allData),
      isCenterUploading: true
    })
    wx.setStorageSync(this._storageKey, [])
    // 如果未在上传中，开始处理
    if (!this.data.uploading) {
      this.processGPSQueue()
    }
  },

  // 取消数据中心上传
  cancelCenterUpload() {
    wx.showModal({
      title: '取消上传',
      content: '确定取消上传？队列中剩余 ' + this.data.gpsQueue.length + ' 条数据将丢失。',
      success: (res) => {
        if (res.confirm) {
          this.setData({
            gpsQueue: [],
            uploading: false,
            isCenterUploading: false
          })
          wx.showToast({ title: '上传已取消', icon: 'none' })
        }
      }
    })
  },

  // 清空缓存
  clearCache() {
    if (this.data.cacheCount === 0) {
      wx.showToast({ title: '缓存已为空', icon: 'none' })
      return
    }
    wx.showModal({
      title: '确认清空',
      content: '确定清空 ' + this.data.cacheCount + ' 条缓存数据？',
      success: (res) => {
        if (res.confirm) {
          this.setData({ cacheQueue: [], cacheCount: 0 })
          wx.showToast({ title: '缓存已清空', icon: 'success' })
        }
      }
    })
  },

  // 处理GPS上传队列
  processGPSQueue() {
    if (this.data.uploading) return
    const queue = this.data.gpsQueue
    if (queue.length === 0) {
      // 队列已空，结束上传状态
      if (this.data.isCenterUploading) {
        this.setData({ isCenterUploading: false })
        wx.showToast({ title: '全部上传完成', icon: 'success' })
      }
      return
    }

    this.data.uploading = true
    const gpsDataStr = queue.shift()
    this.setData({ gpsQueue: queue })

    let gpsData
    try {
      gpsData = JSON.parse(gpsDataStr)
    } catch (e) {
      console.warn('GPS数据JSON解析失败，跳过:', gpsDataStr, e)
      this.data.uploading = false
      this.processGPSQueue()
      return
    }

    const infoStr = gpsData.info
    if (!infoStr) {
      console.warn('GPS数据缺少info字段，跳过:', gpsDataStr)
      this.data.uploading = false
      this.processGPSQueue()
      return
    }
    const parts = infoStr.split('|')
    const msgType = parts[0]

     // typedef enum {
    //   MSG_TYPE_GPS = 1,        // GPS定位信息
    //   MSG_TYPE_TIME = 2,       // 对时信息
    //   MSG_TYPE_BATTERY = 3,    // 电量信息（小数，如0.5、0.1）
    //   MSG_TYPE_FIRMWARE = 10,   // 固件更新指令
    //   MSG_TYPE_COM = 11   // 下载指令到设备
    // } MessageType_t;
    if (msgType == 1||msgType == 3) {
      const deviceId = parts[1]
      const lorastr = infoStr
      const logTime = getApp().formatTime()
      const postData = {
        time: logTime,
        action: "insertlog",
        info: {
          deviceId: deviceId,
          lorastr: lorastr,
          upDateDevice: gpsData.upDateDevice,
          time: gpsData.time
        }
      }
      console.log('上传设备记录, 设备编号:', deviceId, 'lora数据:', lorastr, '队列剩余:', queue.length)
      const API_URL = getApp().globalData.api_device_Url
      const that = this
      wx.request({
        url: API_URL,
        method: 'POST',
        data: postData,
        success: (res) => {
          console.log('GPS记录上传成功:', res.data)
          that.data.uploading = false
          that.data.uploadedCount++
          that.setData({
            uploadedCount: that.data.uploadedCount,
            gpsQueue: that.data.gpsQueue
          })
          that.processGPSQueue()
        },
        fail: (err) => {
          console.error('GPS记录上传失败:', err)
          that.data.uploading = false
          that.setData({ gpsQueue: that.data.gpsQueue })
          that.processGPSQueue()
        }
      })
    } else {
      this.data.uploading = false
      this.processGPSQueue()
    }
  },

  // ========== 数据同步 ==========
  syncData() {
    if (!this.data.bluetoothConnected) {
      wx.showToast({ title: '请先连接设备', icon: 'none' })
      return
    }
    const info = this.data.writeDeviceInfo
    if (!info) {
      wx.showToast({ title: '未找到可写入特征值', icon: 'error' })
      return
    }

    const isSyncing = this.data.isSyncing
    let sendText = ''
    if (!isSyncing) {
      sendText = JSON.stringify({ syncing: true, time: getApp().formatTime() })
    } else {
      sendText = JSON.stringify({ syncing: false, time: getApp().formatTime() })
    }

    const buffer = this.textToAb(sendText)
    const that = this

    wx.writeBLECharacteristicValue({
      deviceId: info.deviceId,
      serviceId: info.serviceId,
      characteristicId: info.characteristicId,
      value: buffer,
      success: () => {
        const newState = !isSyncing
        that.setData({ isSyncing: newState })
        wx.showToast({ title: newState ? '同步已开始' : '同步已停止', icon: 'success' })
        console.log('已发送[' + (newState ? '开始' : '停止') + '同步]:', sendText)
      },
      fail(err) {
        console.error('发送失败:', err)
        wx.showToast({ title: '发送失败', icon: 'error' })
      }
    })
  },

  // ========== 发送指令面板 + 弹窗 ==========
  toggleCmdPanel() {
    this.setData({ showCmdPanel: !this.data.showCmdPanel })
  },

  // 打开指令弹窗，可选预填指令文本
  _openCmdModalWithPreset(presetText, mode) {
    dataCache.getDeviceList((deviceData) => {
      const list = []
      if (deviceData && deviceData.recordList) {
        deviceData.recordList.forEach(r => {
          if (r.deviceId && r.deviceId !== '-') list.push(r.deviceId)
        })
      }
      this.setData({
        showCmdModal: true,
        cmdDeviceList: list.length > 0 ? list : [this.data.connectedDeviceName || '未知设备'],
        cmdDeviceIndex: 0,
        cmdText: presetText || '',
        cmdMode: mode || 'custom'
      })
    })
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

  // 快捷填充：高频 value=1
  onQuickFreq1() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'setfreq', value: 1 }) })
  },

  // 快捷填充：获取GPS value=2
  onQuickGps() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'setfreq', value: 2 }) })
  },

  // 快捷填充：获取电量 value=3
  onQuickBattery() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'setfreq', value: 3 }) })
  },

  onCmdDeviceChange(e) {
    this.setData({ cmdDeviceIndex: e.detail.value })
  },

  onCmdInput(e) {
    this.setData({ cmdText: e.detail.value })
  },

  onCmdSend() {
    const deviceId = this.data.cmdDeviceList[this.data.cmdDeviceIndex]
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

    const info = this.data.writeDeviceInfo
    if (!info) {
      wx.showToast({ title: '未找到可写入特征值', icon: 'error' })
      return
    }
    const buffer = this.textToAb(sendText)
    const that = this
    wx.writeBLECharacteristicValue({
      deviceId: info.deviceId,
      serviceId: info.serviceId,
      characteristicId: info.characteristicId,
      value: buffer,
      success: () => {
        wx.showToast({ title: '指令已发送 → ' + deviceId, icon: 'success' })
        console.log('BLE指令已发送:', sendText)
        that.setData({ showCmdModal: false })
      },
      fail(err) {
        console.error('BLE指令发送失败:', err)
        wx.showToast({ title: '发送失败', icon: 'error' })
      }
    })
  },

  // ========== 页面生命周期 ==========
  onUnload() {
    this.disconnectBluetooth()
  }
})
