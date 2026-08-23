// utils/ble-manager.js - 全局蓝牙管理单例
// 核心职责：连接管理、数据接收、本地缓存、上传队列、同步开关。
// 生命周期独立于页面：离开蓝牙页后连接保持，数据照常接收/缓存/上报。
// 页面通过 subscribe/getState 订阅状态，仅负责 UI 展示与操作入口。
const STORAGE_KEY = 'bt_cache_queue'
const STORAGE_KEY_SOUND = 'setting_ble_sound'
const STORAGE_KEY_LAST_DEVICE = 'bt_last_device'
const dataCache = require('../config/data-cache.js')

const state = {
  connected: false,
  connectedDeviceName: '',
  writeDeviceInfo: null,
  isSyncing: false,
  isCenterUploading: false,
  cacheQueue: [],
  gpsQueue: [],
  uploading: false,
  uploadedCount: 0,
  receivedList: [],          // 最近200条原始消息（最新在前）
  cmdDeviceRawList: [],      // 指令弹窗设备原始列表
  cmdDeviceList: []          // 指令弹窗显示列表（含 RENAME）
}

let _inited = false
const _subscribers = []

function getState() {
  return {
    connected: state.connected,
    connectedDeviceName: state.connectedDeviceName,
    writeDeviceInfo: state.writeDeviceInfo,
    isSyncing: state.isSyncing,
    isCenterUploading: state.isCenterUploading,
    cacheQueue: state.cacheQueue,
    cacheCount: state.cacheQueue.length,
    gpsQueue: state.gpsQueue,
    uploading: state.uploading,
    uploadedCount: state.uploadedCount,
    receivedList: state.receivedList,
    cmdDeviceRawList: state.cmdDeviceRawList,
    cmdDeviceList: state.cmdDeviceList
  }
}

function emit() {
  const snapshot = getState()
  _subscribers.forEach(fn => {
    try { fn(snapshot) } catch (e) { console.error('蓝牙状态订阅回调异常:', e) }
  })
}

function subscribe(fn) {
  if (_subscribers.indexOf(fn) === -1) _subscribers.push(fn)
}

function unsubscribe(fn) {
  const i = _subscribers.indexOf(fn)
  if (i !== -1) _subscribers.splice(i, 1)
}

// ========== 本地缓存 ==========
function loadCache() {
  try {
    const saved = wx.getStorageSync(STORAGE_KEY)
    if (saved && Array.isArray(saved)) {
      state.cacheQueue = saved
    }
  } catch (e) {
    console.error('读取蓝牙缓存失败:', e)
  }
}

function saveCache() {
  try {
    wx.setStorageSync(STORAGE_KEY, state.cacheQueue)
  } catch (e) {
    console.error('保存蓝牙缓存失败:', e)
  }
}

// ========== 工具函数 ==========
// ArrayBuffer 转可读文本
function abToText(buffer) {
  if (!buffer) return ''
  try {
    const uint8 = new Uint8Array(buffer)
    let str = ''
    for (let i = 0; i < uint8.length; i++) {
      str += '%' + ('00' + uint8[i].toString(16)).slice(-2)
    }
    const text = decodeURIComponent(str)
    if (/[\x00-\x1F]/.test(text) && !/[\u4e00-\u9fa5a-zA-Z0-9]/.test(text)) {
      return 'HEX: ' + abToHex(buffer)
    }
    return text
  } catch (e) {
    return 'HEX: ' + abToHex(buffer)
  }
}

// ArrayBuffer 转 Hex
function abToHex(buffer) {
  if (!buffer) return ''
  const hexArr = Array.prototype.map.call(
    new Uint8Array(buffer),
    function (bit) { return ('00' + bit.toString(16)).slice(-2) }
  )
  return hexArr.join(' ')
}

// 文本转 ArrayBuffer
function textToAb(text) {
  if (!text) return new ArrayBuffer(0)
  const uint8 = new Uint8Array(text.length)
  for (let i = 0; i < text.length; i++) { uint8[i] = text.charCodeAt(i) }
  return uint8.buffer
}

