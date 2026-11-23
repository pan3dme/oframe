// cloud-records.js — 云端记录查看，数据列表排版参照设备详情页
const API_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')

// 各筛选项对应的 getlastlog type 参数：GPS记录 type=2，对时记录 type=1，全部不带 type 属性
const FILTER_TYPE_PARAMS = {
  all: null, // 全部：不带 type 属性
  gps: 1,    // GPS：type=1
  sync: 2    // 对时：type=2
}

Page({
  data: {
    loading: true,
    errorMsg: '',
    refreshing: false,
    // 当前筛选选项：'all'=全部, 'gps'=GPS记录, 'sync'=对时记录
    filterType: 'all',
    // 当前筛选下显示的记录
    filteredRecords: [],
    // 三个选项独立的数据与分页状态（独立显示内容、独立加载更多）
    recordsByType: {
      all: { list: [], page: 0, hasMore: true, loaded: false },
      gps: { list: [], page: 0, hasMore: true, loaded: false },
      sync: { list: [], page: 0, hasMore: true, loaded: false }
    },
    // 当前选项是否已完成首次加载
    tabLoaded: false,
    // 当前选项是否还有更多数据
    hasMore: true,
    loadingMore: false,
    // 分页大小
    pageSize: 10
  },

  onLoad() {
    const that = this
    // 构建 设备id -> 别名 映射，用于记录列表显示上传设备别名（与设备详情一致）
    dataCache.getDeviceList((deviceData) => {
      const renameMap = {}
      if (deviceData && deviceData.recordList) {
        deviceData.recordList.forEach(v => {
          if (v.deviceId) renameMap[v.deviceId] = v.rename || ''
        })
      }
      that._deviceRenameMap = renameMap
      // 若记录已先返回，则补充别名后刷新各选项数据
      const byType = that.data.recordsByType
      let updated = null
      Object.keys(byType).forEach(k => {
        if (byType[k].list.length > 0) {
          if (!updated) updated = Object.assign({}, byType)
          updated[k] = Object.assign({}, byType[k], {
            list: byType[k].list.map(r => Object.assign({}, r, {
              upDateDeviceAlias: renameMap[r.upDateDevice] || ''
            }))
          })
        }
      })
      if (updated) {
        that.setData({ recordsByType: updated }, () => that.refreshCurrentTabView())
      }
    })
    this.fetchRecords(false, 0, false)
  },

  // 将当前选项对应的数据同步到显示列表
  refreshCurrentTabView() {
    const tab = this.data.recordsByType[this.data.filterType]
    this.setData({
      filteredRecords: tab ? tab.list : [],
      tabLoaded: tab ? tab.loaded : false,
      hasMore: tab ? tab.hasMore : true
    })
  },

  // ========== 数据获取 ==========
  // silent=true 用于下拉刷新/切换选项：不重置错误提示
  // page: 指定页码，不传时默认第1页
  // append: true=追加到已有列表（加载更多），false/不传=替换列表
  // 根据当前筛选选项决定是否携带 type 参数：GPS记录 type=2，对时记录 type=1，全部不带 type 属性
  fetchRecords(silent = false, page = 0, append = false) {
    const tabKey = this.data.filterType
    const typeParam = FILTER_TYPE_PARAMS[tabKey]
    if (append) {
      this.setData({ loadingMore: true })
    } else if (!silent) {
      this.setData({ tabLoaded: false, errorMsg: '' })
    } else {
      this.setData({ errorMsg: '' })
    }

    const info = {
      page: page,
      limit: this.data.pageSize,
      wechatid: getApp().getWechatId()
    }
    // 全部时不传 type 属性
    if (typeParam != null) {
      info.type = typeParam
    }
     
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getlastlog',
        info: info,
        time: getApp().formatTime()
      },
      success: (res) => {
        console.log('云端记录返回(type=' + tabKey + ', page=' + page + '):', JSON.stringify(res.data))
        const records = this.parseRecordList(res.data)
        const hasMore = records.length >= this.data.pageSize
        const tab = this.data.recordsByType[tabKey]
        let newList
        if (append) {
          // 加载更多：合并去重（各选项独立）
          const existKeys = new Set(tab.list.map(r => r.rawTime + '|' + r.lorastr))
          const newRecords = records.filter(r => !existKeys.has(r.rawTime + '|' + r.lorastr))
          if (newRecords.length === 0) {
            this.setData({
              loadingMore: false,
              hasMore: false,
              ['recordsByType.' + tabKey + '.hasMore']: false
            })
            return
          }
          newList = [...tab.list, ...newRecords]
        } else {
          newList = records
        }

        const patch = {
          ['recordsByType.' + tabKey]: { list: newList, page: page, hasMore: hasMore, loaded: true },
          refreshing: false,
          loadingMore: false
        }
        // 请求返回时若用户已切换到其它选项，仅更新该选项缓存，不改动当前显示
        if (tabKey === this.data.filterType) {
          patch.filteredRecords = newList
          patch.tabLoaded = true
          patch.hasMore = hasMore
          patch.errorMsg = ''
        }
        this.setData(patch)
      },
      fail: (err) => {
        console.error('获取云端记录失败:', err)
        this.setData({
          loading: false,
          refreshing: false,
          loadingMore: false
        })
        if (append) {
          wx.showToast({ title: '加载失败', icon: 'none' })
        } else if (silent && this.data.recordsByType[tabKey].loaded) {
          wx.showToast({ title: '刷新失败', icon: 'none' })
        } else {
          this.setData({ tabLoaded: true, errorMsg: '网络请求失败，请下拉重试' })
        }
      }
    })
  },

  // ========== 记录解析（与设备详情页 _parseRecords 一致） ==========
  parseRecordList(data) {
    let rawList = []
    if (data && data.data && Array.isArray(data.data)) {
      rawList = data.data
    } else if (Array.isArray(data)) {
      rawList = data
    }
    const records = rawList.map((record, idx) => {
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
      const deviceId = attr.deviceId || attr.deviceid || record.deviceId || record.deviceid || '-'
      const upDateDevice = attr.upDateDevice || attr.updatedevice || record.upDateDevice || record.updatedevice || '-'
      const upDateDeviceAlias = (this._deviceRenameMap && this._deviceRenameMap[upDateDevice]) || ''
      const lorastr = attr.lorastr || record.lorastr || '-'
      const rawTime = attr.time || record.time || '-'
      const rssi = attr.rssi != null ? attr.rssi : (record.rssi != null ? record.rssi : '')
      const snr = attr.snr != null ? attr.snr : (record.snr != null ? record.snr : '')
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']

      // 如果 rssi/snr 为空，尝试从 lorastr 末尾段提取
      let finalRssi = rssi
      let finalSnr = snr
      if (lorastr && lorastr !== '-') {
        const parts = lorastr.split('|')
        if (parts.length >= 2) {
          const lastPart = parts[parts.length - 1]
          const secLastPart = parts[parts.length - 2]
          if (finalRssi === '' && /^-?\d+$/.test(lastPart)) {
            finalRssi = lastPart
          }
          if (finalSnr === '' && /^-?\d+(\.\d+)?$/.test(secLastPart)) {
            finalSnr = secLastPart
          }
        }
      }

      // 解析 lorastr 类型：格式为 type|deviceId|data
      // 1=定位  2=对时  3=电量  5=跟踪  6=设置
      let msgType = '-'
      if (lorastr && lorastr !== '-') {
        const parts = lorastr.split('|')
        msgType = parts[0] || '-'
      }

      return {
        _key: rawTime + '_' + idx,
        deviceId,
        upDateDevice,
        upDateDeviceAlias,
        lorastr,
        displayLorastr: lorastr,
        msgType,
        rssi: finalRssi,
        snr: finalSnr,
        date: date || '-',
        time_part: time_part || '',
        rawTime,
        bgColor: this._devicePastel(upDateDevice),
        deviceColor: this._deviceColor(upDateDevice)
      }
    })
    // 按时间降序排列，最新的在最上面
    records.sort((a, b) => {
      const ta = new Date(a.rawTime).getTime()
      const tb = new Date(b.rawTime).getTime()
      if (isNaN(ta) && isNaN(tb)) return 0
      if (isNaN(ta)) return 1
      if (isNaN(tb)) return -1
      return tb - ta
    })
    return records
  },

  // 按设备名生成稳定的浅色背景色：同一设备始终同色，不同设备不同色（与设备详情一致）
  _devicePastel(deviceName) {
    if (!deviceName || deviceName === '-') return 'hsl(0, 0%, 95%)'
    let h = 0
    for (let i = 0; i < deviceName.length; i++) {
      h = (h * 31 + deviceName.charCodeAt(i)) % 360
    }
    const s = 35 + (h % 15)
    const l = 86 + (h % 10)
    return `hsl(${h}, ${s}%, ${l}%)`
  },

  // 按 upDateDevice 生成稳定文字颜色：同一设备始终同色，不同设备分配鲜艳颜色
  _deviceColor(deviceName) {
    if (!deviceName || deviceName === '-') return '#999'
    const vividColors = [
      '#E53935', '#1E88E5', '#43A047', '#FB8C00', '#8E24AA',
      '#00ACC1', '#F4511E', '#D81B60', '#5E35B1', '#039BE5',
      '#2E7D32', '#C0CA33',
    ]
    let idx = 0
    for (let i = 0; i < deviceName.length; i++) {
      idx = (idx * 31 + deviceName.charCodeAt(i)) % vividColors.length
    }
    return vividColors[idx]
  },

  // ========== 筛选：三个选项独立显示内容 ==========
  onFilterTap(e) {
    const type = e.currentTarget.dataset.type
    if (type === this.data.filterType) return
    const tab = this.data.recordsByType[type]
    if (!tab) return
    if (tab.loaded) {
      // 已加载过：直接切换显示该选项的独立内容（保留各自的分页进度）
      this.setData({ filterType: type, errorMsg: '' })
      this.refreshCurrentTabView()
    } else {
      // 未加载过：切换后按类型请求（GPS type=2，对时 type=1）
      this.setData({
        filterType: type,
        filteredRecords: [],
        tabLoaded: false,
        hasMore: true,
        errorMsg: ''
      })
      this.fetchRecords(true, 0, false)
    }
  },

  // ========== 下拉刷新（scroll-view 内置）：刷新当前选项 ==========
  onScrollRefresh() {
    const tabKey = this.data.filterType
    const patch = { refreshing: true }
    patch['recordsByType.' + tabKey + '.hasMore'] = true
    this.setData(patch)
    this.fetchRecords(true, 0, false)
  },

  // ========== 触底加载下一页：按当前选项各自的分页进度 ==========
  onScrollToLower() {
    if (this.data.loadingMore || !this.data.hasMore) return
    const tabKey = this.data.filterType
    const nextPage = this.data.recordsByType[tabKey].page + 1
    this.fetchRecords(true, nextPage, true)
  },

  // ========== 重试按钮 ==========
  onRetry() {
    const tabKey = this.data.filterType
    const patch = {}
    patch['recordsByType.' + tabKey + '.hasMore'] = true
    this.setData(patch)
    this.fetchRecords(false, 0, false)
  },

  // ========== 点击记录跳转地图详情（与设备详情页 onRecordTap 一致） ==========
  // 只有定位(msgType=1)和跟踪(msgType=5)记录可点击，跳转到定位地图页
  onRecordTap(e) {
    const index = e.currentTarget.dataset.index
    const record = this.data.filteredRecords[index]
    if (!record) return
    // 定位(1)和跟踪(5)才响应点击
    if (record.msgType !== '1' && record.msgType !== '5') return

    // 从 lorastr 中提取 GPS 坐标：格式 type|deviceId|lat,lng|...
    let lat = null, lng = null
    if (record.lorastr && record.lorastr !== '-') {
      const segs = record.lorastr.split(/[｜|]/)
      if (segs.length >= 3 && segs[2]) {
        const parts = segs[2].split(/[,，]\s*/)
        if (parts.length >= 2) {
          lat = parseFloat(parts[0])
          lng = parseFloat(parts[1])
        }
      }
    }
    if (isNaN(lat) || isNaN(lng) || (lat === 0 && lng === 0)) {
      wx.showToast({ title: '该记录无有效坐标', icon: 'none' })
      return
    }

    // 跳转到定位地图页
    wx.navigateTo({
      url: '/pages/location-map/location-map' +
        '?lat=' + lat +
        '&lng=' + lng +
        '&deviceId=' + encodeURIComponent(record.deviceId || '') +
        '&time=' + encodeURIComponent(record.rawTime || '') +
        '&lorastr=' + encodeURIComponent(record.lorastr || '') +
        '&upDateDevice=' + encodeURIComponent(record.upDateDevice || '')
    })
  }
})
