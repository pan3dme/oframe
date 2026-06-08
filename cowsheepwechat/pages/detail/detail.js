// detail.js
const API_URL = 'https://device-updata-puknouxjhg.cn-shanghai.fcapp.run'
const OSS_CONFIG = require('../../config/oss-config.js')
const dataCache = require('../../config/data-cache.js')

// ==================== 时间格式化 ====================
function formatDate(date) {
  const y = date.getFullYear()
  const m = date.getMonth() + 1
  const d = date.getDate()
  const h = date.getHours()
  const min = date.getMinutes()
  const s = date.getSeconds()
  const pad = (n) => n < 10 ? '0' + n : n
  return y + '/' + m + '/' + d + ' ' + pad(h) + ':' + pad(min) + ':' + pad(s)
}

// ==================== SHA1 / HMAC-SHA1 纯 JS 实现 ====================
function _sha1Core(msgBytes) {
  const rotl = (n, s) => (n << s) | (n >>> (32 - s))
  const len = msgBytes.length * 8
  const blocks = []
  for (let i = 0; i < msgBytes.length; i += 4) {
    blocks[i >> 2] = (msgBytes[i] << 24) | (msgBytes[i + 1] << 16) | (msgBytes[i + 2] << 8) | msgBytes[i + 3]
  }
  blocks[len >> 5] |= 0x80 << (24 - (len % 32))
  blocks[((len + 64 >> 9) << 4) + 15] = len
  let h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0
  for (let i = 0; i < blocks.length; i += 16) {
    let a = h0, b = h1, c = h2, d = h3, e = h4
    const w = []
    for (let j = 0; j < 80; j++) {
      w[j] = j < 16 ? (blocks[i + j] || 0) : rotl(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1)
      let f, k
      if (j < 20) { f = (b & c) | (~b & d); k = 0x5A827999 }
      else if (j < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1 }
      else if (j < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC }
      else { f = b ^ c ^ d; k = 0xCA62C1D6 }
      const temp = (rotl(a, 5) + f + e + k + (w[j] >>> 0)) >>> 0
      e = d; d = c; c = rotl(b, 30); b = a; a = temp
    }
    h0 = (h0 + a) >>> 0; h1 = (h1 + b) >>> 0; h2 = (h2 + c) >>> 0
    h3 = (h3 + d) >>> 0; h4 = (h4 + e) >>> 0
  }
  const buf = new ArrayBuffer(20), dv = new DataView(buf)
  dv.setUint32(0, h0); dv.setUint32(4, h1); dv.setUint32(8, h2)
  dv.setUint32(12, h3); dv.setUint32(16, h4)
  return buf
}

function _strToBytes(str) {
  const bytes = new Uint8Array(str.length)
  for (let i = 0; i < str.length; i++) bytes[i] = str.charCodeAt(i) & 0xFF
  return bytes
}

function _hmacSha1(key, msg) {
  const blockSize = 64
  let keyBytes = _strToBytes(key)
  if (keyBytes.length > blockSize) {
    keyBytes = new Uint8Array(_sha1Core(keyBytes))
  }
  const padded = new Uint8Array(blockSize)
  padded.set(keyBytes)
  const ipad = new Uint8Array(blockSize), opad = new Uint8Array(blockSize)
  for (let i = 0; i < blockSize; i++) {
    ipad[i] = padded[i] ^ 0x36
    opad[i] = padded[i] ^ 0x5C
  }
  const inner = _sha1Core(_concatBytes(ipad, _strToBytes(msg)))
  return _sha1Core(_concatBytes(opad, new Uint8Array(inner)))
}

function _concatBytes(a, b) {
  const c = new Uint8Array(a.length + b.length)
  c.set(a); c.set(b, a.length); return c
}

function _base64(arrayBuffer) {
  return wx.arrayBufferToBase64(arrayBuffer)
}

