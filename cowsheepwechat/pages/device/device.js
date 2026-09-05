// device.js - 设备管理
const API_DEVICE_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')
const timeWindowCodec = require('../../utils/time-window-codec.js')

Page({
  data: {
    // 新增设备弹窗
    showAddModal: false,
    addDeviceId: '',

    // 设备列表
    deviceList: [],
    isAdmin: false,
    showAllDevices: false,
    refresherTriggered: false,
    // 设备配置休眠状态映射 deviceId -> { isDormant, powerOnTime }
    deviceConfigMap: {}
  },

  _readSettings() {
    let isAdmin = false
    let showAllDevices = false
    try {
      const adminVal = wx.getStorageSync('setting_is_admin')
      isAdmin = !!(getApp().globalData.isAdmin || adminVal)
    } catch (e) { /* ignore */ }
    try {
      const raw = wx.getStorageSync('setting_show_all_devices')
      showAllDevices = raw === true || raw === 'true' || raw === 1 || raw === '1'
    } catch (e) { /* ignore */ }
    this.setData({ isAdmin, showAllDevices })
  },

  onLoad() {
    this._readSettings()
    this.fetchDeviceList()
  },

  onShow() {
    this._readSettings()
    // 页面重新可见：若有列表则立即按当前时间刷新一次倒计时并恢复每秒跳动
    if (this.data.deviceList && this.data.deviceList.length) {
      this._startCountdownTimer()
    }
  },

  onHide() {
    this._stopCountdownTimer()
  },

  onUnload() {
    this._stopCountdownTimer()
  },

  // ========== 获取设备配置（工作时间判断休眠） ==========
  fetchDeviceConfigAll(forceRefresh, callback) {
    const that = this
    wx.request({
      url: API_DEVICE_URL,
      method: 'POST',
      data: {
        action: 'getDeviceConfigAll',
        info: { wechatid: getApp().getWechatId() }
      },
      success: (res) => {
        console.log('设备配置All查询返回:', JSON.stringify(res.data))
        let rawList = []
        if (res.data && res.data.data && Array.isArray(res.data.data)) {
          rawList = res.data.data
        } else if (Array.isArray(res.data)) {
          rawList = res.data
        }
        // 构建 deviceId → { isDormant, powerOnTime } 映射
        const configMap = {}
        rawList.forEach(record => {
          const attr = {}
          if (record.attributes) {
            record.attributes.forEach(item => { attr[item.columnName] = item.columnValue })
          }
          if (record.primaryKey) {
            record.primaryKey.forEach(item => { attr[item.name] = item.value })
          }
          if (record.lorastr) attr.lorastr = record.lorastr
          const deviceId = attr.deviceId || (record.primaryKey && record.primaryKey.find(p => p.name === 'deviceId') ? record.primaryKey.find(p => p.name === 'deviceId').value : null)
          if (!deviceId) return

          const configLorastr = attr.lorastr || ''
          const result = that._checkWorkingHours(configLorastr)
          configMap[deviceId] = result
        })
        that.setData({ deviceConfigMap: configMap })
        if (callback) callback(configMap)
      },
      fail: (err) => {
        console.error('设备配置All查询失败:', err)
        if (callback) callback({})
      }
    })
  },

  // 根据配置lorastr判断当前是否在工作时间内，同时提取上报周期（分钟）与主周期（分钟）
  // lorastr格式: 6|v4-16|5,0M,38,2|1.0|4.2|18
  // 第3段(按|分)再按,分: 上报周期,开机时间,GPS工作时间[,主周期]
  // 上报周期=开机(工作)时间内GPS上报间隔(分钟，如5分钟)；GPS工作时间仅展示，不影响工作期判断
  // 开机时间/GPS工作时间为两位base62代号（兼容旧格式 "8-6" = 8:00开始持续6小时）
  // 主周期为第4个参数 1-10（=10-100分钟，参数×10，如2=20分钟）：不在开机时间(非工作时间)时设备按主周期上报
  _checkWorkingHours(configLorastr) {
    const result = { isDormant: false, powerOnTime: '-', reportInterval: 30, mainPeriodMin: 0, powerWin: null, gpsWin: null }
    if (!configLorastr) return result

    const parts = configLorastr.split('|')
    if (parts.length < 3 || !parts[2]) return result

    const configParts = parts[2].split(',')
    if (configParts.length < 1) return result

    // 上报周期（分钟），第3段第1项（工作时段内GPS按此周期上报）
    const intervalNum = parseInt(configParts[0].trim(), 10)
    if (intervalNum > 0) result.reportInterval = intervalNum

    // 主周期（分钟），第3段第4项：参数 1-10 = 10-100分钟（不在工作时段时使用）
    if (configParts.length >= 4) {
      const mainNum = parseInt(configParts[3].trim(), 10)
      if (!isNaN(mainNum) && mainNum >= 1 && mainNum <= 10) result.mainPeriodMin = mainNum * 10
    }

    if (configParts.length < 2 || !configParts[1]) return result

    const powerRaw = configParts[1].trim()
    result.powerOnTime = timeWindowCodec.formatTimeRange(powerRaw)

    // 开机时间窗口（仅当天）：区间内=活跃，区间外=休眠（设备休眠颜色判断沿用）
    const win = timeWindowCodec.parseTimeWindow(powerRaw)
    if (!win) return result
    result.powerWin = { start: win.start, end: win.end }

    // GPS工作时间窗口（仅当天）：仅作信息展示（详情页"GPS时间"），不参与倒计时/工作期判断
    if (configParts.length >= 3 && configParts[2]) {
      const gpsWin = timeWindowCodec.parseTimeWindow(configParts[2].trim())
      if (gpsWin) result.gpsWin = { start: gpsWin.start, end: gpsWin.end }
    }

    const now = new Date()
    const currentMinutes = now.getHours() * 60 + now.getMinutes()
    const startMinutes = win.start * 60
    // end=23 代表 23:59
    const endMinutes = win.end === 23 ? 23 * 60 + 59 : win.end * 60
    result.isDormant = currentMinutes < startMinutes || currentMinutes >= endMinutes
    return result
  },

  // 当前时间应采用的推算周期（分钟）：
  // "工作期"只按"开机时间窗口"判断（不使用GPS时间窗口）——开机时间内用上报周期（第3段第1项，如5分钟）；
  // 不在开机时间（非工作时间）→ 用主周期（第4个参数×10分钟，如参数2=20分钟）；未配置主周期时回退到上报周期
  _effectiveIntervalFor(item, now) {
    const work = (item && item.reportIntervalMin > 0) ? item.reportIntervalMin : 0
    const win = (item && item.cadenceWin) || null
    if (!win) return work
    const d = now ? new Date(now) : new Date()
    const cur = d.getHours() * 60 + d.getMinutes()
    const start = win.start * 60
    const end = win.end === 23 ? 23 * 60 + 59 : win.end * 60
    const inWork = cur >= start && cur < end
    if (inWork) return work
    const main = (item && item.mainPeriodMin > 0) ? item.mainPeriodMin : 0
    return main || work
  },

  // ========== 获取设备列表 ==========
  fetchDeviceList(forceRefresh, onComplete) {
    let deviceData, livestockData, lotData, syncData, configMapData
    let done = 0
    const merge = () => {
      done++
      if (done < 5) return

      const nameMap = {}
      if (livestockData && livestockData.livestockList) {
        livestockData.livestockList.forEach(item => {
          if (item.cowsheepId) nameMap[item.cowsheepId] = item.name
        })
      }

      const lotMap = {}
      if (lotData && lotData.lotList) {
        lotData.lotList.forEach(rec => {
          if (rec.deviceId && rec.deviceId !== '-') {
            if (!lotMap[rec.deviceId]) lotMap[rec.deviceId] = rec
          }
        })
      }

      const syncMap = (syncData && syncData.syncMap) || {}

      const deviceList = (deviceData.recordList || []).map(item => {
        const lotRec = lotMap[item.deviceId]
        const syncInfo = syncMap[item.deviceId]

        // —— 最后上报时间：只从「对时/定位」两类来源取（LOT最新表 + 对时同步表），取两者中更晚的一次 ——
        // LOT表 lorastr 首段为类型编号：1=GPS定位, 2=对时, 5=跟踪（视为定位）
        let lastTs = NaN            // 最后上报时间戳(ms)
        let lastRaw = ''            // 最后上报原始时间串
        let lastType = ''           // 'gps' | 'time' | ''

        if (lotRec && lotRec.rawTime && lotRec.rawTime !== '-') {
          const ts = new Date(lotRec.rawTime).getTime()
          if (!isNaN(ts)) {
            lastTs = ts
            lastRaw = lotRec.rawTime
            const typePart = (lotRec.lorastr || '').split('|')[0]
            if (typePart === '1' || typePart === '5') lastType = 'gps'
            else if (typePart === '2') lastType = 'time'
          }
        }
        if (syncInfo && syncInfo.rawTime && syncInfo.rawTime !== '-') {
          const ts = new Date(syncInfo.rawTime).getTime()
          if (!isNaN(ts) && (isNaN(lastTs) || ts > lastTs)) {
            lastTs = ts
            lastRaw = syncInfo.rawTime
            lastType = 'time'   // 对时同步表记录 = 对时
          }
        }

        const hasReport = !isNaN(lastTs)
        let lastDate = '-'
        let lastTimePart = ''
        if (hasReport) {
          if (lastRaw.includes(' ')) {
            const seg = lastRaw.split(' ')
            lastDate = seg[0]
            lastTimePart = seg[1]
          } else {
            lastDate = lastRaw
          }
        }

        // 电量仅从同步时间表（device_sync）取，统一归一化为 0~100 显示
        let battery = ''
        if (syncInfo && syncInfo.battery) battery = this._formatBatteryPercent(syncInfo.battery)

        // 配置：上报间隔（分钟）来自配置表 lorastr 第3段第1项；配置表无此设备 → 不猜测，按未配置处理
        const cfg = (configMapData && configMapData[item.deviceId]) || null
        const isDormant = !!(cfg && cfg.isDormant)
        const reportIntervalMin = (cfg && cfg.reportInterval && cfg.reportInterval > 0) ? cfg.reportInterval : 0
        // 主周期（分钟）：配置第3段第4个参数 1-10 = 10-100分钟；不在工作时段时设备按主周期上报
        const mainPeriodMin = (cfg && cfg.mainPeriodMin && cfg.mainPeriodMin > 0) ? cfg.mainPeriodMin : 0
        // "工作期"边界：只按设备"开机时间窗口"判断（不使用GPS时间窗口）——
        // 开机时间内按上报周期(第3段第1项，如5分钟)推算；不在开机时间(非工作时间)按主周期(第4项×10，如20分钟)
        // 跨时段由每秒定时器动态切换推算周期
        const cadenceWin = (cfg && cfg.powerWin) || null

        // 倒计时初始状态（进入页面后由定时器每秒刷新文本）
        // 工作时段内按"上报周期"推算；不在工作时段按"主周期"推算（未配置主周期则回退上报周期）
        const nowMs = Date.now()
        const effIntervalMin = this._effectiveIntervalFor({ reportIntervalMin, mainPeriodMin, cadenceWin }, nowMs)
        const cd = this._buildCountdownState(lastTs, effIntervalMin, nowMs)

        // 信号图标颜色：
        // 中继设备（有 ProductKey）：不在工作区间 → 灰色
        // 其它设备（GPS设备）：超过2个上报周期未上报数据 → 灰色
        const isRelay = !!(item.ProductKey && item.ProductKey !== '-')
        const signalColor = isRelay
          ? (isDormant ? '#999999' : '#4caf50')
          : (cd.overdue ? '#999999' : '#4caf50')

        // 设备名颜色：与信号图标灰色条件一致
        // 中继设备不在工作区间 → 灰色；其它设备超过2个上报周期未上报 → 灰色；其余黑色
        const nameColor = signalColor === '#999999' ? '#999999' : ''

        return {
          ...item,
          date: lastDate,
          time_part: lastTimePart,
          rawTime: lastRaw,
          bindName: item.link_cowsheep_id ? (nameMap[item.link_cowsheep_id] || item.link_cowsheep_id) : '',
          hasReport,
          lastReportTs: lastTs,
          reportIntervalMin,
          mainPeriodMin,
          cadenceWin,
          countdownText: cd.text,
          timeColor: cd.color,
          timeBgColor: cd.bgColor,
          nextTimeText: cd.nextText,
          overdue: cd.overdue,
          dotColor: cd.color,
          lastRecordType: lastType,
          battery,
          batteryColor: isDormant ? '#999999' : (battery && parseFloat(battery) < 50) ? '#f44336' : '#333',
          isDormant: isDormant,
          powerOnTime: (cfg && cfg.powerOnTime) || '-',
          signalColor: signalColor,
          nameColor: nameColor
        }
      })

      // 排序：无ProductKey的在前，有ProductKey的排到最后，各自内部按设备ID中"-"后面的序号数字排序
      deviceList.sort((a, b) => {
        const hasPK = (item) => !!(item.ProductKey && item.ProductKey !== '-')
        // 有ProductKey的排后面
        if (hasPK(a) !== hasPK(b)) return hasPK(a) ? 1 : -1
        // 同组内按设备ID序号升序
        const getSeq = (id) => {
          if (!id) return 0
          const match = id.match(/-(\d+)$/)
          return match ? parseInt(match[1], 10) : 0
        }
        return getSeq(a.deviceId) - getSeq(b.deviceId)
      })

      // 根据设置过滤：如果未开启"显示所有设备"，仅显示 visible=true 的设备
      const filteredList = this.data.showAllDevices
        ? deviceList
        : deviceList.filter(item => item.visible === true)

      this.setData({ deviceList: filteredList })
      // 列表就绪后启动倒计时刷新
      this._startCountdownTimer()
      if (forceRefresh) {
        wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
      }
      if (onComplete) onComplete()
    }

    dataCache.getDeviceList((data) => { deviceData = data; merge() }, forceRefresh)
    dataCache.getLivestockList((data) => { livestockData = data; merge() }, forceRefresh)
    dataCache.getDeviceLotRefresh((data) => { lotData = data; merge() }, forceRefresh)
    dataCache.getDeviceSyncAll((data) => { syncData = data; merge() }, forceRefresh)
    this.fetchDeviceConfigAll(forceRefresh, (configMap) => { configMapData = configMap; merge() })
  },

  refreshDeviceList() {
    this.fetchDeviceList(true)
  },

  onPullDownRefresh() {
    this.setData({ refresherTriggered: true })
    this.fetchDeviceList(true, () => {
      this.setData({ refresherTriggered: false })
    })
  },

  // 电量归一化为 0~100 显示：兼容 0~1 小数（如 1.0/0.87）与 0~100 整数（如 99）
  _formatBatteryPercent(raw) {
    if (raw === null || raw === undefined || raw === '') return ''
    const n = parseFloat(raw)
    if (isNaN(n)) return raw
    const percent = n > 1 ? Math.round(n) : Math.round(n * 100)
    return String(percent)
  },

  // ========== 时间倒计时（距下次预计上报） ==========
  // 依据：最后上报时间(对时/定位) + 配置表上报间隔(分钟) → 下次预计时间 → 实时倒计时
  // 颜色：
  //   未到下次预计 → 绿色（倒计时中）
  //   已过下次预计、未超过1个完整周期 → 红色（超时）
  //   已超过1个完整周期仍未上报（约2个周期无数据）→ 灰色（久未上报）
  _buildCountdownState(lastTs, intervalMin, now) {
    const empty = { text: '', color: '#999', bgColor: '#f5f5f5', nextText: '', overdue: false }
    if (!lastTs || isNaN(lastTs)) return empty

    const fmtTime = (ts) => this._formatFullTime(ts)
    if (!intervalMin || intervalMin <= 0) {
      // 配置表无上报间隔 → 无法推算下次预计时间
      return {
        text: '周期未配置',
        color: '#999',
        bgColor: '#f5f5f5',
        nextText: '最后上报 ' + fmtTime(lastTs),
        overdue: false
      }
    }

    const periodMs = intervalMin * 60000
    const nextTs = lastTs + periodMs
    const remain = nextTs - now

    if (remain > 0) {
      // 正常：距下次预计上报的实时倒计时
      return {
        text: '距下次 ' + this._formatClock(remain),
        color: '#4caf50',
        bgColor: '#e8f5e9',
        nextText: '预计上报 ' + fmtTime(nextTs),
        overdue: false
      }
    }

    const past = -remain
    if (past >= periodMs) {
      // 超过1个完整周期未再上报 → 灰色（久未上报）：
      // 徽章由静态"久未上报"改为"自上次上报时间至今"的累计时长（随每秒定时器实时跳动）
      return {
        text: '未上报 ' + this._formatLongClock(now - lastTs),
        color: '#999',
        bgColor: '#f5f5f5',
        nextText: '最后上报 ' + fmtTime(lastTs),
        overdue: true
      }
    }
    // 已过下次预计、尚未超过1个周期 → 红色超时
    return {
      text: '超时 ' + this._formatClock(past),
      color: '#f44336',
      bgColor: '#ffebee',
      nextText: '预计 ' + fmtTime(nextTs) + ' 未上报',
      overdue: false
    }
  },

  // 毫秒 → 倒计时文本（≥1小时 HH:MM:SS，不足1小时 MM:SS）
  _formatClock(ms) {
    const total = Math.max(0, Math.floor(ms / 1000))
    const h = Math.floor(total / 3600)
    const m = Math.floor((total % 3600) / 60)
    const s = total % 60
    const p2 = (n) => String(n).padStart(2, '0')
    return h > 0 ? p2(h) + ':' + p2(m) + ':' + p2(s) : p2(m) + ':' + p2(s)
  },

  // 毫秒 → 累计时长文本（"上次上报时间至今"的正计时）：
  // ≥1天 → "X天HH:MM:SS"；≥1小时 → "HH:MM:SS"；不足1小时 → "MM:SS"
  _formatLongClock(ms) {
    const total = Math.max(0, Math.floor(ms / 1000))
    const d = Math.floor(total / 86400)
    const h = Math.floor((total % 86400) / 3600)
    const m = Math.floor((total % 3600) / 60)
    const s = total % 60
    const p2 = (n) => String(n).padStart(2, '0')
    if (d > 0) return d + '天 ' + p2(h) + ':' + p2(m) + ':' + p2(s)
    if (h > 0) return p2(h) + ':' + p2(m) + ':' + p2(s)
    return p2(m) + ':' + p2(s)
  },

  // 时间戳 → "YYYY/M/D HH:mm:ss"
  _formatFullTime(ts) {
    const d = new Date(ts)
    const p2 = (n) => String(n).padStart(2, '0')
    return d.getFullYear() + '/' + (d.getMonth() + 1) + '/' + d.getDate() +
      ' ' + p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds())
  },

  // 每秒刷新列表中每台设备的倒计时文本/颜色
  // 推算周期随当前时段动态切换：开机时间窗口内用上报周期（如5分钟），不在开机时间（非工作时间）用主周期（如20分钟）
  _tickCountdown() {
    const list = this.data.deviceList
    if (!list || !list.length) return
    const now = Date.now()
    const patch = {}
    for (let i = 0; i < list.length; i++) {
      const it = list[i]
      const effMin = this._effectiveIntervalFor(it, now)
      const cd = this._buildCountdownState(it.lastReportTs, effMin, now)
      patch['deviceList[' + i + '].countdownText'] = cd.text
      patch['deviceList[' + i + '].timeColor'] = cd.color
      patch['deviceList[' + i + '].timeBgColor'] = cd.bgColor
      patch['deviceList[' + i + '].nextTimeText'] = cd.nextText
      patch['deviceList[' + i + '].dotColor'] = cd.color
      patch['deviceList[' + i + '].overdue'] = cd.overdue
    }
    this.setData(patch)
  },

  _startCountdownTimer() {
    this._stopCountdownTimer()
    this._tickCountdown()
    this._countdownTimer = setInterval(() => {
      this._tickCountdown()
    }, 1000)
  },

  _stopCountdownTimer() {
    if (this._countdownTimer) {
      clearInterval(this._countdownTimer)
      this._countdownTimer = null
    }
  },

  // ========== 新增设备 ==========
  onAdd() {
    this.setData({
      showAddModal: true,
      addDeviceId: ''
    })
  },

  onAddDeviceIdInput(e) {
    this.setData({ addDeviceId: e.detail.value })
  },

  onAddClose() {
    this.setData({ showAddModal: false })
  },

  onAddConfirm() {
    const deviceId = this.data.addDeviceId.trim()
    if (!deviceId) {
      wx.showToast({ title: '请输入设备ID', icon: 'none' })
      return
    }

    this.setData({ showAddModal: false })
    wx.showLoading({ title: '提交中...' })

    wx.request({
      url: API_DEVICE_URL,
      method: 'POST',
      data: {
        action: 'addDevice',
        info: { deviceId, wechatid: getApp().getWechatId() }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('新增设备返回:', JSON.stringify(res.data))
        wx.showToast({ title: '新增成功', icon: 'success', duration: 1500 })
        this.fetchDeviceList(true)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('新增设备失败:', err)
        wx.showToast({ title: '提交失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // ========== 点击设备：选中并回首页展示该设备详情 ==========
  onTapDevice(e) {
    const deviceId = e.currentTarget.dataset.deviceid
    if (!deviceId) return

    // 缓存选中的设备，首页每次打开都恢复该设备详情
    dataCache.setHomeSelectedDevice(deviceId)
    wx.switchTab({
      url: '/pages/index/index',
      fail: (err) => {
        console.error('切回首页失败:', err)
        wx.showToast({ title: '页面跳转失败', icon: 'none' })
      }
    })
  }
})