// 播放蓝牙接收提示音（开关在设置页面控制）
function playBleSound() {
  try {
    const enabled = wx.getStorageSync(STORAGE_KEY_SOUND)
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
      let env = Math.exp(-i / (sampleRate * 0.15))
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
}

// 缓存最后连接设备信息到本地
function saveLastDevice(deviceId, deviceName) {
  try {
    wx.setStorageSync(STORAGE_KEY_LAST_DEVICE, { deviceId, deviceName, time: Date.now() })
  } catch (e) {
    console.warn('缓存蓝牙设备ID失败:', e)
  }
}

// ========== 连接管理 ==========
function connect(deviceId, deviceName, onSuccess, onFail) {
  wx.openBluetoothAdapter({
    success() {
      wx.createBLEConnection({
        deviceId,
        success() {
          state.connected = true
          state.connectedDeviceName = deviceName || '已连接设备'
          saveLastDevice(deviceId, state.connectedDeviceName)
          setupSession(deviceId)
          emit()
          if (onSuccess) onSuccess()
        },
        fail(err) {
          console.error('连接失败:', err)
          if (onFail) onFail(err)
        }
      })
    },
    fail(err) {
      console.error('蓝牙适配器启动失败:', err)
      if (onFail) onFail(err)
    }
  })
}

// 获取设备服务
function setupSession(deviceId) {
  wx.getBLEDeviceServices({
    deviceId,
    success(res) {
      console.log('设备服务列表:', res.services)
      if (res.services.length > 0) {
        setupCharacteristics(deviceId, res.services[0].uuid)
      }
    },
    fail(err) { console.error('获取服务失败:', err) }
  })
}

// 获取特征值、开启通知、记录可写特征值
function setupCharacteristics(deviceId, serviceId) {
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
        state.writeDeviceInfo = {
          deviceId, serviceId, characteristicId: writeChar.uuid
        }
        console.log('可写入特征值:', writeChar.uuid)
      }
      emit()
    },
    fail(err) { console.error('获取特征值失败:', err) }
  })
}

// 断开蓝牙（手动断开时调用；页面销毁不再自动断开）
function disconnect(onDone) {
  wx.closeBluetoothAdapter({
    success: () => {
      state.connected = false
      state.connectedDeviceName = ''
      state.writeDeviceInfo = null
      state.isSyncing = false
      state.receivedList = []
      state.gpsQueue = []
      state.uploading = false
      state.uploadedCount = 0
      state.isCenterUploading = false
      // cacheQueue/cacheCount 保留，不随断开清空
      emit()
      if (onDone) onDone()
    },
    fail: () => { if (onDone) onDone() }
  })
}

// ========== 数据接收 ==========
function prependReceived(msg) {
  state.receivedList = [msg].concat(state.receivedList)
  if (state.receivedList.length > 200) state.receivedList = state.receivedList.slice(0, 200)
}

