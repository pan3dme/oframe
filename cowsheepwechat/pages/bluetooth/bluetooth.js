// bluetooth.js
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
    writeDeviceInfo: null
  },

  _lastCacheTapTime: 0,  // 双击清空缓存用
  _storageKey: 'bt_cache_queue',  // 本地存储 key

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
          const msg = that.abToText(res.value)
          console.log('收到蓝牙数据:', msg)
          const now = getApp().formatTime()
          const displayResult = that._parseDisplay(msg)
          const newItem = {
            text: msg,
            time: now.substring(11, 19),
            displayParts: displayResult
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
      return [
        { text: msg.substring(0, idx), color: '#e74c3c' },
        { text: msg.substring(idx), color: '#07c160' }
      ]
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
    parts.push({ text: 'rssi:' + rssi, color: '#333' })
    parts.push({ text: '  snr:' + snr, color: '#333' })
    parts.push({ text: '\n', color: '#333' })

    // info 各段：只有 v3-4（第2段）和末尾数字标红
    for (let i = 0; i < infoParts.length; i++) {
      const isRed = (i === 1) || (i === infoParts.length - 1)
      parts.push({ text: infoParts[i], color: isRed ? '#e74c3c' : '#333' })
      if (i < infoParts.length - 1) parts.push({ text: '|', color: '#333' })
    }

    // 设备和时间
    if (dev) {
      parts.push({ text: '  ↑' + dev, color: '#333' })
    }
    if (timeFull) {
      parts.push({ text: '\n' + timeFull, color: '#333' })
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
    if (!this.data.bluetoothConnected) {
      wx.showToast({ title: '请先连接蓝牙设备', icon: 'none' })
      return
    }
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
    if (msgType == 1) {
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

  // ========== 页面生命周期 ==========
  onUnload() {
    this.disconnectBluetooth()
  }
})
