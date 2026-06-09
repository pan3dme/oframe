// livestock.js - 管理牛羊
const API_URL = getApp().globalData.apiUrl
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
  /* 工具 */
  const rotl = (n, s) => (n << s) | (n >>> (32 - s))
  const len = msgBytes.length * 8
  /* 补位 */
  const blocks = []
  for (let i = 0; i < msgBytes.length; i += 4) {
    blocks[i >> 2] = (msgBytes[i] << 24) | (msgBytes[i + 1] << 16) | (msgBytes[i + 2] << 8) | msgBytes[i + 3]
  }
  blocks[len >> 5] |= 0x80 << (24 - (len % 32))
  blocks[((len + 64 >> 9) << 4) + 15] = len
  /* 主循环 */
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

    // 1. 构建 Policy（有效期 1 天）
    const expire = new Date(Date.now() + 86400000).toISOString()
    const policyObj = {
      expiration: expire,
      conditions: [
        { bucket: bucket },
        ['starts-with', '$key', OSS_CONFIG.uploadDir],
        { 'x-oss-object-acl': 'public-read' },
        ['content-length-range', 0, 104857600]   // 100MB
      ]
    }
    const policyStr = JSON.stringify(policyObj)
    const policyBase64 = _base64(_strToBytes(policyStr).buffer)

    // 2. 签名 = base64(hmac-sha1(AccessKeySecret, policyBase64))
    const signature = _base64(_hmacSha1(accessKeySecret, policyBase64))

    // 3. 上传到 OSS
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
      success: () => {
        const ossUrl = `${host}${objectKey}`
        console.log('OSS 上传成功:', ossUrl)
        resolve(ossUrl)
      },
      fail: (err) => {
        console.error('OSS 上传失败:', err)
        reject(err)
      }
    })
  })
}

