// bluetooth.js
Page({
  data: {
    bluetoothConnected: false,
    bluetoothScanning: false,
    isSyncing: false,
    connectedDeviceName: '',
    devices: [],
    receivedMsg: '',
    // GPS 数据上传队列
    gpsQueue: [],
    uploading: false,
    uploadedCount: 0,
    // 写入特征值信息（连接成功后缓存）
    writeDeviceInfo: null
  },

  // ========== 蓝牙连接 ==========
  onLoad() {
    // 开发工具不支持蓝牙，跳过自动连接避免 timeout
    const sysInfo = wx.getSystemInfoSync()
    if (sysInfo.platform === 'devtools') {
      console.log('开发工具环境，跳过蓝牙自动连接')
      return
    }
    // 打开小程序自动连接蓝牙
    this.connectBluetooth()
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
          that.setData({ receivedMsg: msg })
          console.log('收到蓝牙数据:', msg)
          that.data.gpsQueue.push(msg)
          that.processGPSQueue()
        })
      },
      fail(err) { console.error('获取特征值失败:', err) }
    })
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
          receivedMsg: '',
          writeDeviceInfo: null,
          isSyncing: false,
          gpsQueue: [],
          uploading: false,
          uploadedCount: 0
        })
      }
    })
  },

  // 处理GPS上传队列
  processGPSQueue() {
    if (this.data.uploading) return
    const queue = this.data.gpsQueue
    if (queue.length === 0) return

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
