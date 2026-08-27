// config/report-interval.js — 上报周期与数据记录间隔校验
// 逻辑：
//   1. 只取 TYPE=1(定位) / TYPE=2(对时) 记录的时间（服务器接收时间，与列表显示一致）
//   2. 同一上报可能经多个中继转发，各中继时间差≤10秒，视为同一个上报时间点
//   3. 按时间升序后，从最近一对时间点向前找间隔：若两个时间点之间有 TYPE=6(设置/配置) 记录，
//      说明该间隔可能被人为操作打断，忽略这对，继续向前找更早的一对
//   4. 与配置上报周期比较：偏差 ≤ 周期一半 → 正常；否则 → 异常
//   5. 时间点不足2个，或所有相邻对中间都有 TYPE=6（数据不够）时返回 null，不判定

const MERGE_GAP_MS = 10 * 1000 // 同一上报经多个中继转发的时间差上限（10秒内视为同一时间）

// 从已解析记录中提取上报时间点序列（升序、10秒内合并去重）
// @param {Array} records 已解析记录（含 msgType、rawTime）
// @returns {number[]} 毫秒时间戳数组
function extractReportPoints(records) {
  const times = (records || [])
    .filter(r => (r.msgType === '1' || r.msgType === '2') && r.rawTime && r.rawTime !== '-')
    .map(r => new Date(r.rawTime).getTime())
    .filter(t => !isNaN(t))
    .sort((a, b) => a - b)

  // 10秒内的多中继重复上报合并为同一时间点（保留首个到达时间）
  const points = []
  for (const t of times) {
    const last = points[points.length - 1]
    if (last !== undefined && (t - last) <= MERGE_GAP_MS) continue
    points.push(t)
  }
  return points
}

/**
 * 分析最近上报间隔并与配置周期比较
 * @param {Array} records 已解析记录（含 msgType、rawTime）
 * @param {number|string} periodMin 配置上报周期（分钟）
 * @returns {object|null} { actualMs, periodMs, actualMin, abnormal }
 *   数据不足（时间点<2）或周期无效时返回 null
 */
function analyzeReportInterval(records, periodMin) {
  const period = parseInt(periodMin, 10)
  if (!period || isNaN(period) || period <= 0) return null

  const points = extractReportPoints(records)
  if (points.length < 2) return null // 数据不足，无法判断

  // TYPE=6(设置/配置) 记录时间：两个时间点之间出现设置操作时，间隔可能被人为操作打断，忽略该间隔
  const type6Times = (records || [])
    .filter(r => r.msgType === '6' && r.rawTime && r.rawTime !== '-')
    .map(r => new Date(r.rawTime).getTime())
    .filter(t => !isNaN(t))

  const periodMs = period * 60 * 1000

  // 从最近一对时间点向前找：中间有 TYPE=6 的间隔不用于判定
  for (let i = points.length - 1; i >= 1; i--) {
    const a = points[i - 1]
    const b = points[i]
    const hasConfigBetween = type6Times.some(t6 => t6 > a && t6 < b)
    if (hasConfigBetween) continue // 中间有设置操作，忽略这对，继续向前找

    const actualMs = b - a
    const deviation = Math.abs(actualMs - periodMs)
    return {
      actualMs,
      periodMs,
      actualMin: actualMs / 60000,
      abnormal: deviation > periodMs / 2
    }
  }

  return null // 所有相邻时间点之间都有 TYPE=6，无法判断
}

module.exports = {
  extractReportPoints,
  analyzeReportInterval,
  MERGE_GAP_MS
}
