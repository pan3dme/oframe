// 独立测试 battery-swap 的解析和分析逻辑（v5：换电判定基于"放电≥20个百分点后电量回满"）

// 复制关键的解析函数（去掉 getApp 依赖）
function parseBatteryValue(v) {
  if (v === null || v === undefined || v === '') return null
  const n = parseFloat(v)
  if (isNaN(n) || n <= 0) return null
  return n > 1 ? n / 100 : n
}

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

    const lorastr = attr.lorastr || record.lorastr || ''
    console.log('  [record] recDevice=' + JSON.stringify(recDevice) + ', lorastr=' + JSON.stringify(lorastr))
    if (!lorastr || !String(lorastr).startsWith('2|')) {
      console.log('    -> skip: not type=2')
      return
    }

    const lorastrParts = String(lorastr).split('|')
    const actualDevice = lorastrParts.length >= 2 ? lorastrParts[1] : ''

    if (deviceId) {
      const matchDevice = actualDevice || recDevice
      console.log('    matchDevice=' + JSON.stringify(matchDevice) + ', deviceId=' + JSON.stringify(deviceId) + ', match=' + (matchDevice === deviceId))
      if (matchDevice && matchDevice !== deviceId) {
        console.log('    -> skip: device mismatch')
        return
      }
    }

    let t = 0
    if (lorastrParts.length >= 3) {
      const lorastrTs = parseInt(lorastrParts[2], 10)
      if (!isNaN(lorastrTs) && lorastrTs > 0) {
        t = lorastrTs < 1e10 ? lorastrTs * 1000 : lorastrTs
      }
    }
    if (!t) {
      const rawTime = attr.time || record.time || ''
      t = new Date(rawTime).getTime()
    }
    console.log('    t=' + t + ' (date=' + new Date(t).toISOString() + ')')
    if (isNaN(t) || t <= 0) {
      console.log('    -> skip: invalid time')
      return
    }
    const rawTime = new Date(t).toISOString()

    let b = lorastrParts.length >= 4 ? parseBatteryValue(lorastrParts[3]) : null
    if (b === null) {
      b = parseBatteryValue(attr.battery != null ? attr.battery : record.battery)
    }
    console.log('    b=' + b)
    if (b === null) {
      console.log('    -> skip: invalid battery')
      return
    }
    samples.push({ t, rawTime, b })
  })
  return samples
}

const MIN_VALID_BATTERY = 0.05
const SWAP_DROP_DELTA = 0.20

function analyzeSamples(samples, cachedHighest) {
  const asc = samples
    .filter(s => s.b >= MIN_VALID_BATTERY)
    .sort((a, b) => a.t - b.t)
  console.log('asc samples count:', asc.length)
  asc.forEach(s => console.log('  ' + new Date(s.t).toISOString() + ' -> ' + (s.b * 100) + '%'))
  if (!asc.length) {
    return { highest: cachedHighest || null, lowest: null }
  }

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
    if (s.t <= highest.t) continue

    // 换电判定：电量回到满电水平（≥原最高电量），且中间最低点比原最高电量低≥20个百分点
    if (s.b >= highest.b &&
        lowest &&
        (highest.b - lowest.b) >= SWAP_DROP_DELTA) {
      console.log('  -> 判定换电: ' + Math.round(s.b * 100) + '% @ ' + s.rawTime +
        ' (原最高 ' + Math.round(highest.b * 100) + '%, 最低 ' + Math.round(lowest.b * 100) + '%)')
      highest = s
      lowest = null
      continue
    }

    if (s.b < highest.b) {
      if (!lowest || s.b < lowest.b) lowest = s
    }
  }

  if (!lowest) lowest = asc[asc.length - 1]

  const fmt = s => ({ time: s.t, timeStr: s.rawTime, battery: s.b })
  return { highest: fmt(highest), lowest: fmt(lowest) }
}

// 构造一条 type=2 记录
function makeRecord(id, lorastr, time, rssi, snr) {
  return {
    deviceId: 'v4-10',
    upDateDevice: 'v4-27',
    attributes: [
      { columnName: 'deviceId', columnValue: 'v4-10' },
      { columnName: 'upDateDevice', columnValue: 'v4-27' },
      { columnName: 'lorastr', columnValue: lorastr },
      { columnName: 'time', columnValue: time },
      { columnName: 'rssi', columnValue: rssi },
      { columnName: 'snr', columnValue: snr }
    ],
    primaryKey: [{ name: 'id', value: id }]
  }
}

