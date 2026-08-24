// login.js - 用户登录页
// 说明：微信小程序无法直接获取用户微信号，这里通过 wx.login 获取 code（可换 openid 作为唯一标识），
// 用户输入用户名、密码。验证通过后记录到本地，下次打开自动登录。

const LOGIN_KEY = 'login_info'
const STORAGE_KEY_AUTO_LOGIN = 'setting_auto_login'
// 服务器登录成功后返回的用户数据缓存（{status,msg,data}，data 含 primaryKey/attributes）
// 主程序依赖这份数据取用户身份，缺失时必须重新输入用户名密码获取
const SERVER_DATA_KEY = 'login_server_data'

Page({
  data: {
    username: '',      // 用户名
    password: '',      // 密码
    loginCode: '',     // wx.login 获取的临时凭证 code
    autoLogin: false,  // 是否自动登录（勾选后下次免密进入首页）
    loading: false
  },

  onLoad() {
    // 读取自动登录开关状态
    try {
      const autoLogin = wx.getStorageSync(STORAGE_KEY_AUTO_LOGIN)
      this.setData({ autoLogin: autoLogin === true || autoLogin === 'true' || autoLogin === 1 })
    } catch (e) { /* ignore */ }
    // 已登录且启用自动登录，且本地有服务器返回的用户数据时才直接回首页；
    // 服务器数据缺失则必须重新输入用户名密码重新获取
    try {
      const loginInfo = wx.getStorageSync(LOGIN_KEY)
      if (loginInfo && loginInfo.isLoggedIn && this.data.autoLogin && this._hasServerData()) {
        wx.reLaunch({ url: '/pages/index/index' })
        return
      }
      // 未自动登录时，预填上次的用户名，方便确认
      if (loginInfo && loginInfo.username) {
        this.setData({ username: loginInfo.username })
      }
    } catch (e) { /* ignore */ }
    this._fetchLoginCode()
  },

  // 获取微信登录 code（临时凭证，可传给后端换取 openid 作为用户唯一标识）
  _fetchLoginCode() {
    wx.login({
      success: (res) => {
        if (res.code) {
          this.setData({ loginCode: res.code })
          getApp().globalData.loginCode = res.code
        }
      },
      fail: () => console.warn('wx.login 获取 code 失败')
    })
  },

  onUsernameInput(e) {
    this.setData({ username: e.detail.value })
  },

  onPasswordInput(e) {
    this.setData({ password: e.detail.value })
  },

  // 自动登录复选框
  onAutoLoginChange(e) {
    const checked = (e.detail.value || []).indexOf('auto') !== -1
    this.setData({ autoLogin: checked })
    try {
      wx.setStorageSync(STORAGE_KEY_AUTO_LOGIN, checked)
    } catch (err) { /* ignore */ }
    getApp().globalData.autoLogin = checked
  },

  // ==================== 后端验证 ====================
  // 服务器根据 data.action === 'login' 判断登录请求
  _verifyAccount({ username, password, code }) {
    const url = getApp().globalData.api_device_Url
    return new Promise((resolve) => {
      wx.request({
        url: url,
        method: 'POST',
        data: { action: 'login', info: { username, password, code } },
        timeout: 8000,
        success: (res) => {
          // 打印返回结果（调试用）
          console.log('[登录验证] 请求URL:', url)
          console.log('[登录验证] 返回结果:', JSON.stringify(res.data))
          // 服务器返回：{ status: "success"|"error", msg: "...", data: ... }
          const data = res.data
          if (data && data.status === 'success') {
            // 把服务器返回的完整数据带回（含 data.primaryKey / data.attributes），供缓存
            resolve({ success: true, serverData: data })
          } else {
            resolve({ success: false, message: (data && data.msg) || '用户名或密码错误' })
          }
        },
        fail: (err) => {
          console.error('[登录验证] 请求失败:', err)
          resolve({ success: false, message: '网络异常，请稍后重试' })
        }
      })
    })
  },

  onLogin() {
    const { username, password } = this.data
    if (!username.trim()) {
      wx.showToast({ title: '请输入用户名', icon: 'none' })
      return
    }
    if (!password) {
      wx.showToast({ title: '请输入密码', icon: 'none' })
      return
    }
    if (this.data.loading) return

    this.setData({ loading: true })
    wx.showLoading({ title: '登录中...', mask: true })

    this._verifyAccount({ username: username.trim(), password, code: this.data.loginCode })
      .then((res) => {
        wx.hideLoading()
        this.setData({ loading: false })
        if (res && res.success === false) {
          wx.showToast({ title: res.message || '用户名或密码错误', icon: 'none' })
          return
        }
        this._saveLoginInfo(res)
      })
      .catch((err) => {
        wx.hideLoading()
        this.setData({ loading: false })
        console.error('登录验证失败:', err)
        wx.showToast({ title: '验证失败，请稍后重试', icon: 'none' })
      })
  },

  // 本地是否已有服务器返回的用户数据（主程序依赖它，缺失则必须重新登录获取）
  _hasServerData() {
    try {
      const sd = wx.getStorageSync(SERVER_DATA_KEY)
      return !!(sd && sd.data && sd.data.primaryKey && sd.data.attributes)
    } catch (e) {
      return false
    }
  },

  // 记录登录信息（下次自动登录），并跳转首页
  _saveLoginInfo(verifyResult) {
    const serverData = (verifyResult && verifyResult.serverData) || null
    const loginInfo = {
      username: this.data.username.trim(),
      password: this.data.password,
      wxAvatar: '',
      wxNickname: '',
      openid: (verifyResult && verifyResult.openid) || '', // 后端换取的 openid（可选）
      loginCode: this.data.loginCode,
      serverData: serverData, // 服务器返回的完整数据 { status, msg, data }
      loginTime: getApp().formatTime(new Date()),
      isLoggedIn: true
    }
    try {
      wx.setStorageSync(LOGIN_KEY, loginInfo)
    } catch (e) {
      console.error('保存登录信息失败:', e)
    }
    // 单独缓存服务器返回数据，主程序用它取用户身份；缺失则自动登录也要重新输入
    if (serverData) {
      try {
        wx.setStorageSync(SERVER_DATA_KEY, serverData)
      } catch (e) {
        console.error('保存服务器用户数据失败:', e)
      }
    }
    const app = getApp()
    app.globalData.loginInfo = loginInfo
    app.globalData.serverData = serverData
    app.globalData.isLoggedIn = true
    app.globalData.autoLogin = this.data.autoLogin
    // 本次会话已确认登录（内存态，重启后失效，需重新确认）
    app.globalData.sessionConfirmed = true

    wx.showToast({ title: '登录成功', icon: 'success' })
    setTimeout(() => {
      wx.reLaunch({ url: '/pages/index/index' })
    }, 600)
  }
})
