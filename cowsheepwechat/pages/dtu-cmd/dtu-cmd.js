// pages/dtu-cmd/dtu-cmd.js
// DTU发送指令页面：选择设备 → 输入/快捷指令 → HTTP发送到云函数
const dataCache = require('../../config/data-cache.js')
const timeWindowCodec = require('../../utils/time-window-codec.js')

// DTU 指令转发云函数地址
const FC_URL = 'https://gpsmoveinfo.cn/fc/sendtodtucmd'
// 设备日志查询接口（用于查找上传设备以获取密钥）
const API_URL = getApp().globalData.api_device_Url

Page({
  data: {
    // 目标设备列表
    deviceList: [],         // 完整设备记录 [{deviceId, ProductKey, DeviceName, rename}, ...]
    deviceDisplayList: [],  // 下拉显示用 ["deviceId (rename)", ...]
    deviceIndex: 0,

    // 中继转发设备列表（手动选择用哪个设备来转发）
    relayDisplayList: ['自动'],   // 下拉显示用 ["自动", "deviceId (rename)", ...]
    relayDeviceList: [null],      // 对应设备对象，null=自动模式
    relayIndex: 0,                // 默认"自动"

    // 指令内容
    cmdText: '',
    quickSelected: 0,  // 当前选中快捷按钮
    sendmodeValue: 0,  // 上报模式当前值（0/1 循环切换）

    // 发送日志
    sendLog: [],

    // ===== 配置下发弹框 =====
    showConfigModal: false,
    hourRange: (() => { const arr = []; for (let i = 0; i <= 24; i++) arr.push(i); return arr })(),      // 结束时间可选 0-24
    startHourRange: (() => { const arr = []; for (let i = 0; i <= 23; i++) arr.push(i); return arr })(),  // 开始时间可选 0-23
    configPeriod: '10',          // 上报周期（5-60）
    configWorkStart: 0,          // 开机时间开始（小时索引）
    configWorkEnd: 24,           // 开机时间结束（24=23:59）
    configGpsStart: 12,          // GPS工作时间开始
    configGpsEnd: 24             // GPS工作时间结束（24=23:59）
  },

  onLoad(options) {
    const preselectDeviceId = options.deviceId || ''
    // 从缓存获取设备列表
    dataCache.getDeviceList((deviceData) => {
      if (deviceData && deviceData.recordList && deviceData.recordList.length > 0) {
        // 过滤出有有效 deviceId 的设备（显示所有设备）
        const validDevices = deviceData.recordList.filter(r =>
          r.deviceId && r.deviceId !== '-'
        )

        // 去重（同一 deviceId 只保留一条）
        const seen = new Set()
        const uniqueDevices = validDevices.filter(r => {
          if (seen.has(r.deviceId)) return false
          seen.add(r.deviceId)
          return true
        })

        if (uniqueDevices.length > 0) {
          // 生成显示列表：标注是否有 DTU 密钥
          const displayList = uniqueDevices.map(r => {
            const hasKey = r.ProductKey && r.DeviceName
            let label = r.rename && r.rename !== '-' && r.rename !== r.deviceId
              ? r.deviceId + ' (' + r.rename + ')'
              : r.deviceId
            if (!hasKey) label += ' [无密钥]'
            return label
          })

          // 如果传入了 deviceId，自动选中对应设备
          let selectedIndex = 0
          if (preselectDeviceId) {
            const foundIdx = uniqueDevices.findIndex(d => d.deviceId === preselectDeviceId)
            if (foundIdx >= 0) selectedIndex = foundIdx
          }

          // 构建中继转发设备列表：只列出有密钥的设备
          const keyDevices = uniqueDevices.filter(d => d.ProductKey && d.DeviceName)
          const relayDisplay = ['自动'].concat(keyDevices.map(d => {
            return d.rename && d.rename !== '-' && d.rename !== d.deviceId
              ? d.deviceId + ' (' + d.rename + ')'
              : d.deviceId
          }))
          const relayDevices = [null].concat(keyDevices)

          this.setData({
            deviceList: uniqueDevices,
            deviceDisplayList: displayList,
            deviceIndex: selectedIndex,
            relayDisplayList: relayDisplay,
            relayDeviceList: relayDevices
          })
        } else {
          this.setData({
            deviceDisplayList: ['暂无可用设备'],
            deviceIndex: 0
          })
        }
      } else {
        this.setData({
          deviceDisplayList: ['暂无可用设备'],
          deviceIndex: 0
        })
      }
    })
  },

  // ========== 设备选择 ==========
  onDeviceChange(e) {
    this.setData({ deviceIndex: parseInt(e.detail.value) })
  },

  // 中继转发设备选择
  onRelayChange(e) {
    this.setData({ relayIndex: parseInt(e.detail.value) })
  },

  // ========== 指令输入 ==========
  onCmdInput(e) {
    this.setData({ cmdText: e.detail.value, quickSelected: 0 })
  },

  // ========== 快捷按钮 ==========
  onQuickReportGps() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'upgps', value: 0 }), quickSelected: 5 })
  },

  onQuickNormalMode() {
    const newVal = this.data.sendmodeValue === 0 ? 1 : 0
    this.setData({
      cmdText: JSON.stringify({ cmd: 'sendmode', value: newVal }),
      quickSelected: 12,
      sendmodeValue: newVal
    })
  },

  onQuickSyncTime() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'B', value: '30,5' }), quickSelected: 15 })
  },

  onQuickTxPower() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'txpower', value: 20 }), quickSelected: 16 })
  },
  onQuickMinBattery() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'minbattery', value: 50 }), quickSelected: 17 })
  },
  onQuickConfig() {
    const device = this.data.deviceList[this.data.deviceIndex]
    if (!device) {
      wx.showToast({ title: '请先选择目标设备', icon: 'none' })
      return
    }

    this.setData({ quickSelected: 18 })
    // 打开配置下发弹框（先用默认值）
    this._openConfigModal(null)

    // 异步查询设备已有配置，用于预填弹框
    const that = this
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceConfigAll',
        info: { deviceId: device.deviceId }
      },
      timeout: 8000,
      success: (res) => {
        console.log('[DTU指令] getDeviceConfigAll 返回:', JSON.stringify(res.data))
        if (!that.data.showConfigModal) return // 弹框已被关闭则忽略

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
            return devId === device.deviceId
          })

          if (record) {
            const attr = {}
            if (record.attributes) {
              record.attributes.forEach(item => { attr[item.columnName] = item.columnValue })
            }
            if (record.primaryKey) {
              record.primaryKey.forEach(item => { attr[item.name] = item.value })
            }
            if (record.lorastr) attr.lorastr = record.lorastr

            const configLorastr = attr.lorastr || ''
            const defaults = that._parseLoraConfigToModal(configLorastr)
            if (defaults) {
              that._openConfigModal(defaults)
              that.addLog('info', '已加载设备配置预填弹框: ' + configLorastr)
            }
          }
        }
      },
      fail: (err) => {
        console.error('[DTU指令] getDeviceConfigAll 失败:', err)
      }
    })
  },

  // 打开配置下发弹框，defaults 为空时使用默认值 10,0-24,12-24
  _openConfigModal(defaults) {
    const d = defaults || {}
    this.setData({
      showConfigModal: true,
      configPeriod: d.configPeriod !== undefined ? d.configPeriod : '10',
      configWorkStart: d.configWorkStart !== undefined ? d.configWorkStart : 0,
      configWorkEnd: d.configWorkEnd !== undefined ? d.configWorkEnd : 24,
      configGpsStart: d.configGpsStart !== undefined ? d.configGpsStart : 12,
      configGpsEnd: d.configGpsEnd !== undefined ? d.configGpsEnd : 24
    })
  },

  // 解析设备配置 lorastr 第3段: 上报周期,开机时间,GPS工作时间 → 弹框默认值
  // 兼容两位代号（新格式）与 start-duration（旧格式）；无法解析返回 null
  _parseLoraConfigToModal(lorastr) {
    const parts = String(lorastr || '').split('|')
    if (parts.length < 3 || !parts[2]) return null
    const segs = parts[2].split(',')
    if (segs.length < 3) return null

    const period = parseInt(segs[0], 10)
    const workWin = timeWindowCodec.parseTimeWindow(segs[1])
    const gpsWin = timeWindowCodec.parseTimeWindow(segs[2])
    if (!workWin || !gpsWin) return null

    // end=23 代表 23:59，弹框结束时间回填为 24（对应"当天最后一刻"）
    return {
      configPeriod: String(isNaN(period) || period < 5 ? 10 : (period > 60 ? 60 : period)),
      configWorkStart: workWin.start,
      configWorkEnd: workWin.end === 23 ? 24 : workWin.end,
      configGpsStart: gpsWin.start,
      configGpsEnd: gpsWin.end === 23 ? 24 : gpsWin.end
    }
  },

  // ===== 配置下发弹框交互 =====
  onConfigPeriodInput(e) {
    this.setData({ configPeriod: e.detail.value })
  },

  onConfigWorkStartChange(e) {
    this.setData({ configWorkStart: parseInt(e.detail.value, 10) })
  },

  onConfigWorkEndChange(e) {
    this.setData({ configWorkEnd: parseInt(e.detail.value, 10) })
  },

  onConfigGpsStartChange(e) {
    this.setData({ configGpsStart: parseInt(e.detail.value, 10) })
  },

  onConfigGpsEndChange(e) {
    this.setData({ configGpsEnd: parseInt(e.detail.value, 10) })
  },

  onConfigModalCancel() {
    this.setData({ showConfigModal: false })
  },

  // 确定：时间窗口编码为两位代号，生成指令内容并填入 cmdText
  onConfigModalConfirm() {
    const period = parseInt(this.data.configPeriod, 10)
    if (isNaN(period) || period < 5 || period > 60) {
      wx.showToast({ title: '上报周期需在5-60之间', icon: 'none' })
      return
    }

    const workCode = timeWindowCodec.encodeTimeWindow(this.data.configWorkStart, this.data.configWorkEnd)
    if (!workCode) {
      wx.showToast({ title: '开机时间需为当天整点且持续≥1小时', icon: 'none' })
      return
    }
    const gpsCode = timeWindowCodec.encodeTimeWindow(this.data.configGpsStart, this.data.configGpsEnd)
    if (!gpsCode) {
      wx.showToast({ title: 'GPS工作时间需为当天整点且持续≥1小时', icon: 'none' })
      return
    }

    // 结构: 周期,开机代号,GPS代号 （如 10,0K,0L）
    const value = period + ',' + workCode + ',' + gpsCode

    this.setData({
      cmdText: JSON.stringify({ cmd: 'A', value }),
      showConfigModal: false
    })
    this.addLog('info', '已生成配置指令: ' + value)
  },

  // ========== 发送指令 ==========
  onSend() {
    const device = this.data.deviceList[this.data.deviceIndex]
    if (!device) {
      wx.showToast({ title: '请选择目标设备', icon: 'none' })
      return
    }

    let cmdText = this.data.cmdText.trim()
    if (!cmdText) {
      wx.showToast({ title: '请输入指令内容', icon: 'none' })
      return
    }

    // 校验 tm 字段不能超过 60
    try {
      const cmdObj = JSON.parse(cmdText)
      if (cmdObj.tm !== undefined && Number(cmdObj.tm) > 60) {
        cmdObj.tm = 60
        cmdText = JSON.stringify(cmdObj)
        this.addLog('info', 'tm超限，已自动调整为60')
        wx.showToast({ title: 'tm不能超过60，已自动调整', icon: 'none', duration: 2000 })
      }
    } catch (e) {
      // 非JSON或解析失败，跳过校验
    }

    // 获取手动选择的中继转发设备
    const relayDevice = this.data.relayDeviceList[this.data.relayIndex]  // null=自动

    // 手动指定了中继设备，直接用它的密钥发送
    if (relayDevice) {
      this.addLog('info', '中继: ' + relayDevice.deviceId)
      this._doSend(relayDevice, device.deviceId, cmdText)
      return
    }

    // 自动模式：设备已有密钥，直接发送
    if (device.ProductKey && device.DeviceName) {
      this._doSend(device, device.deviceId, cmdText)
    } else {
      // 设备缺少密钥，通过 getDeviceLogbyId 查找上传设备获取密钥
      wx.showLoading({ title: '查询上传设备...' })
      this._queryUploadDevice(device.deviceId, cmdText)
    }
  },

  // 通过 getDeviceBestRssibyId 查询目标设备的最新记录，找到信号最佳的上传设备以获取密钥
  _queryUploadDevice(targetDeviceId, cmdText) {
    const that = this
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceBestRssibyId',
        info: { limit: 3, deviceId: targetDeviceId }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('[DTU指令] getDeviceBestRssibyId 返回:', JSON.stringify(res.data))

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

        console.log('[DTU指令] 候选上传设备:', JSON.stringify(deviceBestRssi),
          '出现次数:', JSON.stringify(deviceCount),
          '最佳设备:', bestDevice, 'RSSI:', bestRssi)

        // 在设备列表中查找上传设备
        const uploadDevice = that.data.deviceList.find(d => d.deviceId === bestDevice)
        if (!uploadDevice) {
          wx.showToast({ title: '上传设备 ' + bestDevice + ' 不在设备列表中', icon: 'none', duration: 2500 })
          return
        }

        if (!uploadDevice.ProductKey || !uploadDevice.DeviceName) {
          wx.showToast({ title: '上传设备 ' + bestDevice + ' 也缺少密钥', icon: 'none', duration: 2500 })
          return
        }

        const rssiInfo = bestRssi > -999 ? ' RSSI:' + bestRssi : ''
        that.addLog('info', '通过上传设备 ' + bestDevice + rssiInfo + ' 获取密钥')
        that._doSend(uploadDevice, targetDeviceId, cmdText)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('[DTU指令] getDeviceBestRssibyId 失败:', err)
        wx.showToast({ title: '查询上传设备失败', icon: 'error' })
      }
    })
  },

  // 解析 RSSI：字符串/数字转整数，无效返回 -999（表示无数据）
  _parseRssi(val) {
    if (val === undefined || val === null || val === '' || val === '-') return -999
    const n = parseInt(val, 10)
    return isNaN(n) ? -999 : n
  },

  // 实际执行发送：credDevice 提供密钥，targetDeviceId 注入到消息中
  _doSend(credDevice, targetDeviceId, cmdText) {
    // 尝试解析为 JSON，并注入目标设备 ID
    let msgObj
    try {
      msgObj = JSON.parse(cmdText)
    } catch (e) {
      // 非 JSON，作为纯文本发送
      msgObj = { text: cmdText }
    }
    // 注入目标设备 ID（而非凭据设备的 ID）
    msgObj.deviceId = targetDeviceId
    const finalMsg = JSON.stringify(msgObj)

    // 构造发送载荷
    const payload = {
      action: 'com',
      deviceName: credDevice.DeviceName,
      productKey: credDevice.ProductKey,
      msg: finalMsg,
      timestamp: Date.now()
    }

    this.addLog('info', '发送 → ' + targetDeviceId + ': ' + finalMsg)
    console.log('[DTU指令] 发送:', JSON.stringify(payload))

    wx.showLoading({ title: '发送中...' })

    const that = this
    wx.request({
      url: FC_URL,
      method: 'POST',
      data: payload,
      timeout: 10000,
      success: (res) => {
        wx.hideLoading()
        const data = res.data
        console.log('[DTU指令] 返回:', JSON.stringify(data))
        that.addLog('success', '返回: ' + JSON.stringify(data))
        wx.showToast({ title: '指令已发送 → ' + targetDeviceId, icon: 'success' })
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('[DTU指令] 失败:', err)
        that.addLog('error', '发送失败: ' + (err.errMsg || '网络错误'))
        wx.showToast({ title: '发送失败', icon: 'error' })
      }
    })
  },

  // ========== 日志 ==========
  addLog(type, text) {
    const now = new Date()
    const time = String(now.getHours()).padStart(2, '0') + ':' +
      String(now.getMinutes()).padStart(2, '0') + ':' +
      String(now.getSeconds()).padStart(2, '0')
    const log = { type, text, time }
    const sendLog = [log].concat(this.data.sendLog).slice(0, 50)
    this.setData({ sendLog })
  }
})
