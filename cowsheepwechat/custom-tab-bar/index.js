// custom-tab-bar/index.js — 自定义 tabBar
// 目的：底部"设备"/"地图"TAB 在无感刷新（已在对应页面时单击该 TAB）期间显示"转圈"加载态
// 说明：原生 tabBar 无法做旋转动画，故改为自定义 tabBar；图标仍复用 images/ 下的原生图标
Component({
  data: {
    // 当前选中的 tab 下标（0首页 / 1设备 / 2功能 / 3地图），由各 tab 页 onShow 同步
    selected: 0,
    color: '#999999',
    selectedColor: '#07c160',
    // 设备 tab 是否处于"无感刷新"中（true 时用转圈替代设备图标）
    deviceRefreshing: false,
    // 地图 tab 是否处于"无感刷新"中（true 时用转圈替代地图图标）
    mapRefreshing: false,
    list: [
      {
        pagePath: '/pages/index/index',
        text: '首页',
        icon: '/images/tab_home.png',
        activeIcon: '/images/tab_home_active.png'
      },
      {
        pagePath: '/pages/device/device',
        text: '设备',
        icon: '/images/tab_device.png',
        activeIcon: '/images/tab_device_active.png'
      },
      {
        pagePath: '/pages/features/features',
        text: '功能',
        icon: '/images/tab_features.png',
        activeIcon: '/images/tab_features_active.png'
      },
      {
        pagePath: '/pages/map/map',
        text: '地图',
        icon: '/images/tab_map.png',
        activeIcon: '/images/tab_map_active.png'
      }
    ]
  },

  methods: {
    onTabTap(e) {
      const index = Number(e.currentTarget.dataset.index)
      const pagePath = e.currentTarget.dataset.path

      // 底部“设备”TAB：只要当前就停留在设备列表页，单击即触发无感刷新（无需双击/再次点击）
      if (index === 1 && this._isOnPage('pages/device/device')) {
        this._triggerRefresh('device')
        return
      }

      // 底部“地图”TAB：只要当前就停留在地图页，单击即触发无感刷新（地图数据静默重载 + TAB 转圈）
      if (index === 3 && this._isOnPage('pages/map/map')) {
        this._triggerRefresh('map')
        return
      }

      // 点击当前已选中的 tab：不重复跳转（兜底：route 不可用时仍按选中态处理无感刷新）
      if (index === this.data.selected) {
        if (index === 1) this._triggerRefresh('device')
        else if (index === 3) this._triggerRefresh('map')
        return
      }

      wx.switchTab({
        url: pagePath,
        fail: (err) => {
          console.error('切换 tab 失败:', err)
        }
      })
    },

    // 当前停留页面是否为指定路由页（route 为主，__route__ 兜底）
    _isOnPage(route) {
      const pages = getCurrentPages()
      const current = pages[pages.length - 1]
      if (!current) return false
      const currentRoute = current.route || current.__route__ || ''
      return currentRoute === route
    },

    // 通知对应页面执行无感刷新；转圈状态由页面写入本组件（deviceRefreshing / mapRefreshing）
    _triggerRefresh(type) {
      const refreshingKey = type === 'map' ? 'mapRefreshing' : 'deviceRefreshing'
      if (this.data[refreshingKey]) return
      const pages = getCurrentPages()
      const current = pages[pages.length - 1]
      const handler = type === 'map' ? 'onMapTabRefresh' : 'onDeviceTabRefresh'
      if (current && typeof current[handler] === 'function') {
        current[handler]()
      }
    }
  }
})
