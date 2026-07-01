// pages/dtu-device/dtu-device.js
// AMQP 模式：后端云函数通过 AMQP 订阅阿里云 IoT 设备消息 → 存入数据库
// 小程序通过 HTTP API 轮询获取设备最新数据

const app = getApp()
const API_URL = app.globalData.api_device_Url

// DTU 设备配置
const DTU_CONFIG = {
  productKey: 'iq66cDleQPs',
  deviceName: 'wifi_rola_v3_1002',
  regionId: 'cn-shanghai'
}

// LoRa 数据段含义（按 | 分隔）
const LORA_FIELDS = ['牛编号', '版本', 'GPS坐标', '参数']

// 轮询间隔 (毫秒)
const POLL_INTERVAL = 5000

Page({
  data: {
    // 设备信息
    deviceName: DTU_CONFIG.deviceName,
    productKey: DTU_CONFIG.productKey,

    // 连接状态 (polling: 轮询中, idle: 空闲, error: 异常)
    pollStatus: 'idle',
    pollStatusText: '未开始',
    pollErrorMsg: '',

    // 物模型属性
    loraInfo: '',
    loraInfoTime: '',
    loraParsed: [],
    rssi: '',
    rssiTime: '',
    snr: '',
    snrTime: '',
    upDateDevice: '',
    upDateDeviceTime: '',

    // 消息日志
    msgLog: [],

    // 统计
    totalPolls: 0,
    lastPollTime: ''
  },

  pollTimer: null,
  isPolling: false,

  onLoad() {
    console.log('[DTU设备页] 加载 (AMQP模式)')
  },

  onShow() {
    // 页面显示时开始轮询
    if (!this.isPolling) {
      this.startPolling()
    }
  },

  onHide() {
    // 页面隐藏时继续轮询（保持后台更新）
  },

  onUnload() {
    this.stopPolling()
  },

  // ========== 轮询逻辑 ==========

  startPolling() {
    if (this.isPolling) return

    this.isPolling = true
    this.setData({
      pollStatus: 'polling',
      pollStatusText: '轮询中',
      pollErrorMsg: ''
    })

    this.addLog('info', '开始轮询设备数据 (间隔' + (POLL_INTERVAL / 1000).toFixed(0) + 's)...')
    this.addLog('info', '设备: ' + DTU_CONFIG.deviceName)

    // 立即执行一次
    this.fetchDeviceData()

    // 定时轮询
    this.pollTimer = setInterval(() => {
      this.fetchDeviceData()
    }, POLL_INTERVAL)
  },

  stopPolling() {
    this.isPolling = false
    if (this.pollTimer) {
      clearInterval(this.pollTimer)
      this.pollTimer = null
    }
    this.setData({
      pollStatus: 'idle',
      pollStatusText: '已停止'
    })
  },

  // 手动刷新
  onRefresh() {
    if (!this.isPolling) {
      this.startPolling()
      return
    }
    this.addLog('info', '手动刷新...')
    this.fetchDeviceData()
  },

  // 停止轮询
  onStopPolling() {
    this.stopPolling()
    this.addLog('info', '已停止轮询')
  },

  // ========== 数据获取 ==========

  fetchDeviceData() {
    if (!this.isPolling) return

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLotRefreshAll',
        info: {
          limit: 5
        }
      },
      timeout: 5000,
      success: (res) => {
        this.processResponse(res.data)
        this.setData({
          pollStatus: 'polling',
          pollStatusText: '轮询中',
          pollErrorMsg: ''
        })
      },
      fail: (err) => {
        console.error('[DTU] 请求失败:', err)
        this.setData({
          pollStatus: 'error',
          pollStatusText: '请求失败',
          pollErrorMsg: err.errMsg || '网络错误'
        })
        this.addLog('error', '请求失败: ' + (err.errMsg || '网络错误'))
      }
    })
  },

  /**
   * 处理 API 返回的设备数据
   * 数据格式: { data: [{ attributes: [...], primaryKey: [...] }] }
   */
  processResponse(data) {
    let rawList = []
    if (data && data.data && Array.isArray(data.data)) {
      rawList = data.data
    } else if (Array.isArray(data)) {
      rawList = data
    }

    if (rawList.length === 0) {
      this.addLog('info', '暂无设备数据')
      return
    }

    // 解析所有记录
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
      return {
        deviceId: attr.deviceId || attr.deviceid || '',
        lorastr: attr.lorastr || '',
        gps: attr.gps || '',
        rssi: attr.rssi || '',
        snr: attr.snr || '',
        upDateDevice: attr.upDateDevice || '',
        time: attr.time || '',
        rawTime: attr.time || ''
      }
    })

    // 找到目标设备的最新记录
    const targetRecords = records
      .filter(r => r.deviceId === DTU_CONFIG.deviceName || r.deviceId === DTU_CONFIG.productKey + '.' + DTU_CONFIG.deviceName)
      .sort((a, b) => {
        return new Date(b.rawTime).getTime() - new Date(a.rawTime).getTime()
      })

    if (targetRecords.length === 0) {
      // 没有目标设备的数据，显示所有设备中最新的一条
      const latest = records.sort((a, b) => {
        return new Date(b.rawTime).getTime() - new Date(a.rawTime).getTime()
      })[0]
      if (latest) {
        this.addLog('info', '收到设备 [' + latest.deviceId + '] 的数据')
        this.parseAndDisplay(latest)
      }
      return
    }

    const latestRecord = targetRecords[0]
    this.addLog('msg', '收到 ' + DTU_CONFIG.deviceName + ' 的数据 (共' + targetRecords.length + '条记录)')

    // 更新统计
    const now = new Date()
    this.setData({
      totalPolls: this.data.totalPolls + 1,
      lastPollTime: this.formatTimestamp(now.getTime())
    })

    // 解析并显示
    this.parseAndDisplay(latestRecord)
  },

  /**
   * 解析记录中的 lorastr 并更新 UI
   */
  parseAndDisplay(record) {
    const updates = {}
    const timeStr = record.time || this.formatTimestamp(Date.now())

    // 解析 lorastr (格式: crow_idx|version|lat,lng|param)
    const lorastr = record.lorastr || ''
    if (lorastr) {
      // 判断是否是 JSON（物模型上报格式）
      try {
        const json = JSON.parse(lorastr)
        // 物模型格式: {"lorainfo":"...","rssi":-49,...}
        if (json.lorainfo !== undefined) {
          const raw = String(json.lorainfo)
          updates.loraInfo = raw
          updates.loraInfoTime = timeStr
          if (raw) {
            const parts = raw.split('|')
            updates.loraParsed = parts.map((val, idx) => ({
              label: LORA_FIELDS[idx] || ('参数' + (idx + 1)),
              value: val
            }))
          }
        }
        if (json.rssi !== undefined) {
          updates.rssi = String(json.rssi)
          updates.rssiTime = timeStr
        }
        if (json.snr !== undefined) {
          updates.snr = String(json.snr)
          updates.snrTime = timeStr
        }
        if (json.upDateDevice !== undefined) {
          updates.upDateDevice = String(json.upDateDevice)
          updates.upDateDeviceTime = timeStr
        }
      } catch (e) {
        // 非 JSON，按管道分隔解析
        updates.loraInfo = lorastr
        updates.loraInfoTime = timeStr
        if (lorastr) {
          const parts = lorastr.split('|')
          updates.loraParsed = parts.map((val, idx) => ({
            label: LORA_FIELDS[idx] || ('参数' + (idx + 1)),
            value: val
          }))
        }
      }
    }

    // GPS
    if (record.gps && record.gps.length > 0) {
      // 如果有独立 GPS 字段，也尝试解析
    }

    if (Object.keys(updates).length > 0) {
      this.setData(updates)
    }
  },

  // ========== 工具 ==========

  formatTimestamp(ts) {
    if (!ts) return ''
    const d = new Date(typeof ts === 'number' ? ts : Number(ts))
    if (isNaN(d.getTime())) return String(ts)
    const pad = n => String(n).padStart(2, '0')
    return d.getFullYear() + '/' + pad(d.getMonth() + 1) + '/' + pad(d.getDate()) +
      ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds()) +
      '.' + String(d.getMilliseconds()).padStart(3, '0')
  },

  addLog(type, text) {
    const now = new Date()
    const time = String(now.getHours()).padStart(2, '0') + ':' +
      String(now.getMinutes()).padStart(2, '0') + ':' +
      String(now.getSeconds()).padStart(2, '0')
    const log = { type, text, time }
    const msgLog = [log].concat(this.data.msgLog).slice(0, 100)
    this.setData({ msgLog })
  },

  // ========== 临时按钮：发送指令到 DTU ==========
  onSendCmd() {
    const FC_URL = 'https://gpsmoveinfo.cn/fc/sendtodtucmd'
    const payload = {
      action: 'com',
      deviceName: DTU_CONFIG.deviceName,
      productKey: DTU_CONFIG.productKey,
      timestamp: Date.now()
    }

    this.addLog('info', '发送指令: ' + JSON.stringify(payload))
    console.log('[DTU指令] 发送:', payload)

    wx.request({
      url: FC_URL,
      method: 'POST',
      data: payload,
      timeout: 10000,
      success: (res) => {
        const data = res.data
        console.log('[DTU指令] 返回数据:', JSON.stringify(data, null, 2))
        this.addLog('success', '指令返回: ' + JSON.stringify(data))
      },
      fail: (err) => {
        console.error('[DTU指令] 请求失败:', err)
        this.addLog('error', '指令发送失败: ' + (err.errMsg || '网络错误'))
      }
    })
  }
})
