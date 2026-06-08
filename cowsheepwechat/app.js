// app.js
App({
  onLaunch() {
    // 小程序一打开就执行
    console.log("✅ 小程序启动成功ABCD！")
    console.log("🐂 牛羊GPS小程序运行中1234")
  },

  globalData: {
    gpsData: null // 以后放定位数据
  }
})