// 独立测试 battery-swap 的解析和分析逻辑

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
const SWAP_JUMP_DELTA = 0.15

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
  if (!highest) {
    highest = asc[0]
    for (let i = 1; i < asc.length; i++) {
      if (asc[i].b > highest.b ||
          (asc[i].b === highest.b && asc[i].t > highest.t)) {
        highest = asc[i]
      }
    }
  }
  let lowest = null

  for (let i = 0; i < asc.length; i++) {
    const s = asc[i]
    if (s.t <= highest.t) continue

    if (lowest && (s.b - lowest.b) >= SWAP_JUMP_DELTA) {
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

// 模拟截图中的5条对时记录（v4-10，由不同中继转发）
const mockData = {
  data: [
    {
      deviceId: 'v4-10',  // 实际设备
      upDateDevice: 'v4-27',
      attributes: [
        { columnName: 'deviceId', columnValue: 'v4-10' },
        { columnName: 'upDateDevice', columnValue: 'v4-27' },
        { columnName: 'lorastr', columnValue: '2|v4-10|1787689260|99|52' },
        { columnName: 'time', columnValue: '2026-08-25 20:20:47' },
        { columnName: 'rssi', columnValue: '-64' },
        { columnName: 'snr', columnValue: '8' }
      ],
      primaryKey: [{ name: 'id', value: 'r1' }]
    },
    {
      deviceId: 'v4-10',
      upDateDevice: 'v4-20',
      attributes: [
        { columnName: 'deviceId', columnValue: 'v4-10' },
        { columnName: 'upDateDevice', columnValue: 'v4-20' },
        { columnName: 'lorastr', columnValue: '2|v4-10|1787689260|99|52' },
        { columnName: 'time', columnValue: '2026-08-25 20:20:46' },
        { columnName: 'rssi', columnValue: '-100' },
        { columnName: 'snr', columnValue: '6' }
      ]
    },
    {
      deviceId: 'v4-10',
      upDateDevice: 'v4-22',
      attributes: [
        { columnName: 'deviceId', columnValue: 'v4-10' },
        { columnName: 'upDateDevice', columnValue: 'v4-22' },
        { columnName: 'lorastr', columnValue: '2|v4-10|1787689260|99|52' },
        { columnName: 'time', columnValue: '2026-08-25 20:20:46' }
      ]
    },
    {
      deviceId: 'v4-10',
      upDateDevice: 'v4-29',
      attributes: [
        { columnName: 'deviceId', columnValue: 'v4-10' },
        { columnName: 'upDateDevice', columnValue: 'v4-29' },
        { columnName: 'lorastr', columnValue: '2|v4-10|1787689260|99|52' },
        { columnName: 'time', columnValue: '2026-08-25 20:20:46' }
      ]
    },
    {
      deviceId: 'v4-10',
      upDateDevice: 'v4-29',
      attributes: [
        { columnName: 'deviceId', columnValue: 'v4-10' },
        { columnName: 'upDateDevice', columnValue: 'v4-29' },
        { columnName: 'lorastr', columnValue: '2|v4-10|1787682060|99|50' },
        { columnName: 'time', columnValue: '2026-08-25 18:20:58' }
      ]
    }
  ]
}

console.log('=== Test 1: standard format with deviceId in attributes ===')
const samples = extractBatterySamples(mockData, 'v4-10')
console.log('Extracted samples:', samples.length)
const summary = analyzeSamples(samples, null)
console.log('Summary:', JSON.stringify(summary, null, 2))
console.log()
console.log('Last swap time (display):', summary.highest.timeStr, 'battery:', summary.highest.battery)

// 再测试一种格式：API 直接用 record.deviceId 作为中继设备，actual device 在 lorastr 中
console.log()
console.log('=== Test 2: record.deviceId is relay, actual device in lorastr ===')
const mockData2 = {
  data: mockData.data.map(r => {
    const relay = r.attributes.find(a => a.columnName === 'upDateDevice').columnValue
    return {
      ...r,
      deviceId: relay,  // 顶层 deviceId 是中继
      attributes: r.attributes.filter(a => a.columnName !== 'deviceId')  // 移除 attributes 里的 deviceId
    }
  })
}
const samples2 = extractBatterySamples(mockData2, 'v4-10')
console.log('Extracted samples:', samples2.length)
const summary2 = analyzeSamples(samples2, null)
console.log('Summary:', JSON.stringify(summary2, null, 2))
console.log('Last swap time (display):', summary2.highest.timeStr, 'battery:', summary2.highest.battery)
