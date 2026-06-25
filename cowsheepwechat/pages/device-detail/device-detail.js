// device-detail.js - 设备详情
const API_URL = getApp().globalData.api_device_Url
const API_COWSHEEP_URL = getApp().globalData.api_cowsheep_Url
const OSS_CONFIG = require('../../config/oss-config.js')
const dataCache = require('../../config/data-cache.js')
const { compressImage } = require('../../utils/image-compress.js')

// ==================== SHA1 / HMAC-SHA1 纯 JS 实现（OSS签名用） ====================
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
        ['starts-with', '$key', 'device/'],
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
    deviceId: '',
    deviceInfo: null,
    // 查看设备轨迹
    showTrackModal: false,
    trackDate: '',
    // 列出数据（表格展示）
    showRecordTable: false,
    recordList: [],
    // 编辑设备弹窗
    showEditModal: false,
    editOldDeviceKey: '',
    editDeviceCode: '',
    editRename: '',
    editPicurl: '',
    editPicFilePath: '',
    // 连接牛羊弹窗
    showBindModal: false,
    bindDeviceId: '',
    bindNameIndex: 0,
    livestockNames: [],
    // 管理员模式
    isAdmin: false
  },

  onLoad(options) {
    const deviceId = options.deviceId || ''
    const today = this.getTodayStr()
    // 读取管理员设置
    let isAdmin = false
    try {
      const adminVal = wx.getStorageSync('setting_is_admin')
      isAdmin = !!(getApp().globalData.isAdmin || adminVal)
    } catch (e) { /* ignore */ }
    this.setData({ deviceId, trackDate: today, isAdmin })
    if (deviceId) {
      this.loadDeviceInfo(deviceId)
    }
  },

  loadDeviceInfo(deviceId) {
    dataCache.getDeviceList((deviceData) => {
      if (!deviceData || !deviceData.recordList) {
        wx.showToast({ title: '数据加载失败', icon: 'none' })
        return
      }
      const recordList = deviceData.recordList || []
      const item = recordList.find(v => v.deviceId === deviceId)

      if (item) {
        // 从LOT表取最新lorastr和时间
        dataCache.getDeviceLotRefresh((lotData) => {
          let lotRec = null
          if (lotData && lotData.lotList) {
            lotRec = lotData.lotList.find(v => v.deviceId === deviceId)
          }
          // 用LOT数据覆盖设备表的lorastr和时间
          const enriched = { ...item }
          // 保存设备表原始时间作为"上次充电时间"
          const devDate = item.date && item.date !== '-' ? item.date : ''
          const devTime = item.time_part && item.time_part !== '-' ? item.time_part : ''
          enriched.chargeTime = devDate || devTime ? (devDate + ' ' + devTime).trim() : ''
          if (lotRec) {
            enriched.lorastr = lotRec.lorastr || item.lorastr
            enriched.date = lotRec.date || item.date
            enriched.time_part = lotRec.time_part || item.time_part
            enriched.rawTime = lotRec.rawTime || item.rawTime
          }
          this._loadBindName(enriched)
        })
      } else {
        wx.showToast({ title: '未找到设备', icon: 'none' })
        setTimeout(() => wx.navigateBack(), 1500)
      }
    })
  },

  _loadBindName(item) {
    if (!item.link_cowsheep_id) {
      this.setData({ deviceInfo: { ...item, bindName: '' } })
      this.loadTodayRecords()
      return
    }
    dataCache.getLivestockList((livestockData) => {
      let bindName = ''
      const list = (livestockData && livestockData.livestockList) ? livestockData.livestockList : []
      const found = list.find(v => v.cowsheepId === item.link_cowsheep_id)
      if (found) bindName = found.name
      this.setData({ deviceInfo: { ...item, bindName } })
      this.loadTodayRecords()
    })
  },

  // 自动加载当天轨迹记录
  loadTodayRecords(callback) {
    const deviceId = this.data.deviceId
    if (!deviceId) return
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: {
          deviceId: deviceId,
          curdate: this.data.trackDate
        },
        time: getApp().formatTime()
      },
      success: (res) => {
        console.log('设备当天轨迹查询返回:', JSON.stringify(res.data))
        const recordList = this._parseRecords(res.data)
        this.setData({ recordList, showRecordTable: recordList.length > 0 })
        if (callback) callback()
      },
      fail: (err) => {
        console.error('设备轨迹查询失败:', err)
        if (callback) callback()
      }
    })
  },

  getTodayStr() {
    const d = new Date()
    return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0')
  },

  // ========== 编辑设备弹窗 ==========
  onDetailEdit() {
    const info = this.data.deviceInfo
    if (!info) return
    this.setData({
      showEditModal: true,
      editOldDeviceKey: info.deviceId,
      editDeviceCode: info.device_key || '',
      editRename: info.rename || '',
      editPicurl: info.picurl || '',
      editPicFilePath: ''
    })
  },

  onEditDeviceCodeInput(e) {
    this.setData({ editDeviceCode: e.detail.value })
  },

  onEditRenameInput(e) {
    this.setData({ editRename: e.detail.value })
  },

  onEditPic() {
    wx.chooseMedia({
      count: 1,
      mediaType: ['image'],
      sourceType: ['album', 'camera'],
      sizeType: ['compressed'],
      success: (res) => {
        const file = res.tempFiles[0]
        this.setData({
          editPicFilePath: file.tempFilePath,
          editPicurl: file.tempFilePath
        })
      },
      fail: () => { console.log('取消选择图片') }
    })
  },

  onEditClose() {
    this.setData({ showEditModal: false })
  },

  onEditConfirm() {
    const oldKey = this.data.editOldDeviceKey
    const device_key = this.data.editDeviceCode.trim()
    const rename = this.data.editRename.trim()
    const picFilePath = this.data.editPicFilePath

    this.setData({ showEditModal: false })

    if (picFilePath) {
      wx.showLoading({ title: '压缩上传...' })
      const objectKey = 'device/' + (device_key || oldKey) + '_' + Date.now() + '.jpg'
      compressImage(picFilePath).then((compressedPath) => {
        return uploadToOSS(compressedPath, objectKey)
      }).then((ossUrl) => {
        this._doEditConfirm(oldKey, device_key, rename, ossUrl)
      }).catch((err) => {
        wx.hideLoading()
        console.error('OSS 上传失败:', err)
        wx.showToast({ title: '上传失败', icon: 'error', duration: 2000 })
      })
    } else {
      this._doEditConfirm(oldKey, device_key, rename, this.data.editPicurl)
    }
  },

  _doEditConfirm(oldKey, device_key, rename, picurl) {
    wx.showLoading({ title: '更新中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'updateDevice',
        info: { deviceId: oldKey, device_key, rename, picurl }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('编辑设备返回:', JSON.stringify(res.data))
        let result = res.data
        if (typeof result === 'string') {
          try { result = JSON.parse(result) } catch (e) {}
        }
        if (result && result.status === 'success') {
          wx.showToast({ title: result.msg || '更新成功', icon: 'success', duration: 1500 })
          dataCache.clearCache()
          this.loadDeviceInfo(this.data.deviceId)
        } else {
          wx.showToast({ title: (result && result.msg) || '更新失败', icon: 'none', duration: 2500 })
        }
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('编辑设备失败:', err)
        wx.showToast({ title: '网络请求失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // ========== 连接牛羊弹窗 ==========
  onDetailBind() {
    const info = this.data.deviceInfo
    if (!info) return
    let nameIdx = 0
    if (info.link_cowsheep_id && info.bindName) {
      nameIdx = this.data.livestockNames.indexOf(info.bindName)
      if (nameIdx < 0) nameIdx = 0
    }

    // 加载牛名列表
    dataCache.getLivestockList((livestockData) => {
      const names = (livestockData && livestockData.livestockNames) ? livestockData.livestockNames : []
      this.setData({
        showBindModal: true,
        bindDeviceId: info.deviceId,
        bindNameIndex: nameIdx,
        livestockNames: names
      })
    })
  },

  onBindNameChange(e) {
    this.setData({ bindNameIndex: parseInt(e.detail.value) })
  },

  onBindClose() {
    this.setData({ showBindModal: false })
  },

  onBindConfirm() {
    const name = this.data.livestockNames[this.data.bindNameIndex]
    const deviceId = this.data.bindDeviceId
    if (!name || !deviceId) {
      wx.showToast({ title: '请选择牛羊', icon: 'none' })
      return
    }

    dataCache.getLivestockList((livestockData) => {
      const item = (livestockData.livestockList || []).find(v => v.name === name)
      if (!item || !item.cowsheepId) {
        wx.showToast({ title: '未找到对应牛羊', icon: 'none' })
        return
      }

      this.setData({ showBindModal: false })
      wx.showLoading({ title: '绑定中...' })

      wx.request({
        url: API_COWSHEEP_URL,
        method: 'POST',
        data: {
          action: 'bindDeviceCow',
          info: { deviceId, cowsheepId: item.cowsheepId }
        },
        success: (res) => {
          wx.hideLoading()
          console.log('设备绑定返回:', JSON.stringify(res.data))
          let result = res.data
          if (typeof result === 'string') {
            try { result = JSON.parse(result) } catch (e) {}
          }
          if (result && result.status === 'success') {
            wx.showToast({ title: result.msg || '绑定成功', icon: 'success', duration: 1500 })
            this.loadDeviceInfo(this.data.deviceId)
          } else {
            wx.showToast({ title: (result && result.msg) || '绑定失败', icon: 'none', duration: 2500 })
          }
        },
        fail: (err) => {
          wx.hideLoading()
          console.error('设备绑定失败:', err)
          wx.showToast({ title: '网络请求失败', icon: 'error', duration: 2000 })
        }
      })
    })
  },

  // 查看设备轨迹 — 弹日期选择 → 查GPS数据 → 跳转地图页
  // 点击设备图片放大预览
  onPreviewImage() {
    const picurl = this.data.deviceInfo && this.data.deviceInfo.picurl
    if (picurl) {
      wx.previewImage({
        current: picurl,
        urls: [picurl]
      })
    }
  },

  onViewRecords() {
    this.setData({
      showTrackModal: true,
      trackDate: this.getTodayStr()
    })
  },

  onTrackDateChange(e) {
    this.setData({ trackDate: e.detail.value })
  },

  onTrackClose() {
    this.setData({ showTrackModal: false })
  },

  onTrackConfirm() {
    this.setData({ showTrackModal: false })
    wx.showLoading({ title: '查询中...' })
    this.loadTodayRecords(() => wx.hideLoading())
  },

  // 轨迹地图：跳转到地图页展示GPS轨迹
  onTrackShowMap() {
    const deviceId = this.data.deviceId
    this.setData({ showTrackModal: false })
    wx.showLoading({ title: '查询中...' })
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: {
          deviceId: deviceId,
          curdate: this.data.trackDate
        },
        time: getApp().formatTime()
      },
      success: (res) => {
        wx.hideLoading()
        console.log('设备轨迹查询返回:', JSON.stringify(res.data))
        const recordList = this._parseRecordsForMap(res.data)
        if (recordList.length === 0) {
          wx.showToast({ title: '该设备当天无轨迹数据', icon: 'none' })
          return
        }
        getApp().globalData.trackData = recordList
        wx.navigateTo({ url: '/pages/trackmap/trackmap' })
      },
      fail: (err) => {
        wx.hideLoading()
        wx.showToast({ title: '查询失败', icon: 'error' })
      }
    })
  },

  // 列出数据的解析：和features页parseRecordList格式一致
  _parseRecords(data) {
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
      const rawTime = attr.time || record.time || '-'
      const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']
      return { deviceId, lorastr, date: date || '-', time_part: time_part || '', rawTime }
    })
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

  // 地图轨迹解析：输出gps/crow_id供trackmap使用
  _parseRecordsForMap(data) {
    let rawList = []
    if (data && data.data && Array.isArray(data.data)) {
      rawList = data.data
    } else if (Array.isArray(data)) {
      rawList = data
    }
    return rawList.map(record => {
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
      return {
        deviceId: attr.deviceId || record.deviceId || '-',
        auto_id: attr.auto_id || record.auto_id || '-',
        gps: attr.gps || record.gps || '',
        lorastr: attr.lorastr || record.lorastr || '',
        crow_id: attr.crow_idx || record.crow_idx || '',
        time: attr.time || record.time || ''
      }
    })
  }
})