const t0 = 1787689260 // 基准时间（秒级时间戳）

console.log('=== Test 1: 带缓存 + 完整放电回满（缓存90% + 85/70/64/92 应判定换电） ===')
const cached1 = { time: t0 * 1000, timeStr: '2026-08-25T20:21:00.000Z', battery: 0.9 }
const mockData1 = { data: [
  makeRecord('r1', '2|v4-10|' + (t0 + 3600) + '|85|52', '2026-08-25 21:20:47', '-60', '8'),
  makeRecord('r2', '2|v4-10|' + (t0 + 7200) + '|70|52', '2026-08-25 22:20:47', '-61', '7'),
  makeRecord('r3', '2|v4-10|' + (t0 + 10800) + '|64|52', '2026-08-25 23:20:47', '-62', '6'),
  makeRecord('r4', '2|v4-10|' + (t0 + 14400) + '|92|52', '2026-08-26 00:20:47', '-58', '9')
]}
const samples1 = extractBatterySamples(mockData1, 'v4-10')
console.log('Extracted samples:', samples1.length)
const summary1 = analyzeSamples(samples1, cached1)
console.log('Summary:', JSON.stringify(summary1, null, 2))
console.log('期望: 判定换电 → 上次换电 = 92% @ r4 时间')
console.log()

console.log('=== Test 2: 带缓存 + 放电不足20%（缓存90% + 88/87/90 不判定换电） ===')
const cached2 = { time: t0 * 1000, timeStr: '2026-08-25T20:21:00.000Z', battery: 0.9 }
const mockData2 = { data: [
  makeRecord('r1', '2|v4-10|' + (t0 + 3600) + '|88|52', '2026-08-25 21:20:47', '-60', '8'),
  makeRecord('r2', '2|v4-10|' + (t0 + 7200) + '|87|52', '2026-08-25 22:20:47', '-61', '7'),
  makeRecord('r3', '2|v4-10|' + (t0 + 10800) + '|90|52', '2026-08-25 23:20:47', '-62', '6')
]}
const samples2 = extractBatterySamples(mockData2, 'v4-10')
console.log('Extracted samples:', samples2.length)
const summary2 = analyzeSamples(samples2, cached2)
console.log('Summary:', JSON.stringify(summary2, null, 2))
console.log('期望: 不判定 → 上次换电保持 90% @ 缓存时间')
console.log()

console.log('=== Test 3: 截图场景，无缓存（91% → 90%，平稳无放电，取最高91%为基准） ===')
const mockData3 = { data: [
  makeRecord('r1', '2|v4-10|1787822460|90|95', '2026-08-27 09:20:50', '-88', '6'),
  makeRecord('r2', '2|v4-10|1787822460|90|95', '2026-08-27 09:20:49', '-74', '7'),
  makeRecord('r3', '2|v4-10|1787822460|90|95', '2026-08-27 09:20:49', '-100', '6'),
  makeRecord('r4', '2|v4-10|1787822460|90|95', '2026-08-27 09:20:49', '-90', '5'),
  makeRecord('r5', '2|v4-10|1787818860|91|94', '2026-08-27 08:20:52', '-88', '8')
]}
const samples3 = extractBatterySamples(mockData3, 'v4-10')
console.log('Extracted samples:', samples3.length)
const summary3 = analyzeSamples(samples3, null)
console.log('Summary:', JSON.stringify(summary3, null, 2))
console.log('期望: 上次换电 = 91% @ r5 时间（无缓存取最高点）')
console.log()

console.log('=== Test 4: 带缓存再扫（缓存91%基准 + 新数据80/64/91 应判定换电） ===')
const cachedHighest = { time: 1787818860000, timeStr: '2026-08-26T08:21:00.000Z', battery: 0.91 }
const mockData4 = { data: [
  makeRecord('r1', '2|v4-10|1787822460|80|95', '2026-08-27 09:20:50', '-88', '6'),
  makeRecord('r2', '2|v4-10|1787826060|64|95', '2026-08-27 10:20:50', '-74', '7'),
  makeRecord('r3', '2|v4-10|1787829660|91|95', '2026-08-27 11:20:50', '-100', '6')
]}
const samples4 = extractBatterySamples(mockData4, 'v4-10')
console.log('Extracted samples:', samples4.length)
const summary4 = analyzeSamples(samples4, cachedHighest)
console.log('Summary:', JSON.stringify(summary4, null, 2))
console.log('期望: 判定换电 → 上次换电更新为 91% @ r3（最低64比91低27个百分点）')
