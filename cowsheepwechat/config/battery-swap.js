// config/battery-swap.js — 换电时间分析与永久缓存
// 逻辑：
//   1. 只有对时信息（type=2）中包含电量，lorastr 格式 2|device|时间|电量|...，电量在 parts[3]。
//   2. 从 getDeviceLogbyId 拉取日志，按时间升序遍历（一个循环）：
//      - 记录电量最高的那条记录（含时间）→ 设备刚换电/上电时电量最高，其时间即上次上电/换电时间
//      - 记录该最高记录之后出现的最低电量记录（含时间）→ 当前这节电池消耗到的最低点
//   3. 每台设备永久缓存这两条记录（最高电量记录 + 最低电量记录），即可得出上次换电时间。

const API_URL = getApp().globalData.api_device_Url

const SWAP_CACHE_KEY = 'battery_swap_cache_v3'
const PAGE_SIZE = 100 // 每页拉取记录数
const MAX_PAGES_FIRST = 15 // 无缓存时首次扫描最多页数（最多1500条）
const MAX_PAGES_UPDATE = 2 // 已有缓存后只扫描最近几页（最多200条）
const MIN_VALID_BATTERY = 0.05 // 忽略过低无效读数

// ==================== 永久缓存 ====================

function getSwapCache() {
  try {
    const data = wx.getStorageSync(SWAP_CACHE_KEY)
    if (data && data.devices) return data
  } catch (e) {
    console.error('[换电缓存] 读取失败:', e)
  }
  return { version: 3, devices: {}, updatedAt: 0 }
}

function saveSwapCache(cache) {
  try {
    cache.updatedAt = Date.now()
    wx.setStorageSync(SWAP_CACHE_KEY, cache)
  } catch (e) {
    console.error('[换电缓存] 保存失败:', e)
  }
}

// ==================== 数据解析 ====================

// 通用电量值解析：0~1 小数或 0~100 百分比，统一归一为 0~1，无效返回 null
function parseBatteryValue(v) {
  if (v === null || v === undefined || v === '') return null
  const n = parseFloat(v)
  if (isNaN(n) || n <= 0) return null
  return n > 1 ? n / 100 : n
}

// 从 lorastr 中解析电量，只认 type=2 对时记录：2|device|时间|电量|...
function parseBatteryFromLorastr(lorastr) {
  if (!lorastr) return null
  const parts = String(lorastr).split('|')
  if (parts[0] === '2' && parts.length >= 4) {
    return parseBatteryValue(parts[3])
  }
  return null
}

// 从接口返回中提取该设备的电量样本（仅 type=2 对时记录）
function extractBatterySamples(data, deviceId) {
  let rawList = []
  if (data && data.data && Array.isArray(data.data)) {
    rawList = data.data
  } else if (Array.isArray(data)) {
    rawList = data
  }
  const samples = []
  rawList.forEach((record) => {
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
    const recDevice = attr.deviceId || attr.deviceid || record.deviceId || record.deviceid || ''
    if (deviceId && recDevice && recDevice !== deviceId) return

    // 只处理 type=2 的对时记录
    const lorastr = attr.lorastr || record.lorastr || ''
    if (!lorastr || !String(lorastr).startsWith('2|')) return

    const rawTime = attr.time || record.time || ''
    const t = new Date(rawTime).getTime()
    if (isNaN(t)) return

    let b = parseBatteryValue(attr.battery != null ? attr.battery : record.battery)
    if (b === null) {
      b = parseBatteryFromLorastr(lorastr)
    }
    if (b === null) return
    samples.push({ t, rawTime, b })
  })
  return samples
}

// ==================== 换电分析 ====================

// 按时间升序样本，一个循环内：
//   highest → 电量最高的对时记录（其时间 = 上次上电/换电时间）
//   lowest  → 最高记录之后出现的最低电量记录（当前电池消耗到的最低点）
function analyzeSamples(samples) {
  const asc = samples
    .filter(s => s.b >= MIN_VALID_BATTERY)
    .sort((a, b) => a.t - b.t)
  if (!asc.length) {
    return { highest: null, lowest: null }
  }

  let highest = asc[0]
  let lowest = null
  for (let i = 1; i < asc.length; i++) {
    const s = asc[i]
    if (s.b > highest.b) {
      // 出现更高电量 → 视为新的"上次上电/换电"记录，其后的最低电量重新统计
      highest = s
      lowest = null
    } else if (!lowest || s.b < lowest.b) {
      lowest = s
    }
  }
  // 整段电量都在下降时（未出现更高电量），最低电量取扫描范围内最后一条最低记录
  if (!lowest) lowest = asc[asc.length - 1]

  const fmt = s => ({ time: s.t, timeStr: s.rawTime, battery: s.b })
  return { highest: fmt(highest), lowest: fmt(lowest) }
}

// ==================== 合并缓存 ====================

// 每台设备只保留两条记录：最高电量（上次上电/换电）+ 该记录之后的最低电量
function updateDeviceRecords(cache, deviceId, summary) {
  if (!cache.devices) cache.devices = {}
  if (!cache.devices[deviceId]) cache.devices[deviceId] = {}
  const dev = cache.devices[deviceId]
  if (summary.highest) dev.highest = summary.highest
  if (summary.lowest) dev.lowest = summary.lowest
}

