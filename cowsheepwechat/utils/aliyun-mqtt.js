// utils/aliyun-mqtt.js — 微信小程序直连阿里云 IoT MQTT（原生 MQTT 3.1.1 + wx.connectSocket）
const { hmacSha1 } = require('./hmac-sha1.js')

/**
 * 连接阿里云 IoT MQTT 代理
 * @param {Object} opts
 *   productKey   - 产品 Key
 *   deviceName   - 设备名称
 *   deviceSecret - 设备密钥
 *   regionId     - 区域 (如 cn-shanghai)
 *   onMessage    - function(topic, payload) 收到消息回调
 *   onConnected  - function() 连接成功回调
 *   onError      - function(err) 错误回调
 * @returns {{ close: function, subscribe: function }}
 */
function connectAliyunIoT(opts) {
  const { productKey, deviceName, deviceSecret, regionId, onMessage, onConnected, onError } = opts

  // ---- 1. 计算签名 ----
  const clientId = deviceName + '|securemode=3,signmethod=hmacsha1|'
  const username = deviceName + '&' + productKey
  const signContent = 'clientId' + clientId + 'deviceName' + deviceName + 'productKey' + productKey
  const password = hmacSha1(deviceSecret, signContent)

  console.log('[IoT-MQTT] === 连接参数 ===')
  console.log('[IoT-MQTT] clientId:', clientId)
  console.log('[IoT-MQTT] username:', username)
  console.log('[IoT-MQTT] signContent:', signContent)
  console.log('[IoT-MQTT] password(hex):', password)
  console.log('[IoT-MQTT] password length:', password.length)

  // ---- 2. 构建 MQTT CONNECT 二进制包 ----
  const protocolName = [0x00, 0x04, 0x4D, 0x51, 0x54, 0x54] // "MQTT"
  const protocolLevel = 0x04 // MQTT 3.1.1
  const connectFlags = 0xC2   // username(1) + password(1) + clean session(1)
  const keepAlive = [0x00, 0x3C] // 60 秒

  const payload = encodeUTF8(clientId).concat(encodeUTF8(username)).concat(encodeUTF8(password))
  const varHeader = protocolName.concat([protocolLevel, connectFlags], keepAlive)
  const packet = [0x10] // CONNECT packet type
    .concat(encodeRemainingLength(varHeader.length + payload.length))
    .concat(varHeader)
    .concat(payload)

  console.log('[IoT-MQTT] CONNECT 包大小:', packet.length, 'bytes')

  // ---- 3. WebSocket 连接 ----
  const wsUrl = 'wss://' + productKey + '.iot-as-mqtt.' + regionId + '.aliyuncs.com/mqtt'
  console.log('[IoT-MQTT] 连接地址:', wsUrl)

  let pingTimer = null
  let closed = false

  wx.connectSocket({
    url: wsUrl,
    header: { 'content-type': 'application/json' },
    success: () => console.log('[IoT-MQTT] wx.connectSocket 调用成功'),
    fail: (err) => {
      console.error('[IoT-MQTT] wx.connectSocket 失败:', err)
      onError && onError(err)
    }
  })

  wx.onSocketOpen(() => {
    console.log('[IoT-MQTT] WebSocket 已连接，发送 CONNECT 包...')
    wx.sendSocketMessage({
      data: new Uint8Array(packet).buffer,
      success: () => console.log('[IoT-MQTT] CONNECT 包已发送'),
      fail: (err) => console.error('[IoT-MQTT] CONNECT 发送失败:', err)
    })
  })

  wx.onSocketMessage((res) => {
    const data = arrayBufferToBytes(res.data)
    if (data.length === 0) return
    handlePacket(data)
  })

  wx.onSocketClose((res) => {
    console.log('[IoT-MQTT] WebSocket 已关闭, code:', res.code)
    closed = true
    if (pingTimer) clearInterval(pingTimer)
  })

  wx.onSocketError((err) => {
    console.error('[IoT-MQTT] WebSocket 错误:', JSON.stringify(err))
    onError && onError(err)
  })

  // ---- 4. MQTT 包解析 ----
  function handlePacket(data) {
    const type = (data[0] & 0xF0) >> 4
    // console.log('[IoT-MQTT] 收到包类型:', type)

    let pos = 1
    let multiplier = 1
    let remainingLength = 0
    let encodedByte
    do {
      encodedByte = data[pos++]
      remainingLength += (encodedByte & 127) * multiplier
      multiplier *= 128
    } while ((encodedByte & 128) !== 0)

    const payloadStart = pos
    const payloadEnd = payloadStart + remainingLength

    switch (type) {
      case 2: { // CONNACK: fixedHeader(2) + ackFlags(1) + returnCode(1)
        const returnCode = data[pos + 1]
        console.log('[IoT-MQTT] CONNACK raw bytes:', JSON.stringify(data), 'pos:', pos, 'returnCode:', returnCode)
        if (returnCode === 0) {
          console.log('[IoT-MQTT] CONNACK 连接成功!')
          // 启动心跳
          pingTimer = setInterval(() => {
            sendPingReq()
          }, 50000) // 略小于 keepAlive
          onConnected && onConnected()
        } else {
          console.error('[IoT-MQTT] CONNACK 连接被拒绝, 返回码:', returnCode)
          onError && onError({ code: returnCode, message: 'MQTT连接被拒绝, 返回码:' + returnCode })
        }
        break
      }
      case 3: { // PUBLISH
        // topic
        let tpos = payloadStart
        const topicLen = (data[tpos] << 8) | data[tpos + 1]
        tpos += 2
        const topic = bytesToUTF8(data.slice(tpos, tpos + topicLen))
        tpos += topicLen

        // QoS > 0 有 packetId
        const qos = (data[0] & 0x06) >> 1
        if (qos > 0) {
          tpos += 2 // skip packetId
        }

        const payloadBytes = data.slice(tpos, payloadEnd)
        let payloadStr = ''
        try {
          payloadStr = bytesToUTF8(payloadBytes)
        } catch (e) {
          payloadStr = JSON.stringify(Array.from(payloadBytes))
        }

        console.log('[IoT-MQTT] 收到消息 topic:', topic, 'payload:', payloadStr)
        onMessage && onMessage(topic, payloadStr)
        break
      }
      case 9: // SUBACK
        console.log('[IoT-MQTT] SUBACK 订阅确认')
        break
      case 13: // PINGRESP
        // console.log('[IoT-MQTT] PINGRESP 心跳回复')
        break
      default:
        console.log('[IoT-MQTT] 未处理的包类型:', type)
    }
  }

  function sendPingReq() {
    if (closed) return
    wx.sendSocketMessage({
      data: new Uint8Array([0xC0, 0x00]).buffer,
      fail: (err) => console.warn('[IoT-MQTT] PINGREQ 发送失败:', err)
    })
  }

  // ---- 公开方法 ----
  function subscribe(topic, qos) {
    if (closed) return
    qos = qos || 1
    const topicBytes = encodeUTF8(topic)
    const payload = [0x00, 0x01] // packetId = 1
      .concat(topicBytes)
      .concat([qos])

    const packet = [0x82] // SUBSCRIBE type + QoS1
      .concat(encodeRemainingLength(payload.length))
      .concat(payload)

    wx.sendSocketMessage({
      data: new Uint8Array(packet).buffer,
      success: () => console.log('[IoT-MQTT] SUBSCRIBE 已发送 topic:', topic),
      fail: (err) => console.error('[IoT-MQTT] SUBSCRIBE 发送失败:', err)
    })
  }

  function close() {
    closed = true
    if (pingTimer) clearInterval(pingTimer)
    // 发送 DISCONNECT
    try {
      wx.sendSocketMessage({
        data: new Uint8Array([0xE0, 0x00]).buffer
      })
    } catch (e) { /* ignore */ }
    wx.closeSocket()
  }

  return { subscribe, close }
}

