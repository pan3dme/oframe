// device.js - 设备管理
const API_DEVICE_URL = getApp().globalData.api_device_Url
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
    // 新增设备弹窗
    showAddModal: false,
    addDeviceId: '',

    // 连接牛羊弹窗
    showBindModal: false,
    bindDeviceId: '',
    bindCowsheepId: '',
    bindNameIndex: 0,
    livestockNames: [],

    // 编辑设备弹窗
    showEditModal: false,
    editOldDeviceKey: '',
    editDeviceCode: '',
    editRename: '',
    editPicurl: '',
    editPicFilePath: '',

    // 设备列表
    deviceList: []
  },

  onLoad() {
    this.fetchDeviceList()
  },

  // ========== 获取设备列表 ==========
  fetchDeviceList(forceRefresh) {
    dataCache.getDeviceList((deviceData) => {
      // 同时获取牛羊名字列表
      dataCache.getLivestockList((livestockData) => {
        const nameMap = {}
        if (livestockData && livestockData.livestockList) {
          livestockData.livestockList.forEach(item => {
            if (item.cowsheepId) nameMap[item.cowsheepId] = item.name
          })
        }

        const deviceList = (deviceData.recordList || []).map(item => ({
          ...item,
          bindName: item.link_cowsheep_id ? (nameMap[item.link_cowsheep_id] || item.link_cowsheep_id) : '',
          relativeTime: this._calcRelativeTime(item.rawTime)
        }))

        this.setData({
          deviceList,
          livestockNames: livestockData.livestockNames || []
        })
        if (forceRefresh) {
          wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
        }
      }, forceRefresh)
    }, forceRefresh)
  },

  refreshDeviceList() {
    this.fetchDeviceList(true)
  },

  // 计算相对时间：返回 "1天前" "3小时前" "刚刚" 等
  _calcRelativeTime(rawTime) {
    if (!rawTime || rawTime === '-') return ''
    const t = new Date(rawTime).getTime()
    if (isNaN(t)) return ''
    const now = Date.now()
    const diff = now - t
    const sec = Math.floor(diff / 1000)
    const min = Math.floor(sec / 60)
    const hour = Math.floor(min / 60)
    const day = Math.floor(hour / 24)
    if (sec < 60) return sec + '秒前'
    if (min < 60) return min + '分钟前'
    if (hour < 24) return hour + '小时前'
    if (day < 30) return day + '天前'
    if (day < 365) return Math.floor(day / 30) + '个月前'
    return Math.floor(day / 365) + '年前'
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
        info: { deviceId }
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

  // ========== 连接牛羊弹窗 ==========
  onItemBind(e) {
    const deviceId = e.currentTarget.dataset.deviceid
    if (!deviceId) return

    const item = this.data.deviceList.find(v => v.deviceId === deviceId)
    let nameIdx = 0
    if (item && item.link_cowsheep_id) {
      nameIdx = this.data.livestockNames.indexOf(item.bindName)
      if (nameIdx < 0) nameIdx = 0
    }

    this.setData({
      showBindModal: true,
      bindDeviceId: deviceId,
      bindNameIndex: nameIdx
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
            this.fetchDeviceList(true)
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

  // ========== 编辑设备弹窗 ==========
  onItemEdit(e) {
    const device_key = e.currentTarget.dataset.devicekey || e.currentTarget.dataset.deviceid

    // 从 deviceList 中取完整数据
    const item = this.data.deviceList.find(v => v.deviceId === device_key || v.device_key === device_key)
    if (!item) return

    this.setData({
      showEditModal: true,
      editOldDeviceKey: item.deviceId,
      editDeviceCode: item.device_key || '',
      editRename: item.rename || '',
      editPicurl: item.picurl || '',
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
          editPicurl: file.tempFilePath  // 预览用临时路径
        })
      },
      fail: () => {
        console.log('取消选择图片')
      }
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

    // 如果选了新图片，先上传 OSS
    if (picFilePath) {
      wx.showLoading({ title: '压缩上传...' })
      const objectKey = 'device/' + (device_key || oldKey) + '_' + Date.now() + '.jpg'
      compressImage(picFilePath).then((compressedPath) => {
        return uploadToOSS(compressedPath, objectKey)
      }).then((ossUrl) => {
          this._doEditConfirm(oldKey, device_key, rename, ossUrl)
        })
        .catch((err) => {
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
      url: API_DEVICE_URL,
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
          this.fetchDeviceList(true)
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

  // ========== 点击设备进入详情 ==========
  onTapDevice(e) {
    const deviceId = e.currentTarget.dataset.deviceid
    if (!deviceId) return

    wx.navigateTo({
      url: '/pages/device-detail/device-detail?deviceId=' + encodeURIComponent(deviceId),
      fail: (err) => {
        console.error('跳转设备详情失败:', err)
        wx.showToast({ title: '页面跳转失败', icon: 'none' })
      }
    })
  }
})
