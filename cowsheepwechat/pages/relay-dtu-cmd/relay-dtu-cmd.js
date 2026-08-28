// pages/relay-dtu-cmd/relay-dtu-cmd.js
// 中继DTU指令页面：选择设备 → 输入/快捷指令 → HTTP发送到云函数
const dataCache = require('../../config/data-cache.js')
const timeWindowCodec = require('../../utils/time-window-codec.js')

// DTU 指令转发云函数地址
const FC_URL = 'https://gpsmoveinfo.cn/fc/sendtodtucmd'
// 设备配置查询接口（用于预填配置下发弹框）
const API_URL = getApp().globalData.api_device_Url

Page({
  data: {
    // 目标设备列表
    deviceList: [],         // 完整设备记录 [{deviceId, ProductKey, DeviceName, rename}, ...]
    deviceDisplayList: [],  // 下拉显示用 ["deviceId (rename)", ...]
    deviceIndex: 0,

    // 指令内容
    cmdText: '',
    quickSelected: 0,  // 当前选中快捷按钮

    // 工作时间弹框
    showWorkTimeModal: false,
    workStartTime: '00:00',
    workEndTime: '23:59',

    // Debug开关状态
    debugValue: 0,
    // Lora开关状态
    loraSwValue: 0,

    // ===== 配置下发弹框 =====
    // 中继为太阳能供电，无GPS定位/主周期概念，弹窗只含：上报周期 + 开机时间
    showConfigModal: false,
    hourRange: (() => { const arr = []; for (let i = 0; i <= 24; i++) arr.push(i); return arr })(),      // 结束时间可选 0-24
    startHourRange: (() => { const arr = []; for (let i = 0; i <= 23; i++) arr.push(i); return arr })(),  // 开始时间可选 0-23
    configPeriod: '10',          // 上报周期（5-60）
    configWorkStart: 0,          // 开机时间开始（小时索引）
    configWorkEnd: 24,           // 开机时间结束（24=23:59）

    // 发送日志
    sendLog: []
  },

  onLoad(options) {
    const preselectDeviceId = options.deviceId || ''
    // 从缓存获取设备列表
    dataCache.getDeviceList((deviceData) => {
      if (deviceData && deviceData.recordList && deviceData.recordList.length > 0) {
        // 只显示有 ProductKey 的设备
        const validDevices = deviceData.recordList.filter(r =>
          r.deviceId && r.deviceId !== '-' && r.ProductKey
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

          this.setData({
            deviceList: uniqueDevices,
            deviceDisplayList: displayList,
            deviceIndex: selectedIndex
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

  // ========== 指令输入 ==========
  onCmdInput(e) {
    this.setData({ cmdText: e.detail.value, quickSelected: 0 })
  },

  // ========== 快捷按钮 ==========

  // ========== 工作时间快捷按钮 ==========
  onQuickWorkTime() {
    this.setData({ showWorkTimeModal: true, quickSelected: 14 })
  },
  onQuickTxPower() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'txpower', value: 20 }), quickSelected: 16 })
  },
  onQuickLoraSw() {
    const newVal = this.data.loraSwValue === 0 ? 1 : 0
    this.setData({
      cmdText: JSON.stringify({ cmd: 'lorasw', value: newVal }),
      quickSelected: 17,
      loraSwValue: newVal
    })
  },
  onQuickRelayReboot() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'relayreboot', value: 1 }), quickSelected: 18 })
  },
  onQuickConfig() {
    const device = this.data.deviceList[this.data.deviceIndex]
    if (!device) {
      wx.showToast({ title: '请先选择目标设备', icon: 'none' })
      return
    }

    this.setData({ quickSelected: 19 })
    // 打开配置下发弹框：优先用缓存中的设备配置预填（一般与网络一致，避免闪动），无缓存时用默认值
    const cached = dataCache.getCachedDeviceConfig(device.deviceId)
    const cachedDefaults = cached && cached.lorastr ? this._parseLoraConfigToModal(cached.lorastr) : null
    this._openConfigModal(cachedDefaults)
    if (cachedDefaults) {
      this.addLog('info', '已用缓存配置预填弹框: ' + cached.lorastr)
    }

    // 异步查询设备已有配置，用于更新预填弹框（网络返回后覆盖缓存值）
    const that = this
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceConfigAll',
        info: { deviceId: device.deviceId, wechatid: getApp().getWechatId() }
      },
      timeout: 8000,
      success: (res) => {
        console.log('[中继DTU] getDeviceConfigAll 返回:', JSON.stringify(res.data))
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
        console.error('[中继DTU] getDeviceConfigAll 失败:', err)
      }
    })
  },

  // 打开配置下发弹框，defaults 为空时使用默认值 10,0-24
  _openConfigModal(defaults) {
    const d = defaults || {}
    this.setData({
      showConfigModal: true,
      configPeriod: d.configPeriod !== undefined ? d.configPeriod : '10',
      configWorkStart: d.configWorkStart !== undefined ? d.configWorkStart : 0,
      configWorkEnd: d.configWorkEnd !== undefined ? d.configWorkEnd : 24
    })
  },

  // 解析设备配置 lorastr 第3段: 上报周期,开机时间(,GPS工作时间,主周期) → 弹框默认值
  // 中继只用前两段（周期+开机时间）；兼容两位代号（新格式）与 start-duration（旧格式）
  _parseLoraConfigToModal(lorastr) {
    const parts = String(lorastr || '').split('|')
    if (parts.length < 3 || !parts[2]) return null
    const segs = parts[2].split(',')
    if (segs.length < 2) return null

    const period = parseInt(segs[0], 10)
    const workWin = timeWindowCodec.parseTimeWindow(segs[1])
    if (!workWin) return null

    // end=23 代表 23:59，弹框结束时间回填为 24（对应"当天最后一刻"）
    return {
      configPeriod: String(isNaN(period) || period < 5 ? 10 : (period > 60 ? 60 : period)),
      configWorkStart: workWin.start,
      configWorkEnd: workWin.end === 23 ? 24 : workWin.end
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

  onConfigModalCancel() {
    this.setData({ showConfigModal: false })
  },

  // 确定：时间窗口编码为两位代号，生成指令内容并填入 cmdText
  // 中继无GPS/主周期，value 只含: 周期,开机代号 （如 10,0K）
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

    const value = period + ',' + workCode

    this.setData({
      cmdText: JSON.stringify({ cmd: 'A', value }),
      showConfigModal: false
    })
    this.addLog('info', '已生成配置指令: ' + value)
  },
  onQuickDebug() {
    const newVal = this.data.debugValue === 0 ? 1 : 0
    this.setData({
      cmdText: JSON.stringify({ cmd: 'debug', value: newVal }),
      quickSelected: 4,
      debugValue: newVal
    })
  },

  onWorkStartChange(e) {
    this.setData({ workStartTime: e.detail.value })
  },

  onWorkEndChange(e) {
    this.setData({ workEndTime: e.detail.value })
  },

  onWorkTimeConfirm() {
    const start = this.data.workStartTime  // HH:mm
    const end = this.data.workEndTime      // HH:mm

    // 转成分钟数进行比较
    const [sh, sm] = start.split(':').map(Number)
    const [eh, em] = end.split(':').map(Number)
    const startMin = sh * 60 + sm
    const endMin = eh * 60 + em

    // 验证：开始时间必须小于结束时间
    if (startMin >= endMin) {
      wx.showToast({ title: '开始时间必须小于结束时间', icon: 'none' })
      return
    }

    // 验证：时间窗口不能小于5小时（300分钟）
    if (endMin - startMin < 300) {
      wx.showToast({ title: '工作时间窗口不能小于5小时', icon: 'none' })
      return
    }

    // 格式: HH:mm-HH:mm
    const value = start + '-' + end
    this.setData({
      cmdText: JSON.stringify({ cmd: 'worktime', value: value }),
      showWorkTimeModal: false
    })
  },

  onWorkTimeCancel() {
    this.setData({ showWorkTimeModal: false })
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

    // 直接用选定设备的密钥发送
    this._doSend(device, device.deviceId, cmdText)
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
      timestamp: Date.now(),
      info: { wechatid: getApp().getWechatId() }
    }

    this.addLog('info', '发送 → ' + targetDeviceId + ': ' + finalMsg)
    console.log('[中继DTU] 发送:', JSON.stringify(payload))

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
        console.log('[中继DTU] 返回:', JSON.stringify(data))
        that.addLog('success', '返回: ' + JSON.stringify(data))
        wx.showToast({ title: '指令已发送 → ' + targetDeviceId, icon: 'success' })
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('[中继DTU] 失败:', err)
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
