// utils/hmac-sha1.js — 纯 JS HMAC-SHA1（正确 32 位无符号运算）
// 适用于微信小程序环境（无 crypto.subtle）

// ==================== SHA-1 核心 ====================

/**
 * 将 UTF-8 字符串转为 32-bit 大端 word 数组
 */
function str2binb(str) {
  const bin = []
  const len = str.length
  for (let i = 0; i < len; i++) {
    bin[i >> 2] |= str.charCodeAt(i) << (24 - (i & 3) * 8)
  }
  // 补齐空洞（如果有）
  for (let j = 0; j < bin.length; j++) {
    bin[j] = bin[j] >>> 0
  }
  return bin
}

/**
 * 32-bit 安全加法（不溢出符号位）
 */
function safe_add(x, y) {
  const lsw = (x & 0xFFFF) + (y & 0xFFFF)
  const msw = (x >>> 16) + (y >>> 16) + (lsw >>> 16)
  return ((msw << 16) | (lsw & 0xFFFF)) >>> 0
}

/**
 * 32-bit 循环左移
 */
function rol(num, cnt) {
  return ((num << cnt) | (num >>> (32 - cnt))) >>> 0
}

/**
 * SHA-1 轮函数
 */
function sha1_ft(t, b, c, d) {
  if (t < 20) return (b & c) | ((~b) & d)
  if (t < 40) return b ^ c ^ d
  if (t < 60) return (b & c) | (b & d) | (c & d)
  return b ^ c ^ d
}

/**
 * SHA-1 轮常量
 */
function sha1_kt(t) {
  if (t < 20) return 0x5A827999
  if (t < 40) return 0x6ED9EBA1
  if (t < 60) return 0x8F1BBCDC
  return 0xCA62C1D6
}

/**
 * SHA-1 核心算法
 */
function core_sha1(x, len) {
  // 填充
  x[len >> 5] |= 0x80 << (24 - (len & 31))
  x[((len + 64 >> 9) << 4) + 15] = len

  const w = new Array(80)
  let a = 0x67452301, b = 0xEFCDAB89, c = 0x98BADCFE, d = 0x10325476, e = 0xC3D2E1F0

  for (let i = 0; i < x.length; i += 16) {
    const oa = a, ob = b, oc = c, od = d, oe = e
    for (let j = 0; j < 80; j++) {
      if (j < 16) {
        w[j] = (x[i + j] || 0) >>> 0
      } else {
        w[j] = rol(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1)
      }
      const t = safe_add(safe_add(rol(a, 5), sha1_ft(j, b, c, d)),
                         safe_add(safe_add(e, w[j]), sha1_kt(j)))
      e = d; d = c; c = rol(b, 30); b = a; a = t
    }
    a = safe_add(a, oa); b = safe_add(b, ob); c = safe_add(c, oc)
    d = safe_add(d, od); e = safe_add(e, oe)
  }
  return [a, b, c, d, e]
}

/**
 * word 数组 → hex 字符串（40 位）
 */
function binb2hex(binarray) {
  const hex_tab = '0123456789abcdef'
  let str = ''
  for (let i = 0; i < binarray.length; i++) {
    const word = binarray[i] >>> 0        // 强制无符号
    str += hex_tab.charAt((word >>> 28) & 0xF)
    str += hex_tab.charAt((word >>> 24) & 0xF)
    str += hex_tab.charAt((word >>> 20) & 0xF)
    str += hex_tab.charAt((word >>> 16) & 0xF)
    str += hex_tab.charAt((word >>> 12) & 0xF)
    str += hex_tab.charAt((word >>> 8) & 0xF)
    str += hex_tab.charAt((word >>> 4) & 0xF)
    str += hex_tab.charAt(word & 0xF)
  }
  return str
}

function sha1(str) {
  return binb2hex(core_sha1(str2binb(str), str.length * 8))
}

// ==================== HMAC-SHA1 ====================

function hmacSha1(key, data) {
  // 将 key 转为 word 数组
  let bkey = str2binb(key)

  // HMAC 要求 key 对齐到 64 字节（16 words）
  if (bkey.length > 16) {
    // key 过长时先做一次 SHA-1 压缩
    bkey = core_sha1(str2binb(key), key.length * 8)
  }
  // 补齐到 16 words
  const paddedKey = new Array(16)
  for (let i = 0; i < 16; i++) {
    paddedKey[i] = (bkey[i] || 0) >>> 0
  }

  // ipad / opad
  const ipad = new Array(16), opad = new Array(16)
  for (let i = 0; i < 16; i++) {
    ipad[i] = paddedKey[i] ^ 0x36363636
    opad[i] = paddedKey[i] ^ 0x5C5C5C5C
  }

  // 内层: SHA-1(ipad || data)
  const dataWords = str2binb(data)
  const inner = core_sha1(ipad.concat(dataWords), (16 + data.length) * 8)
  // 外层: SHA-1(opad || inner(20 bytes = 5 words))
  const outer = core_sha1(opad.concat(inner), (16 + 5) * 32)

  return binb2hex(outer)
}

module.exports = { sha1, hmacSha1 }
