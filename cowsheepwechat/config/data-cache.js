// config/data-cache.js — 全局数据缓存模块
// 设备列表和牛羊列表缓存，避免每次页面切换都请求服务器

const app = getApp()
const API_URL = app.globalData.apiUrl

// ==================== 设备数据缓存 ====================

/**
 * 获取设备列表数据
 * @param {function} callback - 回调 (cachedData)，cachedData 为 { recordList, deviceIdOptions, deviceBindMap }
 * @param {boolean} forceRefresh - 是否强制刷新
 */
function getDeviceList(callback, forceRefresh) {
  if (!forceRefresh && app.globalData.deviceCache) {
    // 命中缓存直接返回
    callback(app.globalData.deviceCache)
    return
  }

  wx.request({
    url: API_URL,
    method: 'POST',
    data: { action: 'getDeviceTaleAll' },
    success: (res) => {
      const recordList = _parseDeviceRecords(res.data)
      const idSet = new Set()
      const bindMap = {}
      recordList.forEach(r => {
        if (r.deviceId && r.deviceId !== '-') {
          idSet.add(r.deviceId)
          if (!bindMap[r.deviceId] && r.link_cowsheep_id) {
            bindMap[r.deviceId] = r.link_cowsheep_id
          }
        }
      })
      const deviceIdOptions = Array.from(idSet).sort()
      deviceIdOptions.unshift('未连接')

      const cachedData = { recordList, deviceIdOptions, deviceBindMap: bindMap }
      app.globalData.deviceCache = cachedData
      callback(cachedData)
    },
    fail: (err) => {
      console.error('获取设备列表失败:', err)
      // 返回旧缓存兜底
      if (app.globalData.deviceCache) {
        callback(app.globalData.deviceCache)
      }
    }
  })
}

/**
 * 强制刷新设备数据
 */
function refreshDeviceList(callback) {
  getDeviceList(callback, true)
}

// 解析设备记录（与 detail.js 的 parseRecordList 一致）
function _parseDeviceRecords(data) {
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
    const device_key = attr.device_key || record.device_key || ''
    const rename = attr.rename || record.rename || ''
    const lorastr = attr.lorastr || record.lorastr || '-'
    const link_cowsheep_id = attr.link_cowsheep_id || record.link_cowsheep_id || ''
    const picurl = attr.picurl || record.picurl || ''
    const rawTime = attr.time || record.time || '-'
    const [date, time_part] = rawTime.includes(' ') ? rawTime.split(' ') : [rawTime, '']
    return { deviceId, device_key, rename, lorastr, link_cowsheep_id, picurl, date: date || '-', time_part: time_part || '', rawTime }
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
}

// ==================== 牛羊列表缓存 ====================

/**
 * 获取牛羊列表数据
 * @param {function} callback - 回调 (cachedData)，cachedData 为 { livestockList, livestockNames }
 * @param {boolean} forceRefresh - 是否强制刷新
 */
function getLivestockList(callback, forceRefresh) {
  if (!forceRefresh && app.globalData.livestockCache) {
    callback(app.globalData.livestockCache)
    return
  }

  wx.request({
    url: API_URL,
    method: 'POST',
    data: { action: 'getLivestockList' },
    success: (res) => {
      const list = []
      const data = res.data
      if (data && data.data && Array.isArray(data.data)) {
        data.data.forEach(item => {
          let name = ''
          let cowsheepId = ''
          let birthday = ''
          let gender = false
          if (item.primaryKey && Array.isArray(item.primaryKey)) {
            item.primaryKey.forEach(pk => {
              if (pk.name === 'cowsheep_id') cowsheepId = String(pk.value)
            })
          }
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
            list.push({ name, cowsheepId, birthday: birthday || '-', gender: gender ? '公' : '母', avatar })
          }
        })
      }
      const names = list.map(item => item.name)
      const cachedData = { livestockList: list, livestockNames: names }
      app.globalData.livestockCache = cachedData
      callback(cachedData)
    },
    fail: (err) => {
      console.error('获取牛羊列表失败:', err)
      if (app.globalData.livestockCache) {
        callback(app.globalData.livestockCache)
      }
    }
  })
}

/**
 * 强制刷新牛羊列表
 */
function refreshLivestockList(callback) {
  getLivestockList(callback, true)
}

/**
 * 清除所有缓存（一般不需要手动调用）
 */
function clearCache() {
  app.globalData.deviceCache = null
  app.globalData.livestockCache = null
}

module.exports = {
  getDeviceList,
  refreshDeviceList,
  getLivestockList,
  refreshLivestockList,
  clearCache
}
