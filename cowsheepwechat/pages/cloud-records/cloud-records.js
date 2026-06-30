// cloud-records.js — 云端记录查看，卡片式列表参考蓝牙缓存数据样式
const API_URL = getApp().globalData.api_device_Url

Page({
  data: {
    loading: true,
    errorMsg: '',
    // 全部原始记录
    allRecords: [],
    // 当前筛选类型：''=全部, '1'=GPS, '2'=对时, '3'=电量
    filterType: '',
    // 筛选后显示的记录
    filteredRecords: []
  },

  onLoad() {
    this.fetchRecords()
  },

  // ========== 数据获取 ==========
  fetchRecords() {
    this.setData({ loading: true, errorMsg: '' })

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getlastlog',
        info: {
          limit: 100
        },
        time: getApp().formatTime()
      },
      success: (res) => {
        console.log('云端记录返回:', JSON.stringify(res.data))
        const records = this.parseRecordList(res.data)
        if (records.length === 0) {
          this.setData({
            loading: false,
            errorMsg: '暂无记录'
          })
          return
        }
        this.setData({
          allRecords: records,
          loading: false
        }, () => {
          this.applyFilter()
        })
      },
      fail: (err) => {
        console.error('获取云端记录失败:', err)
        this.setData({
          loading: false,
          errorMsg: '网络请求失败，请下拉重试'
        })
      }
    })
  },

  // ========== 记录解析 ==========
  parseRecordList(data) {
    let rawList = []
    if (data && data.data && Array.isArray(data.data)) {
      rawList = data.data
    } else if (Array.isArray(data)) {
      rawList = data
    }
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
      const deviceId = attr.deviceId || attr.deviceid || record.deviceId || record.deviceid || '-'
      const lorastr = attr.lorastr || record.lorastr || '-'
      const upDateDevice = attr.upDateDevice || attr.updatedevice || record.upDateDevice || record.updatedevice || '-'
      const rawTime = attr.time || record.time || '-'
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']
      const rssi = attr.rssi != null ? attr.rssi : (record.rssi != null ? record.rssi : '')
      const snr = attr.snr != null ? attr.snr : (record.snr != null ? record.snr : '')

      const display = this._buildDisplayParts(lorastr)

      return {
        deviceId,
        lorastr,
        upDateDevice,
        date: date || '-',
        time_part: time_part || '',
        rawTime,
        rssi,
        snr,
        msgType: display.msgType,
        displayParts: display.displayParts,
        bgColor: this._hashPastel(rawTime + '|' + lorastr)
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

  // 构建 lorastr 彩色分段
  // 7段电离格式: type|device|val1|val2|val3|val4|val5 → 黑|红|黑|红
  // 4段GPS格式:  type|tag|lat,lng|value               → 黑|红|黑|红
  // 旧3段格式:   type|gps|other                        → GPS全黑，其他首尾红中间绿
  _buildDisplayParts(lorastr) {
    const parts = lorastr.split('|')
    if (parts.length === 0) return { msgType: '', displayParts: [] }

    const typeStr = parts[0] || ''
    const displayParts = []
    const icon = this._getTypeIcon(typeStr, parts.length)
    if (icon) {
      displayParts.push({ text: icon.text + ' ', color: icon.color })
    }

    const isGps = typeStr === '1'
    const isTimeSync = typeStr === '2'
    const blackColor = '#333'
    const redColor = '#e74c3c'

    // === 7段电离格式: type|device|val1|val2|val3|val4|val5 ===
    // 3|v4-6|1.00|987|831|4.45|114 → 黑色|红色|黑色|黑色|黑色|黑色|红色
    if (parts.length === 7) {
      displayParts.push({ text: parts[0], color: blackColor, bold: true })
      displayParts.push({ text: '|', color: '#999' })
      displayParts.push({ text: parts[1], color: redColor, bold: true })
      displayParts.push({ text: '|', color: '#999' })
      // 中间4段合并显示为黑色
      const middle4 = parts.slice(2, 6).join('|')
      displayParts.push({ text: middle4, color: blackColor, bold: true })
      displayParts.push({ text: '|', color: '#999' })
      displayParts.push({ text: parts[6], color: redColor, bold: true })

      return { msgType: 'ionization', displayParts }
    }

    // === 4段GPS/对时格式: type|tag|val1|val2 → 黑|红|黑|红 ===
    // 1|device|lat,lng|value  （GPS）
    // 2|v3-12|2000/1/1 09:19:21|80 （对时）
    if ((isGps || isTimeSync) && parts.length === 4) {
      displayParts.push({ text: parts[0], color: blackColor, bold: true })
      displayParts.push({ text: '|', color: '#999' })
      displayParts.push({ text: parts[1], color: redColor, bold: true })
      displayParts.push({ text: '|', color: '#999' })
      displayParts.push({ text: parts[2], color: blackColor, bold: true })
      displayParts.push({ text: '|', color: '#999' })
      displayParts.push({ text: parts[3], color: redColor, bold: true })

      return { msgType: typeStr, displayParts }
    }

    // === 旧3段格式 ===
    displayParts.push({ text: typeStr, color: isGps ? blackColor : '#e74c3c', bold: true })

    if (parts.length > 1) {
      const middle = parts.slice(1, parts.length - 1).join('|')
      const tail = parts[parts.length - 1]
      const midColor = isGps ? blackColor : '#07c160'
      const tailColor = isGps ? blackColor : '#e74c3c'
      displayParts.push({ text: '|' + middle + '|', color: midColor, bold: true })
      displayParts.push({ text: tail, color: tailColor, bold: true })
    }

    return { msgType: typeStr, displayParts }
  },

  // 根据类型编号返回图标（segmentCount 用于区分电离7段格式）
  _getTypeIcon(typeStr, segmentCount) {
    // 7段格式为电离信息
    if (segmentCount === 7) return { text: '⚡', color: '#ff6600' }
    if (typeStr === '1') return { text: '◉', color: '#1989fa' }   // GPS定位
    if (typeStr === '2') return { text: '🕐', color: '#666' }       // 对时
    if (typeStr === '3') return { text: '🔋', color: '#07c160' }    // 电量
    return null
  },

  // 稳定浅色背景：基于字符串哈希生成色相
  _hashPastel(str) {
    let h = 0
    for (let i = 0; i < str.length; i++) {
      h = (h * 31 + str.charCodeAt(i)) % 360
    }
    const s = 30 + (h % 20)
    const l = 88 + (h % 8)
    return `hsl(${h}, ${s}%, ${l}%)`
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
    if (type === this.data.filterType) return
    this.setData({ filterType: type }, () => {
      this.applyFilter()
    })
  },

  // ========== 下拉刷新 ==========
  onPullDownRefresh() {
    this.fetchRecords()
    setTimeout(() => {
      wx.stopPullDownRefresh()
    }, 1500)
  },

  // ========== 重试按钮 ==========
  onRetry() {
    this.fetchRecords()
  }
})
