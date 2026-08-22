// cloud-records.js — 云端记录查看，数据列表排版参照设备详情页
const API_URL = getApp().globalData.api_device_Url
const dataCache = require('../../config/data-cache.js')

Page({
  data: {
    loading: true,
    errorMsg: '',
    refreshing: false,
    // 全部原始记录
    allRecords: [],
    // 当前筛选类型：''=全部, '1'=GPS, '2'=对时, '3'=电量
    filterType: '',
    // 筛选后显示的记录
    filteredRecords: [],
    // 分页相关
    currentPage: 0,
    pageSize: 10,
    hasMore: true,
    loadingMore: false
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
      // 若记录已先返回，则补充别名后刷新显示
      if (that.data.allRecords.length > 0) {
        const updated = that.data.allRecords.map(r => Object.assign({}, r, {
          upDateDeviceAlias: renameMap[r.upDateDevice] || ''
        }))
        that.setData({ allRecords: updated }, () => that.applyFilter())
      }
    })
    this.fetchRecords()
  },

  // ========== 数据获取 ==========
  // silent=true 用于下拉刷新：不显示全屏 loading，避免顶部筛选栏闪动
  // page: 指定页码，不传时默认第1页
  // append: true=追加到已有列表（加载更多），false/不传=替换列表
  fetchRecords(silent = false, page = 0, append = false) {
    if (!silent && !append) {
      this.setData({ loading: true, errorMsg: '' })
    }
    if (append) {
      this.setData({ loadingMore: true })
    }

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getlastlog',
        info: {
          page: page,
          limit: this.data.pageSize
        },
        time: getApp().formatTime()
      },
      success: (res) => {
        console.log('云端记录返回(page=' + page + '):', JSON.stringify(res.data))
        const records = this.parseRecordList(res.data)
        const hasMore = records.length >= this.data.pageSize

        if (append) {
          // 加载更多：合并去重
          const existing = this.data.allRecords
          const existKeys = new Set(existing.map(r => r.rawTime + '|' + r.lorastr))
          const newRecords = records.filter(r => !existKeys.has(r.rawTime + '|' + r.lorastr))

          if (newRecords.length === 0) {
            this.setData({ loadingMore: false, hasMore: false })
            return
          }
          const merged = [...existing, ...newRecords]
          this.setData({
            allRecords: merged,
            currentPage: page,
            hasMore: hasMore,
            loadingMore: false
          }, () => {
            this.applyFilter()
          })
        } else {
          // 首次加载或刷新
          if (records.length === 0) {
            this.setData({
              loading: false,
              refreshing: false,
              allRecords: [],
              filteredRecords: [],
              currentPage: page,
              hasMore: false,
              loadingMore: false,
              errorMsg: silent ? '' : '暂无记录'
            })
            return
          }
          this.setData({
            allRecords: records,
            loading: false,
            errorMsg: '',
            refreshing: false,
            currentPage: page,
            hasMore: hasMore,
            loadingMore: false
          }, () => {
            this.applyFilter()
          })
        }
      },
      fail: (err) => {
        console.error('获取云端记录失败:', err)
        this.setData({
          loading: false,
          refreshing: false,
          loadingMore: false
        })
        if (silent) {
          wx.showToast({ title: '刷新失败', icon: 'none' })
        } else if (append) {
          wx.showToast({ title: '加载失败', icon: 'none' })
        } else {
          this.setData({ errorMsg: '网络请求失败，请下拉重试' })
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

  // ========== 筛选 ==========
  applyFilter() {
    const type = this.data.filterType
    const filtered = type
      ? this.data.allRecords.filter(item => item.msgType === type)
      : this.data.allRecords.slice()
    this.setData({ filteredRecords: filtered })
  },

  onFilterTap(e) {
    const type = e.currentTarget.dataset.type
    // 点同一个按钮时切换回全部
    if (type === this.data.filterType) {
      this.setData({ filterType: '' }, () => {
        this.applyFilter()
      })
      return
    }
    this.setData({ filterType: type }, () => {
      this.applyFilter()
    })
  },

  // ========== 下拉刷新（scroll-view 内置） ==========
  onScrollRefresh() {
    this.setData({ refreshing: true, currentPage: 0, hasMore: true })
    this.fetchRecords(true, 0, false)
  },

  // ========== 触底加载下一页 ==========
  onScrollToLower() {
    if (this.data.loadingMore || !this.data.hasMore) return
    const nextPage = this.data.currentPage + 1
    this.fetchRecords(true, nextPage, true)
  },

  // ========== 重试按钮 ==========
  onRetry() {
    this.setData({ currentPage: 0, hasMore: true })
    this.fetchRecords(false, 0, false)
  }
})
