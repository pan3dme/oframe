// test-report-interval.js — 上报周期校验算法测试（node 直接运行）
// 运行: node test-report-interval.js
const { extractReportPoints, analyzeReportInterval } = require('./config/report-interval.js')

// 构造记录：msgType + rawTime（服务器接收时间）
function rec(msgType, rawTime) {
  return { msgType, rawTime }
}

let pass = 0
let fail = 0
function check(name, actual, expected) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected)
  if (ok) {
    pass++
    console.log('PASS ' + name)
  } else {
    fail++
    console.log('FAIL ' + name + '\n  期望: ' + JSON.stringify(expected) + '\n  实际: ' + JSON.stringify(actual))
  }
}

const t = '2026-08-27 10:00:00'
const min = 60 * 1000

// Test 1: 周期5分钟，同一上报经3个中继转发（10秒内偏差），真实间隔5分钟 → 正常
{
  const recs = [
    rec('1', '2026-08-27 10:00:00'),
    rec('2', '2026-08-27 10:00:03'),
    rec('2', '2026-08-27 10:00:07'),
    rec('1', '2026-08-27 10:05:01'),
    rec('2', '2026-08-27 10:05:04'),
    rec('1', '2026-08-27 10:05:07')
  ]
  const points = extractReportPoints(recs)
  check('Test1 合并后时间点', points, [new Date('2026-08-27 10:00:00').getTime(), new Date('2026-08-27 10:05:01').getTime()])
  const r = analyzeReportInterval(recs, 5)
  check('Test1 正常', r && { abnormal: r.abnormal, actualMin: Math.round(r.actualMin * 10) / 10 }, { abnormal: false, actualMin: 5.0 })
}

// Test 2: 周期5分钟，实际间隔10分钟（偏差5分钟 > 周期一半2.5分钟）→ 异常
{
  const recs = [
    rec('1', '2026-08-27 10:00:00'),
    rec('2', '2026-08-27 10:10:00')
  ]
  const r = analyzeReportInterval(recs, 5)
  check('Test2 异常', r && { abnormal: r.abnormal, actualMin: Math.round(r.actualMin * 10) / 10 }, { abnormal: true, actualMin: 10.0 })
}

// Test 3: 周期5分钟，实际间隔6分钟（偏差1分钟 ≤ 2.5分钟）→ 正常
{
  const recs = [
    rec('1', '2026-08-27 10:00:00'),
    rec('2', '2026-08-27 10:06:00')
  ]
  const r = analyzeReportInterval(recs, 5)
  check('Test3 偏差半周期内正常', r && { abnormal: r.abnormal }, { abnormal: false })
}

// Test 4: 周期5分钟，实际间隔2.5分钟（偏差恰好=周期一半）→ 正常（≤ 半周期）
{
  const recs = [
    rec('1', '2026-08-27 10:00:00'),
    rec('2', '2026-08-27 10:02:30')
  ]
  const r = analyzeReportInterval(recs, 5)
  check('Test4 偏差等于半周期正常', r && { abnormal: r.abnormal }, { abnormal: false })
}

// Test 5: 只有一个时间点 → 数据不足，返回 null
{
  const recs = [rec('1', '2026-08-27 10:00:00'), rec('2', '2026-08-27 10:00:03')]
  const r = analyzeReportInterval(recs, 5)
  check('Test5 数据不足返回null', r, null)
}

// Test 6: 只有 TYPE=3（电量）记录 → 不参与统计，数据不足
{
  const recs = [rec('3', '2026-08-27 10:00:00'), rec('3', '2026-08-27 10:05:00')]
  const r = analyzeReportInterval(recs, 5)
  check('Test6 非TYPE1/2不统计', r, null)
}

// Test 7: 周期无效 → 返回 null
{
  const recs = [rec('1', '2026-08-27 10:00:00'), rec('2', '2026-08-27 10:05:00')]
  const r = analyzeReportInterval(recs, 0)
  check('Test7 周期无效返回null', r, null)
}

