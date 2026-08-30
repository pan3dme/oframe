// config/battery-swap.js — 换电时间分析与永久缓存
// 逻辑：
//   1. 只有对时信息（type=2）中包含电量，lorastr 格式 2|device|时间|电量|...，电量在 parts[3]。
//   2. 每台设备永久缓存"上次换电"记录（电量最高点 + 时间）与当前电池最低点，缓存不清除。
//   3. 无换电记录时，取扫描到的最高电量对时记录作为"上次换电"基准。
//   4. 换电判定：新记录电量回到满电水平（≥原最高电量），
//      且中间出现的最低点比原最高电量低≥20个百分点（证明电池确实用掉一大截），
//      则视为换电/重新上电：新记录替换原最高记录，并清空最低点。

const API_URL = getApp().globalData.api_device_Url

const SWAP_CACHE_KEY = 'battery_swap_cache_v5'
const PAGE_SIZE = 100 // 每页拉取记录数
const MAX_PAGES_FIRST = 15 // 无缓存时首次扫描最多页数（最多1500条）
const MAX_PAGES_UPDATE = 2 // 已有缓存后只扫描最近几页（最多200条）
const MIN_VALID_BATTERY = 0.05 // 忽略过低无效读数
// 换电判定阈值：
const SWAP_MAX_LEVEL = 0.85   // 电量≥85%视为满电级别（新电池，用于"疑似错过换电"加深扫描判断）
const SWAP_DROP_DELTA = 0.20  // 最低点比原最高电量低≥20个百分点，证明电池确实用掉一大截，换电判定才有效
const SWAP_GAP_MS = 24 * 60 * 60 * 1000 // 与缓存换电记录间隔超过24小时
// 2025年之前的记录视为历史脏数据，分析/缓存时忽略（设备上线时间在2025年后）
const MIN_VALID_SWAP_TIME = Date.UTC(2025, 0, 1)

// ==================== 永久缓存 ====================

function getSwapCache() {
  try {
    const data = wx.getStorageSync(SWAP_CACHE_KEY)
    if (data && data.devices) return data
  } catch (e) {
    console.error('[换电缓存] 读取失败:', e)
  }
  return { version: 5, devices: {}, updatedAt: 0 }
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

    // 只处理 type=2 的对时记录
    const lorastr = attr.lorastr || record.lorastr || ''
    if (!lorastr || !String(lorastr).startsWith('2|')) return

    // type=2 记录的实际设备ID在 lorastr 的 parts[1]（中继转发场景下 record.deviceId 是中继设备）
    const lorastrParts = String(lorastr).split('|')
    const actualDevice = lorastrParts.length >= 2 ? lorastrParts[1] : ''

    // 设备过滤：实际设备优先，缺失时回退到 record.deviceId
    if (deviceId) {
      const matchDevice = actualDevice || recDevice
      if (matchDevice && matchDevice !== deviceId) return
    }

    // 设备真实时间在 lorastr parts[2]（Unix 秒级时间戳），外层 record.time 是服务器接收时间
    let t = 0
    if (lorastrParts.length >= 3) {
      const lorastrTs = parseInt(lorastrParts[2], 10)
      if (!isNaN(lorastrTs) && lorastrTs > 0) {
        t = lorastrTs < 1e10 ? lorastrTs * 1000 : lorastrTs // 10位秒 → 毫秒，13位毫秒原样
      }
    }
    if (!t) {
      const rawTime = attr.time || record.time || ''
      t = new Date(rawTime).getTime()
    }
    if (isNaN(t) || t <= 0) return
    const rawTime = new Date(t).toISOString()

    // 电量在 lorastr parts[3]（对时信息里），外层 record.battery 可能为空或不同来源
    let b = lorastrParts.length >= 4 ? parseBatteryValue(lorastrParts[3]) : null
    if (b === null) {
      b = parseBatteryValue(attr.battery != null ? attr.battery : record.battery)
    }
    if (b === null) return
    samples.push({ t, rawTime, b })
  })
  return samples
}

// ==================== 换电分析 ====================

