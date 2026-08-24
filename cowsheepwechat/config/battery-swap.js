// config/battery-swap.js — 换电时间分析与永久缓存
// 逻辑：
//   1. 只认 type=2 对时记录，lorastr 格式 2|device|时间|电量|...，电量在 parts[3]。
//   2. 从 getDeviceLogbyId 拉取最近日志，按时间升序分析。
//   3. 如果存在电量 > 0.8 的样本，取其中最新一条作为最新换电时间。
//   4. 如果所有样本电量都 ≤ 0.8（如全是 0.4/0.5），取最早一条作为最新换电时间。
//   5. 同时记录当前扫描到的最低电量样本与最新电量样本。

const API_URL = getApp().globalData.api_device_Url

const SWAP_CACHE_KEY = 'battery_swap_cache_v2'
const PAGE_SIZE = 100 // 每页拉取记录数
const MAX_PAGES_FIRST = 15 // 无缓存时首次扫描最多页数（最多1500条）
const MAX_PAGES_UPDATE = 2 // 已有缓存后只扫描最近几页（最多200条）
const HIGH_BATTERY_THRESHOLD = 0.8 // 高于此值视为换电后电量
const MIN_VALID_BATTERY = 0.05 // 忽略过低无效读数
const MAX_SWAPS_PER_DEVICE = 50 // 每台设备最多保留的换电记录条数

// ==================== 永久缓存 ====================

function getSwapCache() {
  try {
    const data = wx.getStorageSync(SWAP_CACHE_KEY)
    if (data && data.devices) return data
  } catch (e) {
    console.error('[换电缓存] 读取失败:', e)
  }
  return { version: 2, devices: {}, updatedAt: 0 }
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

// 按时间升序样本，分析最新换电事件、最低电量、最新电量
function analyzeSamples(samples) {
  const asc = samples
    .filter(s => s.b >= MIN_VALID_BATTERY)
    .sort((a, b) => a.t - b.t)
  if (!asc.length) {
    return { swaps: [], minBattery: null, latestBattery: null }
  }

  let minBattery = asc[0]
  for (let i = 1; i < asc.length; i++) {
    if (asc[i].b < minBattery.b) minBattery = asc[i]
  }
  const latestBattery = asc[asc.length - 1]

  // 高电量样本：> 0.8 视为换电后电量
  const highSamples = asc.filter(s => s.b > HIGH_BATTERY_THRESHOLD)
  let latestSwap = null
  if (highSamples.length > 0) {
    latestSwap = highSamples[highSamples.length - 1]
  } else {
    // 全部低电量时，最早那条就是换电后首次出现的记录
    latestSwap = asc[0]
  }

  const swaps = latestSwap ? [{
    time: latestSwap.t,
    timeStr: latestSwap.rawTime,
    battery: latestSwap.b
  }] : []

  return {
    swaps,
    minBattery: {
      time: minBattery.t,
      timeStr: minBattery.rawTime,
      battery: minBattery.b
    },
    latestBattery: {
      time: latestBattery.t,
      timeStr: latestBattery.rawTime,
      battery: latestBattery.b
    }
  }
}

// ==================== 合并缓存 ====================

function mergeSwaps(cache, deviceId, swaps) {
  if (!cache.devices) cache.devices = {}
  if (!cache.devices[deviceId]) cache.devices[deviceId] = { swaps: [] }
  const existing = cache.devices[deviceId].swaps
  swaps.forEach((sw) => {
    const dup = existing.some(e => Math.abs(e.time - sw.time) < 60000)
    if (!dup) existing.push(sw)
  })
  existing.sort((a, b) => b.time - a.time)
  if (existing.length > MAX_SWAPS_PER_DEVICE) {
    cache.devices[deviceId].swaps = existing.slice(0, MAX_SWAPS_PER_DEVICE)
  }
  return existing[0] || null
}

function updateDeviceSummary(cache, deviceId, summary) {
  if (!cache.devices) cache.devices = {}
  if (!cache.devices[deviceId]) cache.devices[deviceId] = { swaps: [] }
  const dev = cache.devices[deviceId]
  if (summary.minBattery) dev.minBattery = summary.minBattery
  if (summary.latestBattery) dev.latestBattery = summary.latestBattery
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
        // 已找到最近换电 / 无更多数据 / 达到页数上限，停止扫描
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
 * @param {function} callback - (latestSwap|null) 分析完成后的回调
 * @returns {object|null} 永久缓存中的最近换电（同步返回，界面可先展示）
 *   swap 结构: { time: 毫秒时间戳, timeStr: '2026/8/14 09:08:33', battery }
 */
function getLastSwap(deviceId, callback) {
  if (!deviceId) {
    if (callback) callback(null)
    return null
  }
  const cache = getSwapCache()
  const deviceData = cache.devices ? cache.devices[deviceId] : null
  const cachedSwap = deviceData && deviceData.swaps && deviceData.swaps.length > 0 ? deviceData.swaps[0] : null

  const scanPages = cachedSwap ? MAX_PAGES_UPDATE : MAX_PAGES_FIRST

  if (!cachedSwap) {
    console.log('[换电分析]', deviceId, '缓存为空，开始扫描最近', scanPages, '页日志')
  }

  fetchAndAnalyze(deviceId, scanPages, (summary, scanned) => {
    updateDeviceSummary(cache, deviceId, summary)
    const latest = mergeSwaps(cache, deviceId, summary.swaps)
    saveSwapCache(cache)
    console.log('[换电分析]', deviceId, '扫描样本:', scanned,
      '最低电量:', summary.minBattery ? Math.round(summary.minBattery.battery * 100) + '%@' + summary.minBattery.timeStr : '无',
      '最新电量:', summary.latestBattery ? Math.round(summary.latestBattery.battery * 100) + '%@' + summary.latestBattery.timeStr : '无',
      '最新换电:', latest ? latest.timeStr : '无')
    if (callback) callback(latest)
  })

  return cachedSwap
}

/**
 * 获取设备电量汇总（最低电量、最新电量、最近换电）
 * @param {string} deviceId
 * @returns {object|null}
 */
function getBatterySummary(deviceId) {
  if (!deviceId) return null
  const cache = getSwapCache()
  const dev = cache.devices ? cache.devices[deviceId] : null
  if (!dev) return null
  return {
    minBattery: dev.minBattery || null,
    latestBattery: dev.latestBattery || null,
    lastSwap: dev.swaps && dev.swaps.length > 0 ? dev.swaps[0] : null
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
