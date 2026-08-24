// pages/relay-dtu-cmd/relay-dtu-cmd.js
// 中继DTU指令页面：选择设备 → 输入/快捷指令 → HTTP发送到云函数
const dataCache = require('../../config/data-cache.js')

// DTU 指令转发云函数地址
const FC_URL = 'https://gpsmoveinfo.cn/fc/sendtodtucmd'

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