// 按时间升序样本，跟踪"上次换电"（电量最高点）与当前电池最低点：
//   - 无缓存时，取扫描窗口内电量最高的对时记录作为"上次换电"基准（新电池上电时电量最高）
//   - 之后正常放电，只跟踪最低电量点
//   - 换电判定：新记录电量回到满电水平（≥原最高电量），
//     且中间出现的最低点比原最高电量低≥SWAP_DROP_DELTA（电池确实用掉一大截），
//     则视为换电/重新上电：新记录替换原最高记录，并清空最低点
//   - 不满足条件的高电量记录不更新最高点，避免小波动/未充分放电误判换电
// @param {object|null} cachedHighest 缓存中的上次换电记录（其时间只前进不回退）
function analyzeSamples(samples, cachedHighest) {
  const asc = samples
    .filter(s => s.b >= MIN_VALID_BATTERY && s.t >= MIN_VALID_SWAP_TIME)
    .sort((a, b) => a.t - b.t)
  if (!asc.length) {
    return { highest: cachedHighest || null, lowest: null }
  }

  // 基准：优先用缓存中的上次换电记录（防止扫描窗口偏移导致换电时间回退/漂移）；
  //   缓存对象为 { time, timeStr, battery }，统一转成样本结构 { t, rawTime, b } 便于内部比较
  // 无缓存时取扫描窗口内电量最高的样本作为"上次换电"记录：
  //   新电池上电时电量最高，其时间即上次上电/换电时间
  //   多个相同最高电量时取时间最早的（首次上电时刻，避免平稳高电量导致时间漂移）
  let highest = cachedHighest
  if (highest) {
    highest = { t: highest.time || 0, rawTime: highest.timeStr || '', b: highest.battery }
  }
  if (!highest) {
    highest = asc[0]
    for (let i = 1; i < asc.length; i++) {
      if (asc[i].b > highest.b ||
          (asc[i].b === highest.b && asc[i].t < highest.t)) {
        highest = asc[i]
      }
    }
  }
  let lowest = null

  for (let i = 0; i < asc.length; i++) {
    const s = asc[i]
    // 跳过早于当前换电记录的样本
    if (s.t <= highest.t) continue

    // 换电判定：电量回到满电水平（≥原最高电量），
    // 且中间存在最低点，且最低点比原最高电量低≥20个百分点
    if (s.b >= highest.b &&
        lowest &&
        (highest.b - lowest.b) >= SWAP_DROP_DELTA) {
      // 视为换电/重新上电：新记录替换原最高记录，清空最低点
      highest = s
      lowest = null
      continue
    }

    // 正常放电（低于最高电量的记录）→ 只跟踪最低点
    if (s.b < highest.b) {
      if (!lowest || s.b < lowest.b) lowest = s
    }
  }

  // 整段电量都在下降时（未出现换电），最低电量取扫描范围内最后一条最低记录
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

function fetchAndAnalyze(deviceId, scanPages, cachedHighest, callback) {
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
        // 以原始记录数判断是否还有更多数据（电量样本只是其中一部分，不能按样本数判断）
        const noMore = rawCount < PAGE_SIZE
        console.log('[换电分析] 第' + page + '页返回' + rawCount +
          '条, 电量样本' + samples.length + '条, 累计' + allSamples.length + '条')
        if (page === 0) {
          const asc = allSamples.slice().sort((a, b) => a.t - b.t)
          console.log('[换电分析] 样本明细:', asc.map(s => s.rawTime + '→' + Math.round(s.b * 100) + '%').join('; ') || '（无）')
        }
        // 疑似错过换电：最新页最早样本已接近满电，且晚于缓存换电时间较多 →
        // 加深扫描寻找换电前的低点，让状态机正确识别跳变
        if (page === 0 && cachedHighest && !noMore && scanPages < MAX_PAGES_FIRST) {
          const earliest = allSamples.slice().sort((a, b) => a.t - b.t)[0]
          if (earliest && earliest.b >= SWAP_MAX_LEVEL && (earliest.t - cachedHighest.t) >= SWAP_GAP_MS) {
            console.log('[换电分析] 最新页起点已接近满电且晚于缓存换电，疑似期间换电，加深扫描')
            scanPages = MAX_PAGES_FIRST
          }
        }
        // 无更多数据 / 达到页数上限，停止扫描
        if (noMore || page + 1 >= scanPages) {
          callback(analyzeSamples(allSamples, cachedHighest), allSamples.length)
        } else {
          fetchPage(page + 1)
        }
      },
      fail: (err) => {
        console.error('[换电分析] 日志请求失败:', err)
        callback(analyzeSamples(allSamples, cachedHighest), allSamples.length)
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
  let cachedSwap = deviceData && deviceData.highest ? deviceData.highest : null

  // 忽略 2025 年之前的换电记录（历史脏数据），视为无缓存重新全量扫描
  if (cachedSwap && cachedSwap.time < MIN_VALID_SWAP_TIME) {
    console.log('[换电分析]', deviceId, '缓存换电时间早于2025年，忽略并重新扫描')
    cachedSwap = null
  }

  const scanPages = cachedSwap ? MAX_PAGES_UPDATE : MAX_PAGES_FIRST

  if (!cachedSwap) {
    console.log('[换电分析]', deviceId, '缓存为空，开始扫描最近', scanPages, '页日志')
  }

  fetchAndAnalyze(deviceId, scanPages, cachedSwap, (summary, scanned) => {
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
  formatRelativeTime,
  MIN_VALID_SWAP_TIME
}