// Test 8: TYPE=1 与 TYPE=2 同一时刻上报合并为一个时间点（混合多中继）
{
  const recs = [
    rec('1', '2026-08-27 10:00:00'),
    rec('2', '2026-08-27 10:00:04'),
    rec('1', '2026-08-27 10:04:58'),
    rec('2', '2026-08-27 10:05:02')
  ]
  const points = extractReportPoints(recs)
  check('Test8 TYPE1/2同时刻合并', points, [new Date('2026-08-27 10:00:00').getTime(), new Date('2026-08-27 10:04:58').getTime()])
  const r = analyzeReportInterval(recs, 5)
  check('Test8 正常', r && { abnormal: r.abnormal }, { abnormal: false })
}

// Test 9: 最近一对时间点之间有 TYPE=6(设置) 记录 → 忽略这对，用更早一对判定
// 时间点: 10:00 / 10:05 / 10:33，中间 10:30 有 TYPE=6，最近对(10:05,10:33)被忽略，
// 使用(10:00,10:05)间隔5分钟，周期5分钟 → 正常
{
  const recs = [
    rec('1', '2026-08-27 10:00:00'),
    rec('1', '2026-08-27 10:05:00'),
    rec('6', '2026-08-27 10:30:00'),
    rec('2', '2026-08-27 10:33:00')
  ]
  const r = analyzeReportInterval(recs, 5)
  check('Test9 有TYPE=6忽略最近对', r && { abnormal: r.abnormal, actualMin: Math.round(r.actualMin * 10) / 10 }, { abnormal: false, actualMin: 5.0 })
}

// Test 10: 最近对之间有 TYPE=6 被忽略后，更早一对本身异常 → 仍判定异常
// 时间点: 10:00 / 10:10 / 10:33，中间 10:30 有 TYPE=6，
// 使用(10:00,10:10)间隔10分钟，周期5分钟 → 异常
{
  const recs = [
    rec('1', '2026-08-27 10:00:00'),
    rec('2', '2026-08-27 10:10:00'),
    rec('6', '2026-08-27 10:30:00'),
    rec('1', '2026-08-27 10:33:00')
  ]
  const r = analyzeReportInterval(recs, 5)
  check('Test10 忽略后更早对异常', r && { abnormal: r.abnormal, actualMin: Math.round(r.actualMin * 10) / 10 }, { abnormal: true, actualMin: 10.0 })
}

// Test 11: 所有相邻时间点之间都有 TYPE=6 → 无法判断，返回 null
// 时间点: 10:00 / 10:02 / 10:33，10:01 和 10:30 都有 TYPE=6
{
  const recs = [
    rec('1', '2026-08-27 10:00:00'),
    rec('6', '2026-08-27 10:01:00'),
    rec('1', '2026-08-27 10:02:00'),
    rec('6', '2026-08-27 10:30:00'),
    rec('2', '2026-08-27 10:33:00')
  ]
  const r = analyzeReportInterval(recs, 5)
  check('Test11 全部有TYPE=6返回null', r, null)
}

// Test 12: TYPE=6 与时间点 B 同时刻（非严格在两点之间）→ 不拦截，正常判定
// 时间点: 10:00 / 10:10，TYPE=6 在 10:10:00 与 B 同时刻，不拦截，
// 间隔10分钟，周期5分钟 → 异常
{
  const recs = [
    rec('1', '2026-08-27 10:00:00'),
    rec('2', '2026-08-27 10:10:00'),
    rec('6', '2026-08-27 10:10:00')
  ]
  const r = analyzeReportInterval(recs, 5)
  check('Test12 同时刻TYPE=6不拦截', r && { abnormal: r.abnormal, actualMin: Math.round(r.actualMin * 10) / 10 }, { abnormal: true, actualMin: 10.0 })
}

console.log('\n结果: ' + pass + ' 通过, ' + fail + ' 失败')
process.exit(fail > 0 ? 1 : 0)
