// livestock.js - 管理牛羊
const API_URL = 'https://device-updata-puknouxjhg.cn-shanghai.fcapp.run'
const OSS_CONFIG = require('../../config/oss-config.js')

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

    // 上传媒体弹窗（图片/视频）
    showUploadModal: false,
    livestockNames: [],
    uploadNameIndex: 0,
    uploadName: '',
    uploadCowsheepId: '',
    uploadFilePath: '',
    uploadFileType: '',   // 'image' | 'video'

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

  // ========== 列表项双击 → 跳转详情页 ==========
  onItemTap(e) {
    const name = e.currentTarget.dataset.name
    if (!name) return
    const now = Date.now()
    if (now - this._tapItemTime < 500 && this._tapItemName === name) {
      this._tapItemTime = 0
      this._tapItemName = ''
      // 双击：跳转详情页
      const item = this.data.livestockList.find(v => v.name === name)
      if (!item) return
      wx.navigateTo({
        url: '/pages/detail/detail?name=' + encodeURIComponent(item.name) +
          '&cowsheepId=' + encodeURIComponent(item.cowsheepId || '') +
          '&birthday=' + encodeURIComponent(item.birthday || '') +
          '&gender=' + encodeURIComponent(item.gender || '') +
          '&avatar=' + encodeURIComponent(item.avatar || '')
      })
    } else {
      this._tapItemTime = now
      this._tapItemName = name
      setTimeout(() => { this._tapItemTime = 0; this._tapItemName = '' }, 500)
    }
  },

  // 从服务器获取牛羊列表
  fetchLivestockList() {
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getLivestockList'
      },
      success: (res) => {
        console.log('牛羊列表返回:', JSON.stringify(res.data))

        let list = []
        const data = res.data
        if (data && data.data && Array.isArray(data.data)) {
          data.data.forEach(item => {
            let name = ''
            let cowsheepId = ''
            let birthday = ''
            let gender = false

            // cowsheep_id 在 primaryKey 中
            if (item.primaryKey && Array.isArray(item.primaryKey)) {
              item.primaryKey.forEach(pk => {
                if (pk.name === 'cowsheep_id') cowsheepId = String(pk.value)
              })
            }

            // name / birthday / gender / avatar 在 attributes 中
            let avatar = ''
            if (item.attributes) {
              item.attributes.forEach(attr => {
                if (attr.columnName === 'rename') name = attr.columnValue
                if (attr.columnName === 'birthday') birthday = attr.columnValue
                if (attr.columnName === 'gender') gender = attr.columnValue === true || attr.columnValue === 'true'
                if (attr.columnName === 'avatar') avatar = attr.columnValue || ''
              })
            }

            if (name) {
              list.push({
                name,
                cowsheepId,
                birthday: birthday || '-',
                gender: gender ? '公' : '母',
                avatar
              })
            }
          })
        }

        const names = list.map(item => item.name)
        this.setData({
          livestockList: list,
          livestockNames: names
        })
      },
      fail: (err) => {
        console.error('获取牛羊列表失败:', err)
      }
    })
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
        this.fetchLivestockList()
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
        // 刷新列表
        this.fetchLivestockList()
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('新增牛羊失败:', err)
        wx.showToast({ title: '提交失败', icon: 'error', duration: 2000 })
      }
    })
  },

  // ========== 列表项点击上传 — 打开弹窗（预填名称 + cowsheep_id） ==========
  onItemUploadPhoto(e) {
    const name = e.currentTarget.dataset.name
    if (!name) return

    const names = this.data.livestockNames
    const idx = names.indexOf(name)
    // 从列表中查找对应的 cowsheepId
    const item = this.data.livestockList.find(v => v.name === name)
    const cowsheepId = item ? item.cowsheepId : ''

    this.setData({
      uploadName: name,
      uploadCowsheepId: cowsheepId,
      uploadNameIndex: idx >= 0 ? idx : 0,
      uploadFilePath: '',
      uploadFileType: '',
      showUploadModal: true
    })
  },

  // 弹窗内事件
  onUploadNameChange(e) {
    const idx = parseInt(e.detail.value)
    const name = this.data.livestockNames[idx] || ''
    const item = this.data.livestockList.find(v => v.name === name)
    this.setData({
      uploadNameIndex: idx,
      uploadName: name,
      uploadCowsheepId: item ? item.cowsheepId : ''
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
          uploadFileType: file.fileType   // 'image' | 'video'
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
    const name = this.data.uploadName
    const cowsheepId = this.data.uploadCowsheepId
    const filePath = this.data.uploadFilePath
    const fileType = this.data.uploadFileType

    if (!name) {
      wx.showToast({ title: '请选择牛羊', icon: 'none' })
      return
    }
    if (!cowsheepId) {
      wx.showToast({ title: '缺少ID，请重新选择牛羊', icon: 'none' })
      return
    }
    if (!filePath) {
      wx.showToast({ title: '请选择图片或视频', icon: 'none' })
      return
    }

    // 生成 OSS 对象 Key: uploadDir/name_timestamp.ext
    const ext = fileType === 'video' ? 'mp4' : 'jpg'
    const objectKey = OSS_CONFIG.uploadDir + name + '_' + Date.now() + '.' + ext

    this.setData({ showUploadModal: false })
    wx.showLoading({ title: '上传到 OSS...' })

    uploadToOSS(filePath, objectKey)
      .then((ossUrl) => {
        // OSS 上传成功后，通知服务器记录文件地址
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
            console.log('=== 上传图片记录 POST 返回数据 ===')
            console.log(JSON.stringify(res, null, 2))
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
})
