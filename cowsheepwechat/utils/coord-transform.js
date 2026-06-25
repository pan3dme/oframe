// utils/coord-transform.js — WGS-84 / GCJ-02 坐标转换及道路坐标解析
// 统一提供正向/逆向转换 + 道路GPS解析，消除 6 个文件中的重复代码

/**
 * WGS-84 → GCJ-02（火星坐标系）正向转换
 * 高德地图原生使用 GCJ-02
 */
function wgs84ToGcj02(lng, lat) {
  const a = 6378245.0
  const ee = 0.00669342162296594323
  const x = +lng - 105.0
  const y = +lat - 35.0
  let dLat = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * Math.sqrt(Math.abs(x))
  dLat += (20.0 * Math.sin(6.0 * x * Math.PI) + 20.0 * Math.sin(2.0 * x * Math.PI)) * 2.0 / 3.0
  dLat += (20.0 * Math.sin(y * Math.PI) + 40.0 * Math.sin(y / 3.0 * Math.PI)) * 2.0 / 3.0
  dLat += (160.0 * Math.sin(y / 12.0 * Math.PI) + 320.0 * Math.sin(y * Math.PI / 30.0)) * 2.0 / 3.0
  let dLng = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * Math.sqrt(Math.abs(x))
  dLng += (20.0 * Math.sin(6.0 * x * Math.PI) + 20.0 * Math.sin(2.0 * x * Math.PI)) * 2.0 / 3.0
  dLng += (20.0 * Math.sin(x * Math.PI) + 40.0 * Math.sin(x / 3.0 * Math.PI)) * 2.0 / 3.0
  dLng += (150.0 * Math.sin(x / 12.0 * Math.PI) + 300.0 * Math.sin(x / 30.0 * Math.PI)) * 2.0 / 3.0
  const radLat = +lat / 180.0 * Math.PI
  let magic = Math.sin(radLat)
  magic = 1 - ee * magic * magic
  const sqrtMagic = Math.sqrt(magic)
  const dLatFinal = (dLat * 180.0) / ((a * (1 - ee)) / (magic * sqrtMagic) * Math.PI)
  const dLngFinal = (dLng * 180.0) / (a / sqrtMagic * Math.cos(radLat) * Math.PI)
  return { lat: +lat + dLatFinal, lng: +lng + dLngFinal }
}

/**
 * GCJ-02 → WGS-84 逆变换（牛顿迭代法）
 * 国产坐标偏移量约 300~500 米，3次迭代收敛到厘米级
 */
function gcj02ToWgs84(lng, lat) {
  const gcj = { lng: +lng, lat: +lat }
  for (let i = 0; i < 3; i++) {
    const wgs = { lng: lng - (gcj.lng - lng), lat: lat - (gcj.lat - lat) }
    const test = wgs84ToGcj02(wgs.lng, wgs.lat)
    const dLng = test.lng - lng
    const dLat = test.lat - lat
    if (Math.abs(dLng) < 1e-7 && Math.abs(dLat) < 1e-7) return wgs
    gcj.lng += dLng
    gcj.lat += dLat
  }
  return { lng: lng - (gcj.lng - lng), lat: lat - (gcj.lat - lat) }
}

/**
 * 解析 roadinfo 中的 GPS 坐标，构建点数组
 * 兼容格式：
 *   lat1,lng1|lat2,lng2|...        (逗号分隔经纬度，竖线分隔点)
 *   lat1|lng1|lat2|lng2|...        (竖线交替)
 *   lat1,lng1;lat2,lng2;...        (分号分隔点)
 *   lat1,lng1,lat2,lng2,...        (逗号交替平铺)
 */
function parseRoadPoints(roadinfo) {
  if (!roadinfo || roadinfo === '-') return []

  // Strategy 0: flat lat, lng, lat, lng, ... (逗号交替平铺)
  const commaAll = roadinfo.split(/[,，]\s*/)
  if (commaAll.length >= 4 && commaAll.length % 2 === 0) {
    const points = []
    let allValid = true
    for (let i = 0; i + 1 < commaAll.length; i += 2) {
      const lat = parseFloat(commaAll[i])
      const lng = parseFloat(commaAll[i + 1])
      if (isNaN(lat) || isNaN(lng)) { allValid = false; break }
      points.push({ lat, lng })
    }
    if (allValid && points.length >= 2) return points
  }

  // Strategy 1: split by | → for each segment try lat,lng
  const segs = roadinfo.split(/[｜|]/)
  if (segs.length >= 2) {
    const points = []
    for (const seg of segs) {
      const parts = seg.split(/[,，]\s*/)
      if (parts.length >= 2) {
        const lat = parseFloat(parts[0])
        const lng = parseFloat(parts[1])
        if (!isNaN(lat) && !isNaN(lng)) {
          points.push({ lat, lng })
        }
      }
    }
    if (points.length >= 2) return points
  }

  // Strategy 2: split by | → alternating lat|lng|lat|lng...
  if (segs.length >= 4) {
    const points = []
    for (let i = 0; i + 1 < segs.length; i += 2) {
      const lat = parseFloat(segs[i])
      const lng = parseFloat(segs[i + 1])
      if (!isNaN(lat) && !isNaN(lng)) {
        points.push({ lat, lng })
      }
    }
    if (points.length >= 2) return points
  }

  // Strategy 3: split by ; for point groups
  const semicolonSegs = roadinfo.split(';')
  if (semicolonSegs.length >= 2) {
    const points = []
    for (const seg of semicolonSegs) {
      const parts = seg.split(/[,，]\s*/)
      if (parts.length >= 2) {
        const lat = parseFloat(parts[0])
        const lng = parseFloat(parts[1])
        if (!isNaN(lat) && !isNaN(lng)) {
          points.push({ lat, lng })
        }
      }
    }
    if (points.length >= 2) return points
  }

  return []
}

/**
 * Haversine 公式计算两点距离（米）
 */
function calcDistance(lat1, lng1, lat2, lng2) {
  const R = 6371000
  const toRad = d => d * Math.PI / 180
  const dLat = toRad(lat2 - lat1)
  const dLng = toRad(lng2 - lng1)
  const a = Math.sin(dLat / 2) * Math.sin(dLat / 2)
    + Math.cos(toRad(lat1)) * Math.cos(toRad(lat2))
    * Math.sin(dLng / 2) * Math.sin(dLng / 2)
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a))
}

module.exports = {
  wgs84ToGcj02,
  gcj02ToWgs84,
  parseRoadPoints,
  calcDistance
}
