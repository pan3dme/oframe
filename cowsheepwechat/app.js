// app.js
App({
  onLaunch() {
    // 小程序一打开就执行
    console.log("✅ 小程序启动成功ABCD！")
    console.log("🐂 牛羊GPS小程序运行中1234")
    // 初始化登录状态：读取本地登录记录 + 获取 wx.login code
    this.initLogin()
  },

  // 初始化登录：读取本地登录记录/自动登录开关到全局，并获取 wx.login 临时凭证 code
  initLogin() {
    // 读取自动登录开关（登录页勾选后持久化）
    try {
      const autoLogin = wx.getStorageSync('setting_auto_login')
      this.globalData.autoLogin = autoLogin === true || autoLogin === 'true' || autoLogin === 1
    } catch (e) { /* ignore */ }

    // 读取服务器登录返回的用户数据（主程序依赖它；缺失则即使自动登录也要重新登录）
    try {
      const serverData = wx.getStorageSync('login_server_data')
      this.globalData.serverData = serverData || null
    } catch (e) { /* ignore */ }

    try {
      const loginInfo = wx.getStorageSync('login_info')
      if (loginInfo && loginInfo.isLoggedIn) {
        this.globalData.loginInfo = loginInfo
        // 仅当启用"自动登录"且本地有服务器用户数据时才免密放行
        if (this.globalData.autoLogin && this.globalData.serverData) {
          this.globalData.sessionConfirmed = true
        }
      }
    } catch (e) { /* ignore */ }

    // 获取微信登录 code（临时凭证，可传给后端换取 openid 作为用户唯一标识）
    wx.login({
      success: (res) => {
        if (res.code) this.globalData.loginCode = res.code
      },
      fail: () => console.warn('wx.login 获取 code 失败')
    })
  },

  // 检查本次会话是否已确认登录（登录页登录成功后置为 true）
  isLoggedIn() {
    return !!this.globalData.sessionConfirmed
  },

  // 格式化时间为 YYYY/M/D HH:mm:ss（24小时制，无中文）
  formatTime(date) {
    if (!date) date = new Date()
    const y = date.getFullYear()
    const M = date.getMonth() + 1
    const d = date.getDate()
    const h = date.getHours()
    const m = date.getMinutes()
    const s = date.getSeconds()
    const pad2 = n => String(n).padStart(2, '0')
    return y + '/' + M + '/' + d + ' ' + pad2(h) + ':' + pad2(m) + ':' + pad2(s)
  },

  // 从服务器登录返回的用户数据中解析 wechatid（除登录外，所有 action 请求的 info 都需携带）
  getWechatId() {
    let serverData = this.globalData.serverData
    if (!serverData) {
      try {
        serverData = wx.getStorageSync('login_server_data')
        this.globalData.serverData = serverData || null
      } catch (e) { /* ignore */ }
    }
    if (serverData && serverData.data && Array.isArray(serverData.data.attributes)) {
      const item = serverData.data.attributes.find(a => a.columnName === 'wechatid')
      if (item && item.columnValue !== undefined && item.columnValue !== null) {
        return item.columnValue
      }
    }
    return ''
  },

  globalData: {
    api_device_Url: 'https://gpsmoveinfo.cn/fc/device',
    api_cowsheep_Url: 'https://gpsmoveinfo.cn/fc/cowsheep',
    api_route_place_Url: 'https://gpsmoveinfo.cn/fc/route_place',
 
    amap_key: '9f13e28346ef46071add1dc6ca4bd0ec',  // 高德地图 Key (Web服务)
 
    gpsData: null,          // 定位数据
    deviceCache: null,      // 设备列表缓存
    livestockCache: null,   // 牛羊列表缓存
    deviceLotCache: null,   // 设备LOT最新数据缓存
    deviceBatteryCache: null, // 设备电量缓存
    roadCache: null,        // 道路列表缓存
    roadCacheTime: null,    // 道路缓存时间戳
    placeCache: null,       // 地名列表缓存
    placeCacheTime: null,   // 地名缓存时间戳

    loginInfo: null,        // 登录用户信息 { username, password, wxNickname, wxAvatar, openid, serverData, loginTime }
    serverData: null,       // 服务器登录返回的用户数据 { status, msg, data:{ primaryKey, attributes } }，主程序依赖
    loginCode: null,        // wx.login 获取的临时凭证 code
    isLoggedIn: false,      // 本次会话是否已确认登录（登录成功后置 true，重启失效）
    autoLogin: false,       // 自动登录开关：true=有本地记录直接进首页；false=每次打开都要登录确认
    sessionConfirmed: false // 内部标记，与 isLoggedIn 同步使用
  }
})