// ==================== 工具函数 ====================

function encodeRemainingLength(length) {
  const bytes = []
  let remaining = length
  do {
    let byte = remaining % 128
    remaining = Math.floor(remaining / 128)
    if (remaining > 0) byte = byte | 128
    bytes.push(byte)
  } while (remaining > 0)
  return bytes
}

function encodeUTF8(str) {
  const utf8 = []
  for (let i = 0; i < str.length; i++) {
    let code = str.charCodeAt(i)
    if (code < 0x80) {
      utf8.push(code)
    } else if (code < 0x800) {
      utf8.push(0xC0 | (code >> 6), 0x80 | (code & 0x3F))
    } else if (code < 0xD800 || code >= 0xE000) {
      utf8.push(0xE0 | (code >> 12), 0x80 | ((code >> 6) & 0x3F), 0x80 | (code & 0x3F))
    } else {
      i++
      code = 0x10000 + (((code & 0x3FF) << 10) | (str.charCodeAt(i) & 0x3FF))
      utf8.push(0xF0 | (code >> 18), 0x80 | ((code >> 12) & 0x3F),
        0x80 | ((code >> 6) & 0x3F), 0x80 | (code & 0x3F))
    }
  }
  // 前加两字节长度（MQTT UTF-8 编码要求）
  return [0x00, utf8.length].concat(utf8)
}

function bytesToUTF8(bytes) {
  let str = ''
  let i = 0
  while (i < bytes.length) {
    const b = bytes[i]
    let code, len
    if (b < 0x80) { code = b; len = 1 }
    else if (b < 0xE0) { code = ((b & 0x1F) << 6) | (bytes[i + 1] & 0x3F); len = 2 }
    else if (b < 0xF0) { code = ((b & 0x0F) << 12) | ((bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F); len = 3 }
    else { code = ((b & 0x07) << 18) | ((bytes[i + 1] & 0x3F) << 12) | ((bytes[i + 2] & 0x3F) << 6) | (bytes[i + 3] & 0x3F); len = 4 }
    str += String.fromCharCode(code)
    i += len
  }
  return str
}

function arrayBufferToBytes(buffer) {
  return Array.from(new Uint8Array(buffer))
}

module.exports = { connectAliyunIoT }