// 全局数据接收回调（init 时注册一次，页面销毁后依然生效）
function handleBleData(res) {
  playBleSound()
  const msg = abToText(res.value)
  const rawHex = abToHex(res.value)
  console.log('═════════════════════════════════')
  console.log('[蓝牙] 原始长度:', res.value.byteLength, 'bytes')
  console.log('[蓝牙] 原始HEX:', rawHex)
  console.log('[蓝牙] 文本内容:', msg)

  // 尝试 JSON 解析，判断 cmd 类型
  let isTip = false
  let tipInfo = ''
  let isDeviceList = false
  let deviceListRaw = ''
  try {
    const obj = JSON.parse(msg)
    console.log('[蓝牙] JSON解析，cmd:', obj.cmd || '(无)', 'info:', obj.info || '(无)')
    if (obj.cmd === 'tip') {
      isTip = true
      tipInfo = obj.info || ''
    } else if (obj.cmd === 'getDeviceList') {
      isDeviceList = true
      deviceListRaw = obj.info || ''
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

  // getDeviceList 响应：更新指令弹窗的设备下拉列表
  if (isDeviceList) {
    console.log('[蓝牙] 收到 getDeviceList 响应:', deviceListRaw)
    let deviceList = []
    try {
      if (typeof deviceListRaw === 'string') {
        deviceList = JSON.parse(deviceListRaw)
      } else if (Array.isArray(deviceListRaw)) {
        deviceList = deviceListRaw
      }
    } catch (e) {
      console.warn('[蓝牙] getDeviceList info 解析失败:', e)
    }
    const rawList = deviceList.length > 0 ? deviceList : [state.connectedDeviceName || '未知设备']
    // 从云端设备缓存表取 RENAME，代替硬编码的 (RENAME) 后缀
    resolveDeviceDisplayNames(rawList, (displayList) => {
      state.cmdDeviceRawList = rawList
      state.cmdDeviceList = displayList
      emit()
    })
    return
  }

  // 预判管道类型
  const firstPipe = msg.indexOf('|')
  if (firstPipe !== -1) {
    console.log('[蓝牙] 管道分隔，首段typeId:', msg.substring(0, firstPipe))
  }
  console.log('═════════════════════════════════')

  // 时间修正：若 gpsData.time 早于 2025 年，则用设备时间偏差重新计算
  let correctedMsg = msg
  try {
    const gpsObj = JSON.parse(msg)
    if (gpsObj.time && gpsObj.now !== undefined && gpsObj.ms !== undefined) {
      const year = parseInt(String(gpsObj.time).substring(0, 4))
      if (year && year < 2025) {
        const offset = Number(gpsObj.now) - Number(gpsObj.ms)
        const refTimestamp = Date.now()
        const correctedTimestamp = refTimestamp + offset
        gpsObj.time = getApp().formatTime(new Date(correctedTimestamp))
        correctedMsg = JSON.stringify(gpsObj)
        console.log('[蓝牙] 时间修正, 原时间:', msg.substring(0, 80))
      }
    }
  } catch (e) { /* 非 JSON，保持原样 */ }

  if (state.isCenterUploading) {
    // 上传模式：直接推入上传队列，不缓存
    state.gpsQueue.push(correctedMsg)
    prependReceived(correctedMsg)
    emit()
    if (!state.uploading) {
      processGPSQueue()
    }
  } else {
    // 暂停模式：存入本地缓存
    state.cacheQueue = state.cacheQueue.concat([correctedMsg])
    prependReceived(correctedMsg)
    saveCache()
    emit()
  }
}

// ========== 上传数据中心（切换：上传 / 暂停） ==========
function toggleCenterUpload() {
  if (state.isCenterUploading) {
    // 正在上传 → 暂停
    state.isCenterUploading = false
    emit()
    wx.showToast({ title: '已暂停上传', icon: 'none' })
    return
  }

  // 开始上传
  state.isCenterUploading = true

  const cache = state.cacheQueue
  if (cache.length > 0) {
    // 有缓存：先上传缓存
    state.cacheQueue = []
    state.gpsQueue = state.gpsQueue.concat(cache)
    saveCache()
    wx.showToast({ title: '上传已开启，先上传 ' + cache.length + ' 条缓存', icon: 'none' })
  } else {
    wx.showToast({ title: '上传已开启，等待接收数据...', icon: 'none' })
  }
  emit()

  // 启动队列处理
  if (!state.uploading) {
    processGPSQueue()
  }
}

// 处理GPS上传队列
function processGPSQueue() {
  if (state.uploading) return
  const queue = state.gpsQueue
  if (queue.length === 0) {
    // 队列已空，等待新数据（上传模式下不自动停止）
    return
  }

  state.uploading = true
  const gpsDataStr = queue.shift()

  let gpsData
  try {
    gpsData = JSON.parse(gpsDataStr)
  } catch (e) {
    console.warn('GPS数据JSON解析失败，跳过:', gpsDataStr, e)
    state.uploading = false
    emit()
    processGPSQueue()
    return
  }

  const infoStr = gpsData.info
  if (!infoStr) {
    console.warn('GPS数据缺少info字段，跳过:', gpsDataStr)
    state.uploading = false
    emit()
    processGPSQueue()
    return
  }
  const parts = infoStr.split('|')
  const msgType = parts[0]

 
  if (msgType == 1 || msgType == 2 || msgType == 3 || msgType == 6) {
    const deviceId = parts[1]
    const lorastr = infoStr
    const logTime = getApp().formatTime()
    const postData = {
      time: logTime,
      action: 'insertlog',
      info: {
        deviceId: deviceId,
        lorastr: lorastr,
        upDateDevice: gpsData.upDateDevice,
        time: gpsData.time,
        rssi: String(gpsData.rssi),
        snr: String(gpsData.snr)
      }
    }
    console.log('上传设备记录, 设备编号:', deviceId, 'lora数据:', lorastr, '队列剩余:', queue.length)
    const API_URL = getApp().globalData.api_device_Url
    wx.request({
      url: API_URL,
      method: 'POST',
      data: postData,
      success: (res) => {
        console.log('GPS记录上传成功:', res.data)
        state.uploading = false
        state.uploadedCount++
        emit()
        processGPSQueue()
      },
      fail: (err) => {
        console.error('GPS记录上传失败:', err)
        state.uploading = false
        emit()
        processGPSQueue()
      }
    })
  } else {
    state.uploading = false
    emit()
    processGPSQueue()
  }
}

// ========== 同步开关 ==========
function toggleSync(onToggle) {
  if (!state.connected) {
    wx.showToast({ title: '请先连接设备', icon: 'none' })
    return
  }
  const info = state.writeDeviceInfo
  if (!info) {
    wx.showToast({ title: '未找到可写入特征值', icon: 'error' })
    return
  }

  const isSyncing = state.isSyncing
  const sendText = JSON.stringify({ syncing: !isSyncing, time: getApp().formatTime() })

  send(sendText, () => {
    state.isSyncing = !isSyncing
    emit()
    if (onToggle) onToggle(state.isSyncing)
  }, () => {
    wx.showToast({ title: '发送失败', icon: 'error' })
  })
}

// ========== 写入特征值 ==========
function send(text, onSuccess, onFail) {
  const info = state.writeDeviceInfo
  if (!info) {
    if (onFail) onFail(new Error('未找到可写入特征值'))
    return
  }
  wx.writeBLECharacteristicValue({
    deviceId: info.deviceId,
    serviceId: info.serviceId,
    characteristicId: info.characteristicId,
    value: textToAb(text),
    success: () => { if (onSuccess) onSuccess() },
    fail: (err) => {
      console.error('BLE写入失败:', err)
      if (onFail) onFail(err)
    }
  })
}

// ========== 指令弹窗设备列表（云端 RENAME） ==========
function resolveDeviceDisplayNames(rawList, callback) {
  // 优先用内存缓存，没有则拉取
  dataCache.getDeviceList((cacheData) => {
    const recordList = (cacheData && cacheData.recordList) ? cacheData.recordList : []
    // 构建 deviceId → rename 映射
    const renameMap = {}
    recordList.forEach(r => {
      if (r.deviceId && r.rename) {
        renameMap[r.deviceId] = r.rename
      }
    })
    // 生成显示列表
    const displayList = rawList.map(name => {
      const rename = renameMap[name]
      if (rename && rename !== '-' && rename !== name) {
        return name + ' (' + rename + ')'
      }
      return name
    })
    console.log('[蓝牙] 设备显示列表（含云端RENAME）:', displayList)
    callback(displayList)
  }, false) // false = 不强制刷新，优先用缓存
}

// ========== 缓存管理 ==========
function clearCache() {
  state.cacheQueue = []
  saveCache()
  emit()
}

// ========== 初始化（只执行一次） ==========
function init() {
  if (_inited) return
  _inited = true
  loadCache()
  // 全局注册数据接收回调：页面销毁后依然生效，连接保持期间持续收数
  wx.onBLECharacteristicValueChange(function (res) {
    handleBleData(res)
  })
}

module.exports = {
  init,
  getState,
  subscribe,
  unsubscribe,
  connect,
  disconnect,
  send,
  toggleSync,
  toggleCenterUpload,
  processGPSQueue,
  clearCache,
  resolveDeviceDisplayNames,
  textToAb
}