// ==================== OSS 上传 ====================
function uploadToOSS(filePath, objectKey) {
  return new Promise((resolve, reject) => {
    const { region, bucket, accessKeyId, accessKeySecret } = OSS_CONFIG
    const host = `https://${bucket}.${region}.aliyuncs.com/`

    const expire = new Date(Date.now() + 86400000).toISOString()
    const policyObj = {
      expiration: expire,
      conditions: [
        { bucket: bucket },
        ['starts-with', '$key', OSS_CONFIG.uploadDir],
        { 'x-oss-object-acl': 'public-read' },
        ['content-length-range', 0, 104857600]
      ]
    }
    const policyStr = JSON.stringify(policyObj)
    const policyBase64 = _base64(_strToBytes(policyStr).buffer)
    const signature = _base64(_hmacSha1(accessKeySecret, policyBase64))

    wx.uploadFile({
      url: host,
      filePath: filePath,
      name: 'file',
      formData: {
        key: objectKey,
        policy: policyBase64,
        OSSAccessKeyId: accessKeyId,
        signature: signature,
        'x-oss-object-acl': 'public-read',
        success_action_status: '200'
      },
      success: (res) => {
        if (res.statusCode === 200 || res.statusCode === 204) {
          resolve(host + objectKey)
        } else {
          reject(new Error('OSS 返回 ' + res.statusCode))
        }
      },
      fail: (err) => {
        reject(err)
      }
    })
  })
}

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
    mediaLoading: false,        // 加载中
    editMode: false,            // 编辑模式（显示复选框）
    selectedCount: 0,           // 选中数量
    showDeleteModal: false,     // 删除确认弹窗
    currentVideo: '',           // 当前播放视频 URL
    showVideoPlayer: false,     // 视频播放弹窗
    showUploadModal: false,     // 上传媒体弹窗
    uploadFilePath: '',         // 选中的文件路径
    uploadFileType: ''          // 'image' | 'video'
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

  // 加载所有设备信息（优先使用缓存）
  loadDeviceInfo() {
    dataCache.getDeviceList((cachedData) => {
      const { recordList, deviceIdOptions, deviceBindMap } = cachedData

      // 查找是否有设备已绑定当前牛羊
      let boundDeviceId = ''
      let selectedIndex = 0
      for (const [devId, linkId] of Object.entries(deviceBindMap)) {
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
        deviceBindMap,
        selectedDeviceId: boundDeviceId || '未连接',
        selectedDeviceIndex: selectedIndex
      })
    })
  },

  // 强制刷新设备列表
  refreshDeviceInfo() {
    dataCache.refreshDeviceList((cachedData) => {
      const { recordList, deviceIdOptions, deviceBindMap } = cachedData

      let boundDeviceId = ''
      let selectedIndex = 0
      for (const [devId, linkId] of Object.entries(deviceBindMap)) {
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
        deviceBindMap,
        selectedDeviceId: boundDeviceId || '未连接',
        selectedDeviceIndex: selectedIndex
      })
      wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
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
      return { ossurl, time, video_id, type, selected: false }
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
    // 编辑模式下不预览，走勾选逻辑
    if (this.data.editMode) {
      this.onToggleSelect(e)
      return
    }
    const index = e.currentTarget.dataset.index
    const item = this.data.mediaList[index]
    if (!item || !item.ossurl) return
    if (item.type === 'image') {
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
      this.setData({ currentVideo: item.ossurl, showVideoPlayer: true })
    }
  },

  // ========== 编辑/删除媒体 ==========
  onToggleEditMode() {
    if (this.data.editMode) {
      // 退出编辑，清空所有勾选
      const list = this.data.mediaList.map(item => ({ ...item, selected: false }))
      this.setData({ editMode: false, mediaList: list, selectedCount: 0 })
    } else {
      const list = this.data.mediaList.map(item => ({ ...item, selected: false }))
      this.setData({ editMode: true, mediaList: list, selectedCount: 0 })
    }
  },

  onToggleSelect(e) {
    const index = e.currentTarget.dataset.index
    if (index === undefined || index === null) return

    const selected = !this.data.mediaList[index].selected
    const key = 'mediaList[' + index + '].selected'
    const newCount = selected ? this.data.selectedCount + 1 : this.data.selectedCount - 1
    this.setData({
      [key]: selected,
      selectedCount: newCount
    })
  },

  onDeleteMedia() {
    const count = this.data.mediaList.filter(item => item.selected).length
    if (count === 0) {
      wx.showToast({ title: '请先选择要删除的图像', icon: 'none' })
      return
    }
    this.setData({ showDeleteModal: true })
  },

  onDeleteClose() {
    this.setData({ showDeleteModal: false })
  },

  onCloseVideo() {
    this.setData({ showVideoPlayer: false, currentVideo: '' })
  },

  onDeleteConfirm() {
    const ossUrls = this.data.mediaList.filter(item => item.selected).map(item => item.ossurl)
    if (ossUrls.length === 0) return

    this.setData({ showDeleteModal: false })
    wx.showLoading({ title: '删除中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'deletecowsheepvideo',
        info: { oss_urls: ossUrls }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('=== deletecowsheepvideo 返回 ===')
        console.log(JSON.stringify(res, null, 2))
        let result = res.data
        if (typeof result === 'string') {
          try { result = JSON.parse(result) } catch (e) {}
        }
        if (result && result.status === 'success') {
          wx.showToast({ title: result.msg || '删除成功', icon: 'success', duration: 1500 })
          this.setData({ editMode: false, selectedCount: 0 })
          this.fetchMedia()
        } else {
          wx.showToast({ title: (result && result.msg) || '删除失败', icon: 'none', duration: 2000 })
        }
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('删除媒体失败:', err)
        wx.showToast({ title: '网络请求失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // ========== 上传媒体弹窗 ==========
  onUploadPhoto() {
    this.setData({
      showUploadModal: true,
      uploadFilePath: '',
      uploadFileType: ''
    })
  },

  onUploadChooseMedia() {
    wx.chooseMedia({
      count: 1,
      mediaType: ['image', 'video'],
      sourceType: ['album', 'camera'],
      sizeType: ['compressed'],
      success: (res) => {
        const file = res.tempFiles[0]
        this.setData({
          uploadFilePath: file.tempFilePath,
          uploadFileType: file.fileType
        })
      },
      fail: () => {
        console.log('取消选择媒体')
      }
    })
  },

  onUploadClose() {
    this.setData({ showUploadModal: false })
  },

  onUploadConfirm() {
    const name = this.data.name
    const cowsheepId = this.data.cowsheepId
    const filePath = this.data.uploadFilePath
    const fileType = this.data.uploadFileType

    if (!name) {
      wx.showToast({ title: '缺少牛羊名称', icon: 'none' })
      return
    }
    if (!cowsheepId) {
      wx.showToast({ title: '缺少ID', icon: 'none' })
      return
    }
    if (!filePath) {
      wx.showToast({ title: '请选择图片或视频', icon: 'none' })
      return
    }

    const ext = fileType === 'video' ? 'mp4' : 'jpg'
    const objectKey = OSS_CONFIG.uploadDir + name + '_' + Date.now() + '.' + ext

    this.setData({ showUploadModal: false })
    wx.showLoading({ title: '上传到 OSS...' })

    uploadToOSS(filePath, objectKey)
      .then((ossUrl) => {
        wx.showLoading({ title: '保存记录...' })
        wx.request({
          url: API_URL,
          method: 'POST',
          data: {
            action: 'uploadLivestockPhoto',
            info: {
              cowsheep_id: cowsheepId,
              time: formatDate(new Date()),
              ossUrl: ossUrl
            }
          },
          success: (res) => {
            wx.hideLoading()
            wx.showToast({ title: '上传成功', icon: 'success', duration: 1500 })
          },
          fail: (err) => {
            wx.hideLoading()
            console.error('保存记录失败:', err)
            wx.showToast({ title: '文件已上传，但记录保存失败', icon: 'none', duration: 2500 })
          }
        })
      })
      .catch((err) => {
        wx.hideLoading()
        console.error('OSS 上传失败:', err)
        wx.showToast({ title: '上传失败', icon: 'error', duration: 2000 })
      })
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