// ==================== 拉取与分析主流程 ====================

function fetchAndAnalyze(deviceId, scanPages, callback) {
  let allSamples = []
  const fetchPage = (page) => {
    wx.request({
      url: API_URL,
      method: 'POST',
      data: {
        action: 'getDeviceLogbyId',
        info: { limit: PAGE_SIZE, deviceId: deviceId, offset: page * PAGE_SIZE, wechatid: getApp().getWechatId() },
        time: getApp().formatTime()
      },
      timeout: 8000,
      success: (res) => {
        const samples = extractBatterySamples(res.data, deviceId)
        allSamples = allSamples.concat(samples)
        const rawCount = (res.data && res.data.data && Array.isArray(res.data.data))
          ? res.data.data.length
          : (Array.isArray(res.data) ? res.data.length : 0)
        const noMore = samples.length < PAGE_SIZE
        console.log('[换电分析] 第' + page + '页返回' + rawCount +
          '条, 电量样本' + samples.length + '条, 累计' + allSamples.length + '条')
        if (page === 0) {
          const asc = allSamples.slice().sort((a, b) => a.t - b.t)
          console.log('[换电分析] 样本明细:', asc.map(s => s.rawTime + '→' + Math.round(s.b * 100) + '%').join('; ') || '（无）')
        }
        // 无更多数据 / 达到页数上限，停止扫描
        if (noMore || page + 1 >= scanPages) {
          callback(analyzeSamples(allSamples), allSamples.length)
        } else {
          fetchPage(page + 1)
        }
      },
      fail: (err) => {
        console.error('[换电分析] 日志请求失败:', err)
        callback(analyzeSamples(allSamples), allSamples.length)
      }
    })
  }
  fetchPage(0)
}

/**
 * 获取设备最近换电时间（自动分析 + 永久缓存）
 * @param {string} deviceId
 * @param {function} callback - (lastSwap|null) 分析完成后的回调
 * @returns {object|null} 永久缓存中的最近换电（同步返回，界面可先展示）
 *   swap 结构: { time: 毫秒时间戳, timeStr: '2026/8/14 09:08:33', battery }
 *   即电量最高的对时记录，其时间 = 上次上电/换电时间
 */
function getLastSwap(deviceId, callback) {
  if (!deviceId) {
    if (callback) callback(null)
    return null
  }
  const cache = getSwapCache()
  const deviceData = cache.devices ? cache.devices[deviceId] : null
  const cachedSwap = deviceData && deviceData.highest ? deviceData.highest : null

  const scanPages = cachedSwap ? MAX_PAGES_UPDATE : MAX_PAGES_FIRST

  if (!cachedSwap) {
    console.log('[换电分析]', deviceId, '缓存为空，开始扫描最近', scanPages, '页日志')
  }

  fetchAndAnalyze(deviceId, scanPages, (summary, scanned) => {
    updateDeviceRecords(cache, deviceId, summary)
    saveSwapCache(cache)
    const lastSwap = summary.highest || cachedSwap || null
    console.log('[换电分析]', deviceId, '扫描样本:', scanned,
      '最高电量(上次上电):', lastSwap ? Math.round(lastSwap.battery * 100) + '%@' + lastSwap.timeStr : '无',
      '最低电量:', summary.lowest ? Math.round(summary.lowest.battery * 100) + '%@' + summary.lowest.timeStr : '无')
    if (callback) callback(lastSwap)
  })

  return cachedSwap
}

/**
 * 获取设备电量汇总（最高电量、最低电量、最近换电）
 * @param {string} deviceId
 * @returns {object|null}
 */
function getBatterySummary(deviceId) {
  if (!deviceId) return null
  const cache = getSwapCache()
  const dev = cache.devices ? cache.devices[deviceId] : null
  if (!dev) return null
  return {
    highest: dev.highest || null,
    lowest: dev.lowest || null,
    lastSwap: dev.highest || null
  }
}

// ==================== 相对时间 ====================

// 格式化相对时间：刚刚 / x分钟前 / x小时前 / x天前
function formatRelativeTime(time) {
  if (!time) return ''
  const diff = Date.now() - time
  if (diff < 0 || isNaN(diff)) return ''
  const minute = 60 * 1000
  const hour = 60 * minute
  const day = 24 * hour
  if (diff < minute) return '刚刚'
  if (diff < hour) return Math.floor(diff / minute) + '分钟前'
  if (diff < day) return Math.floor(diff / hour) + '小时前'
  return Math.floor(diff / day) + '天前'
}

/**
 * 清除换电缓存（测试/调试用）
 */
function clearSwapCache() {
  try {
    wx.removeStorageSync(SWAP_CACHE_KEY)
  } catch (e) {
    console.error('[换电缓存] 清除失败:', e)
  }
}

module.exports = {
  getLastSwap,
  getBatterySummary,
  clearSwapCache,
  parseBatteryFromLorastr,
  formatRelativeTime
}
