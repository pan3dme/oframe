// pages/dtu-cmd/dtu-cmd.js
// DTU发送指令页面：选择设备 → 输入/快捷指令 → HTTP发送到云函数
const dataCache = require('../../config/data-cache.js')

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

    // 工作时间弹框
    showWorkTimeModal: false,
    workStartTime: '00:00',
    workEndTime: '23:59',

    // 发送日志
    sendLog: []
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
  onQuickReport30Min() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'setinterval', value: 30 }), quickSelected: 7 })
  },
  onQuickRefreshGps() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'refrishgps', value: 30 }), quickSelected: 2 })
  },
  onQuickNormalMode() {
    this.setData({ cmdText: JSON.stringify({ cmd: 'sendmode', value: 0 }), quickSelected: 12 })
  },

  // ========== 工作时间快捷按钮 ==========
  onQuickWorkTime() {
    this.setData({ showWorkTimeModal: true, quickSelected: 14 })
  },
  onQuickSyncTime() {
    const now = new Date()
    const timeStr = now.getFullYear() + '/' +
      String(now.getMonth() + 1).padStart(2, '0') + '/' +
      String(now.getDate()).padStart(2, '0') + ' ' +
      String(now.getHours()).padStart(2, '0') + ':' +
      String(now.getMinutes()).padStart(2, '0') + ':' +
      String(now.getSeconds()).padStart(2, '0') + '.' +
      String(now.getMilliseconds()).padStart(3, '0')
    this.setData({ cmdText: JSON.stringify({ cmd: 'synctime', value: timeStr }), quickSelected: 15 })
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

  // 通过 getDeviceLogbyId 查询目标设备的最新N条记录，找到信号最佳的上传设备以获取密钥
  _queryUploadDevice(targetDeviceId, cmdText) {
    const that = this
    const today = this._getTodayStr()
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: {
          limit: 2,
          deviceId: targetDeviceId
 
        },
        time: getApp().formatTime()
      },
      success: (res) => {
        wx.hideLoading()
        console.log('[DTU指令] getDeviceLogbyId 返回:', JSON.stringify(res.data))

        // 解析记录
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

        // 解析所有记录，提取 upDateDevice 和 RSSI
        const parsedRecords = rawList.map(record => {
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
          const upDateDevice = attr.upDateDevice || attr.updatedevice || record.upDateDevice || record.updatedevice || ''
          const rssi = this._parseRssi(attr.rssi || attr.RSSI || record.rssi || record.RSSI)
          return { upDateDevice, rssi }
        }).filter(r => r.upDateDevice && r.upDateDevice !== '-')

        if (parsedRecords.length === 0) {
          wx.showToast({ title: '记录中未找到有效上传设备', icon: 'none', duration: 2500 })
          return
        }

        // 按设备聚合，取每个设备的最佳 RSSI（最大值=信号最好）
        const deviceBestRssi = {}
        const deviceCount = {}
        parsedRecords.forEach(r => {
          if (!deviceBestRssi[r.upDateDevice] || r.rssi > deviceBestRssi[r.upDateDevice]) {
            deviceBestRssi[r.upDateDevice] = r.rssi
          }
          deviceCount[r.upDateDevice] = (deviceCount[r.upDateDevice] || 0) + 1
        })

        // 选信号最好的设备：RSSI 越小越好（绝对值越小，信号越强），其次出现次数多
        let bestDevice = null
        let bestRssi = 999
        let bestCount = 0
        Object.keys(deviceBestRssi).forEach(devId => {
          const r = deviceBestRssi[devId]
          const c = deviceCount[devId]
          // 有 RSSI 数据时按 RSSI 越小排序，否则按出现次数
          const hasRssi = r < 999
          if (hasRssi) {
            if (r < bestRssi || (r === bestRssi && c > bestCount)) {
              bestRssi = r
              bestCount = c
              bestDevice = devId
            }
          } else if (bestRssi >= 999) {
            // 都没有 RSSI 时，按出现次数选
            if (c > bestCount) {
              bestCount = c
              bestDevice = devId
            }
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
        console.error('[DTU指令] getDeviceLogbyId 失败:', err)
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

  // 获取今天日期字符串 yyyy-MM-dd
  _getTodayStr() {
    const d = new Date()
    return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0')
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
