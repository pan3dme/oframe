// app.js
App({
  onLaunch() {
    // 小程序一打开就执行
    console.log("✅ 小程序启动成功ABCD！")
    console.log("🐂 牛羊GPS小程序运行中1234")
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

  globalData: {
    api_device_Url: 'https://gpsmoveinfo.cn/fc/device',
    api_cowsheep_Url: 'https://gpsmoveinfo.cn/fc/cowsheep',
    gpsData: null,          // 定位数据
    deviceCache: null,      // 设备列表缓存
    livestockCache: null,   // 牛羊列表缓存
    deviceLotCache: null    // 设备LOT最新数据缓存
  }
})