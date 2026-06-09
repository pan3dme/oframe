// app.js
App({
  onLaunch() {
    // 小程序一打开就执行
    console.log("✅ 小程序启动成功ABCD！")
    console.log("🐂 牛羊GPS小程序运行中1234")
  },

  globalData: {
    api_device_Url: 'https://device-updata-puknouxjhg.cn-shanghai.fcapp.run',
    api_cowsheep_Url: 'https://cowsheep-updata-dxnlqxjkzc.cn-shanghai.fcapp.run',
    gpsData: null,       // 定位数据
    deviceCache: null,   // 设备列表缓存
    livestockCache: null // 牛羊列表缓存
  }
})