Page({
  data: {
    showAddModal: false,
    addCowsheepId: '',
    addBirthday: '',
    addGenderIndex: 0,
    genderOptions: ['公', '母'],

    // 连接设备弹窗
    showDeviceModal: false,
    bindCowsheepId: '',
    bindCowsheepName: '',
    bindDeviceIndex: 0,
    deviceIdOptions: [],
    deviceBindMap: {},           // deviceId → link_cowsheep_id

    livestockNames: [],

    // 修改记录弹窗
    showEditModal: false,
    editName: '',
    editOriginalName: '',
    editBirthday: '',
    editGenderIndex: 0,
    editCowsheepId: '',
    editAvatar: '',         // 当前已保存的头像 URL
    editAvatarUrl: '',      // 新上传的临时的头像 URL（OSS 回传）

    // 牛羊列表
    livestockList: []
  },

  _tapAvatarTime: 0,       // 修改面板头像双击计时
  _tapItemTime: 0,         // 列表项双击计时
  _tapItemName: '',        // 列表项双击缓存的名称

  onLoad() {
    this.fetchLivestockList()
  },

  // ========== 列表项点击 → 跳转详情页 ==========
  onItemTap(e) {
    const name = e.currentTarget.dataset.name
    if (!name) return
    const item = this.data.livestockList.find(v => v.name === name)
    if (!item) return
    wx.navigateTo({
      url: '/pages/detail/detail?name=' + encodeURIComponent(item.name) +
        '&cowsheepId=' + encodeURIComponent(item.cowsheepId || '') +
        '&birthday=' + encodeURIComponent(item.birthday || '') +
        '&gender=' + encodeURIComponent(item.gender || '') +
        '&avatar=' + encodeURIComponent(item.avatar || '')
    })
  },

  // 从服务器获取牛羊列表（优先使用缓存）
  fetchLivestockList(forceRefresh) {
    dataCache.getLivestockList((cachedData) => {
      // 同时获取设备列表，建立 cowsheep_id → deviceId 映射
      dataCache.getDeviceList((deviceData) => {
        const deviceBindMap = {}  // cowsheep_id → deviceId
        if (deviceData && deviceData.recordList) {
          deviceData.recordList.forEach(record => {
            if (record.link_cowsheep_id && record.deviceId && record.deviceId !== '-') {
              // 一个牛羊可能绑多个设备，只显示第一个
              if (!deviceBindMap[record.link_cowsheep_id]) {
                deviceBindMap[record.link_cowsheep_id] = record.deviceId
              }
            }
          })
        }

        // 给每条牛羊附上连接设备信息 + 年龄
        const enrichedList = cachedData.livestockList.map(item => ({
          ...item,
          connectedDevice: deviceBindMap[item.cowsheepId] || '',
          age: this._calcAge(item.birthday)
        }))

        this.setData({
          livestockList: enrichedList,
          livestockNames: cachedData.livestockNames,
          deviceIdOptions: deviceData.deviceIdOptions || ['未连接'],
          deviceBindMap: deviceData.deviceBindMap || {}
        })
        if (forceRefresh) {
          wx.showToast({ title: '已刷新', icon: 'success', duration: 1000 })
        }
      }, forceRefresh)
    }, forceRefresh)
  },

  // 强制刷新牛羊列表
  refreshLivestockList() {
    this.fetchLivestockList(true)
  },

  // 计算年龄：生日到今天，返回 "1年3个月" 格式
  _calcAge(birthdayStr) {
    if (!birthdayStr || birthdayStr === '-') return '-'
    const str = birthdayStr.replace(/\//g, '-')
    const birth = new Date(str)
    if (isNaN(birth.getTime())) return birthdayStr

    const today = new Date()
    let years = today.getFullYear() - birth.getFullYear()
    let months = today.getMonth() - birth.getMonth()
    // 如果还没到生日月，年-1，月+12
    if (months < 0) {
      years -= 1
      months += 12
    }

    if (years > 0) {
      return years + '年' + (months > 0 ? months + '个月' : '')
    } else {
      return months > 0 ? months + '个月' : '不足1个月'
    }
  },

  // 获取当天日期字符串
  getTodayStr() {
    const d = new Date()
    const y = d.getFullYear()
    const m = String(d.getMonth() + 1).padStart(2, '0')
    const day = String(d.getDate()).padStart(2, '0')
    return y + '-' + m + '-' + day
  },

  // ========== 列表项编辑按钮——打开修改面板 ==========
  onItemEdit(e) {
    const name = e.currentTarget.dataset.name
    if (!name) return
    this.openEditModal(name)
  },

  openEditModal(name) {
    const item = this.data.livestockList.find(v => v.name === name)
    if (!item) return

    const genderIdx = item.gender === '公' ? 0 : 1
    this.setData({
      showEditModal: true,
      editName: item.name,
      editOriginalName: item.name,
      editBirthday: item.birthday === '-' ? '' : item.birthday,
      editGenderIndex: genderIdx,
      editCowsheepId: item.cowsheepId,
      editAvatar: item.avatar || '',
      editAvatarUrl: ''   // 清空临时上传
    })
  },

  // ========== 修改面板事件 ==========
  onEditClose() {
    this.setData({ showEditModal: false })
  },

  // 双击头像 → 选择图片 → 上传 OSS
  onEditAvatarTap() {
    const now = Date.now()
    if (now - this._tapAvatarTime < 500) {
      // 双击触发
      this._tapAvatarTime = 0
      this._chooseAndUploadAvatar()
    } else {
      this._tapAvatarTime = now
      setTimeout(() => { this._tapAvatarTime = 0 }, 500)
    }
  },

  _chooseAndUploadAvatar() {
    wx.chooseMedia({
      count: 1,
      mediaType: ['image'],
      sourceType: ['album', 'camera'],
      sizeType: ['compressed'],
      success: (res) => {
        const filePath = res.tempFiles[0].tempFilePath
        const cowsheepId = this.data.editCowsheepId
        const objectKey = OSS_CONFIG.uploadDir + 'avatar_' + (cowsheepId || Date.now()) + '_' + Date.now() + '.jpg'

        wx.showLoading({ title: '上传头像...' })

        uploadToOSS(filePath, objectKey)
          .then((ossUrl) => {
            wx.hideLoading()
            this.setData({ editAvatarUrl: ossUrl })
            wx.showToast({ title: '头像已更新', icon: 'success', duration: 1200 })
          })
          .catch((err) => {
            wx.hideLoading()
            console.error('头像上传 OSS 失败:', err)
            wx.showToast({ title: '上传失败', icon: 'error', duration: 2000 })
          })
      },
      fail: () => {
        console.log('取消选择头像')
      }
    })
  },

  onEditNameInput(e) {
    this.setData({ editName: e.detail.value })
  },

  onEditBirthdayChange(e) {
    this.setData({ editBirthday: e.detail.value })
  },

  onEditGenderChange(e) {
    this.setData({ editGenderIndex: parseInt(e.detail.value) })
  },

  onEditConfirm() {
    const birthday = this.data.editBirthday
    const gender = this.data.editGenderIndex === 0
    const cowsheepId = this.data.editCowsheepId
    const rename = this.data.editName
    const avatarUrl = this.data.editAvatarUrl || this.data.editAvatar || ""

    if (!birthday) {
      wx.showToast({ title: '请选择生日', icon: 'none' })
      return
    }

    this.setData({ showEditModal: false })
    wx.showLoading({ title: '提交中...' })

    

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'updateLivestock',
        info: {
          cowsheep_id: cowsheepId,
          rename: rename,
          birthday: birthday,
          gender: gender,
          avatar: avatarUrl
        }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('修改牛羊返回:', JSON.stringify(res.data))
        wx.showToast({ title: '修改成功', icon: 'success', duration: 1500 })
        this.fetchLivestockList(true)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('修改牛羊失败:', err)
        wx.showToast({ title: '提交失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // ========== 新增牛羊 ==========
  onAdd() {
    this.setData({
      showAddModal: true,
      addCowsheepId: '',
      addBirthday: this.getTodayStr(),
      addGenderIndex: 0
    })
  },

  
  // 表单输入事件
  onAddCowsheepIdInput(e) {
    this.setData({ addCowsheepId: e.detail.value })
  },
  onAddBirthdayChange(e) {
    this.setData({ addBirthday: e.detail.value })
  },
  /**
 * 新增面板 - 性别选择器变更回调，更新选中性别索引
 * @param {Object} e - 选择器变更事件对象
 * @param {Object} e.detail - 事件详情
 * @param {string|number} e.detail.value - 选中项的索引值
 */
onAddGenderChange(e) {
    this.setData({ addGenderIndex: parseInt(e.detail.value) })
  },

  // 关闭弹窗
  onAddClose() {
    this.setData({ showAddModal: false })
  },

  // 确认新增 — 提交到服务器
  onAddConfirm() {
    const cowsheepId = this.data.addCowsheepId.trim()
    const birthday = this.data.addBirthday
    const gender = this.data.addGenderIndex === 0  // 0=公=true, 1=母=false

    if (!cowsheepId) {
      wx.showToast({ title: '请输入唯一编号', icon: 'none' })
      return
    }
    if (!birthday) {
      wx.showToast({ title: '请选择生日', icon: 'none' })
      return
    }

    this.setData({ showAddModal: false })
    wx.showLoading({ title: '提交中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'addLivestock',
        info: {
          cowsheep_id: cowsheepId,
          birthday: birthday,
          gender: gender
        }
      },
      success: (res) => {
        wx.hideLoading()
        console.log('新增牛羊返回:', JSON.stringify(res.data))
        wx.showToast({ title: '新增成功', icon: 'success', duration: 1500 })
        // 强制刷新列表
        this.fetchLivestockList(true)
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('新增牛羊失败:', err)
        wx.showToast({ title: '提交失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // ========== 列表项点击连接设备 — 打开弹窗 ==========
  onItemConnectDevice(e) {
    const name = e.currentTarget.dataset.name
    if (!name) return
    const item = this.data.livestockList.find(v => v.name === name)
    if (!item) return

    // 查找当前牛羊是否已绑定设备
    let boundIndex = 0
    const deviceBindMap = this.data.deviceBindMap
    for (const [devId, linkId] of Object.entries(deviceBindMap)) {
      if (linkId === item.cowsheepId) {
        boundIndex = this.data.deviceIdOptions.indexOf(devId)
        if (boundIndex < 0) boundIndex = 0
        break
      }
    }

    this.setData({
      showDeviceModal: true,
      bindCowsheepId: item.cowsheepId,
      bindCowsheepName: item.name,
      bindDeviceIndex: boundIndex
    })
  },

  // 连接设备弹窗 — 选择器变更
  onBindDevicePickerChange(e) {
    this.setData({ bindDeviceIndex: parseInt(e.detail.value) })
  },

  // 关闭连接设备弹窗
  onBindDeviceClose() {
    this.setData({ showDeviceModal: false })
  },

  // 确认连接设备
  onBindDeviceConfirm() {
    const index = this.data.bindDeviceIndex
    const deviceId = this.data.deviceIdOptions[index] || ''
    const cowsheepId = this.data.bindCowsheepId

    if (index === 0 || deviceId === '未连接') {
      wx.showToast({ title: '请选择一个设备', icon: 'none' })
      return
    }

    // 已绑定当前牛羊，无需重复
    if (this.data.deviceBindMap[deviceId] === cowsheepId) {
      this.setData({ showDeviceModal: false })
      wx.showToast({ title: '设备已连接，无需重复绑定', icon: 'none' })
      return
    }

    this.setData({ showDeviceModal: false })
    wx.showLoading({ title: '绑定中...' })

    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'bindDeviceCow',
        info: { deviceId, cowsheepId }
      },
      success: (res) => {
        wx.hideLoading()
        let result = res.data
        if (typeof result === 'string') {
          try { result = JSON.parse(result) } catch (e) {}
        }
        if (result && result.status === 'success') {
          // 更新本地绑定映射 + 刷新列表
          const newBindMap = { ...this.data.deviceBindMap }
          newBindMap[deviceId] = cowsheepId
          this.setData({ deviceBindMap: newBindMap })
          this.fetchLivestockList(true)
          wx.showToast({ title: result.msg || '连接成功', icon: 'success', duration: 1500 })
        } else {
          wx.showToast({ title: (result && result.msg) || '连接失败', icon: 'none', duration: 2500 })
        }
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('设备绑定失败:', err)
        wx.showToast({ title: '网络请求失败', icon: 'error', duration: 2000 })
      }
    })
  },
})
