// device.js - 设备管理
const dataCache = require('../../config/data-cache.js')
const bleManager = require('../../utils/ble-manager.js')

Page({
  data: {
    // 新增设备弹窗
    showAddModal: false,
    addDeviceId: '',

    // 设备列表
    deviceList: [],
    // 分类筛选：设备(device，无ProductKey) / 中继(relay，有ProductKey) / 离线(offline，久未上报)
    activeCategory: 'device',
    // "设备"分类下的显示模式：false=全部设备，true=仅在线设备（再次点击"设备"切换）
    deviceOnlineOnly: false,
    categoryCount: { all: 0, device: 0, relay: 0, offline: 0 },
    isAdmin: false,
    showAllDevices: false,
    refresherTriggered: false,
    // 设备配置休眠状态映射 deviceId -> { isDormant, powerOnTime }
    deviceConfigMap: {}
  },

  // 是否为中继设备：有有效 ProductKey
  _isRelay(item) {
    return !!(item && item.ProductKey && item.ProductKey !== '-')
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
    this._syncTabBar()
    this._readSettings()
    // 页面重新可见：若有列表则用缓存重新合并一次（把离开期间蓝牙缓存新增的记录时间
    // 也计入"最后上报时间"），并按当前时间刷新倒计时、恢复每秒跳动
    if (this.data.deviceList && this.data.deviceList.length) {
      if (this._lastDeviceData) {
        this._applyMergedData(this._lastLotData, this._lastSyncData, false)
      } else {
        this._startCountdownTimer()
      }
    }
  },

  // 同步自定义 tabBar 的选中态（设备为第 2 个 tab，下标 1）
  _syncTabBar() {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 1 })
    }
  },

  // 设置底部"设备"TAB 的转圈加载态
  _setDeviceTabSpinning(spinning) {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ deviceRefreshing: !!spinning })
    }
  },

  onHide() {
    this._stopCountdownTimer()
    // 页面隐藏时收回转圈态，避免下次进入残留
    this._setDeviceTabSpinning(false)
  },

  onUnload() {
    this._stopCountdownTimer()
  },

  // 点击底部"设备"TAB（已在设备页）：无感刷新设备列表
  // 只更新 LOT(getDeviceGpsAll) + SYNC(getDevicesyncAll) 两份实时上报数据，
  // 复用已缓存的设备列表(deviceList)与设备配置(deviceConfigMap)重新合并——不请求 deviceList / config
  // 不弹提示、不显示全局加载，仅在"设备"TAB 上显示转圈，刷新完成后恢复
  onDeviceTabRefresh() {
    this._setDeviceTabSpinning(true)
    this._refreshLotAndSyncOnly(() => {
      this._setDeviceTabSpinning(false)
    })
  },

  // ========== 获取设备列表（下拉刷新/首屏/新增设备后）：刷新全部 4 个数据源 ==========
  // silent=true 时强制刷新但不弹"已刷新"提示（用于点击底部"设备"TAB 的无感刷新）
  fetchDeviceList(forceRefresh, onComplete, silent) {
    let deviceData, lotData, syncData, configMapData
    let done = 0
    const merge = () => {
      done++
      if (done < 4) return
      // 缓存最近一次的全量数据，供底部 TAB 无感刷新复用
      this._lastDeviceData = deviceData
      this._lastConfigMap = configMapData
      this._applyMergedData(lotData, syncData, !!(forceRefresh && !silent), onComplete)
    }

    dataCache.getDeviceList((data) => { deviceData = data; merge() }, forceRefresh)
    dataCache.getDeviceLotRefresh((data) => { lotData = data; merge() }, forceRefresh)
    dataCache.getDeviceSyncAll((data) => { syncData = data; merge() }, forceRefresh)
    this.fetchDeviceConfigAll(forceRefresh, (configMap) => { configMapData = configMap; merge() })
  },

  // ========== 仅刷新 LOT + SYNC（底部"设备"TAB 单击无感刷新） ==========
  // 复用 _lastDeviceData + _lastConfigMap 重新合并；不弹"已刷新"toast
  // 若缓存尚未建立（如首屏首次加载时点 TAB），兜底走全量刷新
  _refreshLotAndSyncOnly(onComplete) {
    let lotData, syncData
    let done = 0
    const merge = () => {
      done++
      if (done < 2) return
      if (!this._lastDeviceData || !this._lastConfigMap) {
        // 兜底：缓存还没建立 → 回退到全量刷新
        this.fetchDeviceList(true, onComplete, true)
        return
      }
      this._applyMergedData(lotData, syncData, false, onComplete)
    }
    dataCache.getDeviceLotRefresh((data) => { lotData = data; merge() }, true)
    dataCache.getDeviceSyncAll((data) => { syncData = data; merge() }, true)
  },

  // 把 4 类原始数据(deviceList + lot + sync + config)合并成最终 UI 列表的统一入口
  // 供 fetchDeviceList（下拉刷新/首屏）与 _refreshLotAndSyncOnly（底部 TAB 无感刷新）复用
  _applyMergedData(lotData, syncData, showToast, onComplete) {
    // 记录本次使用的 LOT / SYNC 原始数据，供 onShow 返回页面时用缓存重新合并
    // （无需重新请求网络，即可把期间蓝牙缓存新增的记录时间计入"最后上报时间"）
    this._lastLotData = lotData
    this._lastSyncData = syncData
    const filteredList = this._buildMergedList(
      this._lastDeviceData,
      lotData,
      syncData,
      this._lastConfigMap
    )
    this._allDeviceList = filteredList
    this._applyCategoryFilter()
    // 列表就绪后启动倒计时刷新
    this._startCountdownTimer()
    if (showToast) {
      wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
    }
    if (onComplete) onComplete()
  },

  // 纯函数式：把 4 类原始数据合并为排序+过滤后的设备列表（不含 UI 副作用，可复用）
  _buildMergedList(deviceData, lotData, syncData, configMapData) {
    const lotMap = {}
    if (lotData && lotData.lotList) {
      lotData.lotList.forEach(rec => {
        if (rec.deviceId && rec.deviceId !== '-') {
          if (!lotMap[rec.deviceId]) lotMap[rec.deviceId] = rec
        }
      })
    }

    const syncMap = (syncData && syncData.syncMap) || {}

    // 蓝牙缓存中"每台设备最新一条记录"的时间：这些数据刚通过蓝牙收到、尚未上传到服务器，
    // LOT / 对时表里没有，需要一并作为"最后上报时间"的候选来源（读取失败不影响主流程）
    let bleLatestMap = {}
    try {
      bleLatestMap = bleManager.getLatestRecordByDevice() || {}
    } catch (e) {
      console.error('读取蓝牙缓存最新记录时间失败:', e)
      bleLatestMap = {}
    }

    // deviceData 可能为 null（网络失败且无本地缓存兜底时 getDeviceList 回传 null），此处做空值保护
    const deviceList = ((deviceData && deviceData.recordList) || []).map(item => {
      const lotRec = lotMap[item.deviceId]
      const syncInfo = syncMap[item.deviceId]

      // —— 最后上报时间：从三类来源取（LOT最新表 + 对时同步表 + 蓝牙缓存），取其中更晚的一次 ——
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

      // —— 第三个来源：蓝牙缓存 ——
      // 缓存里该设备最新一条记录的时间（尚未上传服务器），若比上述两表更晚也计为一次上报
      const bleRec = bleLatestMap[item.deviceId]
      if (bleRec && bleRec.ts && (isNaN(lastTs) || bleRec.ts > lastTs)) {
        lastTs = bleRec.ts
        lastRaw = bleRec.rawTime
        const bleType = bleRec.type
        lastType = (bleType === '1' || bleType === '5') ? 'gps' : (bleType === '2' ? 'time' : '')
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

      // 离线判定：从未上报 或 超过1个完整上报周期仍未上报（久未上报）
      // 注：中继设备处于休眠时段属正常现象，不计入离线
      const isOffline = !hasReport || cd.overdue

      // 注意：不使用对象展开 {...item}，展开会被增强编译转成 require('@babel/runtime/helpers/objectSpread2')
      // → 其内部 require('./defineProperty') 在小程序运行时会找不到该模块导致页面崩溃，改用 Object.assign 等价实现
      return Object.assign({}, item, {
        date: lastDate,
        time_part: lastTimePart,
        rawTime: lastRaw,
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
        nameColor: nameColor,
        isOffline: isOffline
      })
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
    return this.data.showAllDevices
      ? deviceList
      : deviceList.filter(item => item.visible === true)
  },

  // ========== 获取设备配置（工作时间判断休眠） ==========
  // 走 dataCache 缓存：forceRefresh=true 强制从网络刷新缓存；否则命中已缓存的 configMap（请求去重，多个页面同时请求只发一次）
  fetchDeviceConfigAll(forceRefresh, callback) {
    const that = this
    const handler = (configData) => {
      const configMap = (configData && configData.configMap) || {}
      that.setData({ deviceConfigMap: configMap })
      if (callback) callback(configMap)
    }
    if (forceRefresh) {
      dataCache.refreshDeviceConfigAll(handler)
    } else {
      dataCache.getDeviceConfigAll(handler, false)
    }
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

  // 按当前分类筛选并写入展示列表，同时刷新各分类数量
  _applyCategoryFilter() {
    const all = this._allDeviceList || []
    const nonRelayList = all.filter(item => !this._isRelay(item))
    const deviceCount = nonRelayList.length
    const relayCount = all.length - deviceCount
    const offlineCount = all.filter(item => item.isOffline).length
    const cat = this.data.activeCategory || 'device'
    let list
    if (cat === 'relay') list = all.filter(item => this._isRelay(item))
    else if (cat === 'offline') list = all.filter(item => item.isOffline)
    else {
      // "设备"分类：默认全部，点击"设备"再次切换为仅在线设备
      list = this.data.deviceOnlineOnly
        ? nonRelayList.filter(item => !item.isOffline)
        : nonRelayList
    }
    this.setData({
      deviceList: list,
      categoryCount: { all: all.length, device: deviceCount, relay: relayCount, offline: offlineCount }
    })
  },

  // 切换分类：设备 / 中继 / 离线
  // 再次点击已选中的"设备"：在 全部设备 / 仅在线设备 之间切换
  onSwitchCategory(e) {
    const cat = (e && e.currentTarget && e.currentTarget.dataset.cat) || 'device'
    if (cat === this.data.activeCategory) {
      if (cat === 'device') {
        const onlineOnly = !this.data.deviceOnlineOnly
        this.setData({ deviceOnlineOnly: onlineOnly })
        this._applyCategoryFilter()
        this._startCountdownTimer()
        wx.showToast({ title: onlineOnly ? '仅显示在线设备' : '显示全部设备', icon: 'none', duration: 1200 })
      }
      return
    }
    this.setData({ activeCategory: cat })
    this._applyCategoryFilter()
    // 重新按当前时间刷新一次倒计时（切换后索引变化）
    this._startCountdownTimer()
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
      url: getApp().globalData.api_device_Url,
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

  // ========== 点击设备：缓存选中并切回首页展示（与首页一致，不再进 device-detail 子页） ==========
  onTapDevice(e) {
    const deviceId = e.currentTarget.dataset.deviceid
    if (!deviceId) return

    // 缓存选中的设备，首页每次展示都恢复该设备的首页样式详情
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
