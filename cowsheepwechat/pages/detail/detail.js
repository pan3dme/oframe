// detail.js
const API_URL = 'https://device-updata-puknouxjhg.cn-shanghai.fcapp.run'

Page({
  data: {
    name: '',
    cowsheepId: '',
    birthday: '',
    gender: '',
    avatar: '',
    deviceList: [],
    deviceIdOptions: [],        // 下拉选项列表
    selectedDeviceId: '',       // 当前选中的设备ID
    selectedDeviceIndex: 0,     // 当前选中索引
    deviceBindMap: {},          // deviceId -> link_cowsheep_id 映射
    mediaList: [],              // 图像/视频列表
    showMedia: false,           // 是否显示媒体区域
    mediaLoading: false         // 加载中
  },

  onLoad(options) {
    this.setData({
      name: decodeURIComponent(options.name || ''),
      cowsheepId: decodeURIComponent(options.cowsheepId || ''),
      birthday: decodeURIComponent(options.birthday || ''),
      gender: decodeURIComponent(options.gender || ''),
      avatar: decodeURIComponent(options.avatar || '')
    })

    // 初始化时加载所有设备信息
    this.loadDeviceInfo()
  },

  // 加载所有设备信息
  loadDeviceInfo() {
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceTaleAll'
      },
      success: (res) => {
        console.log('======== 设备所有数据 ========')
        console.log(JSON.stringify(res.data, null, 2))
        console.log('==============================')
        const recordList = this.parseRecordList(res.data)
        // 提取去重 deviceId 作为下拉选项，并构建绑定映射
        const idSet = new Set()
        const bindMap = {}
        recordList.forEach(r => {
          if (r.deviceId && r.deviceId !== '-') {
            idSet.add(r.deviceId)
            // 取最新的 link_cowsheep_id（记录已按时间降序，首次遇到的即最新）
            if (!bindMap[r.deviceId] && r.link_cowsheep_id) {
              bindMap[r.deviceId] = r.link_cowsheep_id
            }
          }
        })
        const deviceIdOptions = Array.from(idSet).sort()
        // 在选项最前面插入"未连接"
        deviceIdOptions.unshift('未连接')

        // 查找是否有设备已绑定当前牛羊
        let boundDeviceId = ''
        let selectedIndex = 0
        for (const [devId, linkId] of Object.entries(bindMap)) {
          if (linkId === this.data.cowsheepId) {
            boundDeviceId = devId
            break
          }
        }
        if (boundDeviceId) {
          selectedIndex = deviceIdOptions.indexOf(boundDeviceId)
          if (selectedIndex < 0) selectedIndex = 0
        }

        this.setData({
          deviceList: recordList,
          deviceIdOptions,
          deviceBindMap: bindMap,
          selectedDeviceId: boundDeviceId || '未连接',
          selectedDeviceIndex: selectedIndex
        })
      },
      fail: (err) => {
        console.error('获取设备信息失败:', err)
      }
    })
  },

  // 设备下拉选择变更 — 更新选中并提交绑定
  onDeviceChange(e) {
    const index = e.detail.value
    const deviceId = this.data.deviceIdOptions[index] || ''
    this.setData({
      selectedDeviceIndex: index,
      selectedDeviceId: deviceId
    })
    console.log('选中设备:', deviceId)

    // 选择了"未连接"，不提交
    if (index === 0 || deviceId === '未连接') {
      console.log('未选择设备，跳过绑定')
      return
    }

    // 如果当前设备已绑定当前牛羊，无需重复提交
    const currentBind = this.data.deviceBindMap[deviceId]
    if (currentBind === this.data.cowsheepId) {
      console.log('设备[' + deviceId + ']已绑定当前牛羊，跳过提交')
      return
    }

    // 向服务器提交绑定：将设备的 link_cowsheep_id 更新为当前牛羊编号
    wx.showLoading({ title: '绑定中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'bindDeviceCow',
        info: {
          deviceId: deviceId,
          cowsheepId: this.data.cowsheepId
        }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('======== 设备绑定返回 ========')
        console.log('res:', JSON.stringify(res.data, null, 2))
        console.log('==============================')
        // 兼容字符串类型返回
        let result = res.data
        if (typeof result === 'string') {
          try { result = JSON.parse(result) } catch (e) { }
        }
        if (result && result.status === 'success') {
          wx.showToast({ title: result.msg || '绑定成功', icon: 'success', duration: 1500 })
          // 更新本地绑定映射
          const newBindMap = { ...this.data.deviceBindMap }
          newBindMap[deviceId] = this.data.cowsheepId
          this.setData({ deviceBindMap: newBindMap })
        } else {
          wx.showToast({ title: (result && result.msg) || '绑定失败', icon: 'none', duration: 2500 })
        }
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('设备绑定请求失败:', err)
        wx.showToast({ title: '网络请求失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // 点击获取更多详细图像
  fetchMedia() {
    if (this.data.mediaLoading) return
    this.setData({ mediaLoading: true })
    wx.showLoading({ title: '加载中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getcowsheepvideo',
        info: {
          cowsheep_id: this.data.cowsheepId
        }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('======== 媒体列表返回 ========')
        console.log(JSON.stringify(res.data, null, 2))
        
 
        console.log('==============================')
        const mediaList = this.parseMediaList(res.data)
        this.setData({
          mediaList,
          showMedia: true,
          mediaLoading: false
        })
        if (mediaList.length === 0) {
          wx.showToast({ title: '暂无更多图像', icon: 'none', duration: 1500 })
        }
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('获取媒体列表失败:', err)
        this.setData({ mediaLoading: false })
        wx.showToast({ title: '加载失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // 解析 cowsheep_video 表返回数据
  parseMediaList(data) {
    let rawList = []
    if (data && data.data && Array.isArray(data.data)) {
      rawList = data.data
    } else if (Array.isArray(data)) {
      rawList = data
    }
    const mediaList = rawList.map(record => {
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
      const ossurl = attr.ossurl || record.ossurl || ''
      const time = attr.time || record.time || ''
      const video_id = attr.video_id || record.video_id || ''
      // 判断文件类型
      const lower = ossurl.toLowerCase()
      const isVideo = lower.endsWith('.mp4') || lower.endsWith('.mov') || lower.endsWith('.avi')
      const isImage = lower.endsWith('.jpg') || lower.endsWith('.jpeg') ||
                      lower.endsWith('.png') || lower.endsWith('.gif') || lower.endsWith('.webp')
      const type = isVideo ? 'video' : (isImage ? 'image' : 'unknown')
      return { ossurl, time, video_id, type }
    })
    // 按时间降序
    mediaList.sort((a, b) => {
      const ta = new Date(a.time).getTime()
      const tb = new Date(b.time).getTime()
      if (isNaN(ta) && isNaN(tb)) return 0
      if (isNaN(ta)) return 1
      if (isNaN(tb)) return -1
      return tb - ta
    })
    return mediaList
  },

  // 预览媒体
  onPreviewMedia(e) {
    const index = e.currentTarget.dataset.index
    const item = this.data.mediaList[index]
    if (!item || !item.ossurl) return
    if (item.type === 'image') {
      // 收集所有图片 URL 用于预览
      const urls = this.data.mediaList.filter(m => m.type === 'image').map(m => m.ossurl)
      wx.previewImage({
        current: item.ossurl,
        urls: urls,
        success: () => {},
        fail: (err) => {
          console.error('预览图片失败:', err)
          wx.showToast({ title: '预览失败', icon: 'none' })
        }
      })
    } else if (item.type === 'video') {
      // 复制视频链接
      wx.setClipboardData({
        data: item.ossurl,
        success: () => {
          wx.showToast({ title: '视频链接已复制', icon: 'success' })
        }
      })
    }
  },

  // 统一解析接口返回数据，提取 deviceId / lorastr / time
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
      const link_cowsheep_id = attr.link_cowsheep_id || record.link_cowsheep_id || ''
      const rawTime = attr.time || record.time || '-'
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']
      return {
        deviceId,
        lorastr,
        link_cowsheep_id,
        date: date || '-',
        time_part: time_part || '',
        rawTime
      }
    })
    // 按时间降序排列
    records.sort((a, b) => {
      const ta = new Date(a.rawTime).getTime()
      const tb = new Date(b.rawTime).getTime()
      if (isNaN(ta) && isNaN(tb)) return 0
      if (isNaN(ta)) return 1
      if (isNaN(tb)) return -1
      return tb - ta
    })
    return records
  }
})
