// utils/time-window-codec.js
// DTU 时间窗口（开机时间 / GPS工作时间）两位 base62 代号编解码，与服务器一致
// 规则：start(0-23)，end 允许填 24（>=24 自动转 23，代表 23:59）；最小时长 1 小时；仅当天
const TIME_DICT = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'

// start(0-23), end(0-24) → index；非法返回 -1
function timeWindowToIndex(start, end) {
  const realEnd = end >= 24 ? 23 : end
  if (start >= realEnd) return -1

  let preCount = 0
  for (let s = 0; s < start; s++) {
    const valid = 23 - (s + 1) + 1
    if (valid > 0) preCount += valid
  }
  return preCount + (realEnd - (start + 1))
}

// index → 两位字符（base62）；非法返回 'XX'
function indexToTwoChar(idx) {
  if (idx < 0 || idx > 275) return 'XX'
  return TIME_DICT[Math.floor(idx / 62)] + TIME_DICT[idx % 62]
}

// 两位字符 → index；非法返回 -1
function twoCharToIndex(str) {
  if (!str || str.length !== 2) return -1
  const h = TIME_DICT.indexOf(str[0])
  const l = TIME_DICT.indexOf(str[1])
  if (h < 0 || l < 0) return -1
  return h * 62 + l
}

// index → { start, end }（end=23 业务上代表 23:59）；非法返回 null
function indexToTimeWindow(idx) {
  if (idx < 0) return null
  let sum = 0
  for (let s = 0; s <= 23; s++) {
    const valid = 23 - (s + 1) + 1
    if (valid <= 0) continue
    if (idx < sum + valid) {
      return { start: s, end: s + 1 + (idx - sum) }
    }
    sum += valid
  }
  return null
}

// start(0-23), end(0-24) → 两位代号；非法返回 null
function encodeTimeWindow(start, end) {
  const idx = timeWindowToIndex(start, end)
  if (idx < 0) return null
  return indexToTwoChar(idx)
}

// 两位代号 → { start, end }；非法返回 null
function decodeTimeWindow(code) {
  if (!code || code.length !== 2) return null
  const idx = twoCharToIndex(code)
  if (idx < 0) return null
  return indexToTimeWindow(idx)
}

// 兼容解析：两位代号 或 旧格式 "start-duration"（如 8-6 = 8点开始持续6小时）
// → { start, end }（end 为结束时刻，跨天无法表示）；无法解析返回 null
function parseTimeWindow(raw) {
  if (!raw) return null
  const s = String(raw).trim()
  // 优先：两位代号
  const win = decodeTimeWindow(s)
  if (win) return win
  // 兼容旧格式：start-duration
  const m = s.match(/^(\d{1,2})-(\d{1,2})$/)
  if (m) {
    const start = parseInt(m[1], 10)
    const end = start + parseInt(m[2], 10)
    if (start < end && end <= 24) {
      return { start, end: end === 24 ? 23 : end }
    }
  }
  return null
}

// 格式化显示：代号或旧格式 → "HH:00-HH:00"（end=23 显示 23:59）；无法解析返回 '-'
function formatTimeRange(raw) {
  const win = parseTimeWindow(raw)
  if (!win) return '-'
  const pad = (v) => String(v).padStart(2, '0')
  const endText = win.end === 23 ? '23:59' : pad(win.end) + ':00'
  return pad(win.start) + ':00-' + endText
}

module.exports = {
  encodeTimeWindow,
  decodeTimeWindow,
  parseTimeWindow,
  formatTimeRange
}
