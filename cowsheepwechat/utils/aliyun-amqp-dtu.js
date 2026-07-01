// utils/aliyun-amqp-dtu.js — 阿里云 IoT AMQP 订阅工具
// 用于云函数（Node.js 环境）订阅设备上行消息，存入数据库供小程序轮询
//
// 依赖: npm install rhea (AMQP 1.0 客户端)
// 部署: 将此文件部署到阿里云函数，配置环境变量后作为 AMQP 消费者运行
//
// 环境变量:
//   IOT_UID           - 阿里云账号 UID
//   IOT_REGION        - IoT 区域 (如 cn-shanghai)
//   IOT_CONSUMER_GROUP - 消费组 ID
//   IOT_ACCESS_KEY    - AccessKey ID
//   IOT_ACCESS_SECRET - AccessKey Secret
//   IOT_PRODUCT_KEY   - 产品 ProductKey
//   DB_API_URL        - 数据库 API 地址 (用于存储设备数据)

const crypto = require('crypto')

// ==================== 配置 ====================

const CONFIG = {
  uid: process.env.IOT_UID || '',
  region: process.env.IOT_REGION || 'cn-shanghai',
  consumerGroup: process.env.IOT_CONSUMER_GROUP || 'DEFAULT_GROUP',
  accessKey: process.env.IOT_ACCESS_KEY || '',
  accessSecret: process.env.IOT_ACCESS_SECRET || '',
  productKey: process.env.IOT_PRODUCT_KEY || '',
  dbApiUrl: process.env.DB_API_URL || ''
}

// AMQP 连接地址
const AMQP_HOST = `${CONFIG.uid}.iot-amqp.${CONFIG.region}.aliyuncs.com`
const AMQP_PORT = 5671

// ==================== 签名工具 ====================

/**
 * 生成 AMQP 认证密码 (HMAC-SHA1)
 * 阿里云 IoT AMQP 使用 SASL PLAIN + AccessKey 签名认证
 */
function generatePassword() {
  const timestamp = String(Math.floor(Date.now() / 1000))
  const signContent = `authMode=aksign,signMethod=hmacsha1,consumerGroupId=${CONFIG.consumerGroup},authId=${CONFIG.accessKey},timestamp=${timestamp}`
  const hmac = crypto.createHmac('sha1', CONFIG.accessSecret)
  hmac.update(signContent)
  const signature = hmac.digest('base64')
  // 密码格式: base64(signContent)|base64(signature)
  return Buffer.from(signContent).toString('base64') + '|' + signature
}

/**
 * 生成 AMQP 用户名
 */
function generateUsername() {
  const timestamp = String(Math.floor(Date.now() / 1000))
  return `${CONFIG.accessKey}|authMode=aksign,signMethod=hmacsha1,consumerGroupId=${CONFIG.consumerGroup},authId=${CONFIG.accessKey},timestamp=${timestamp}|`
}

// ==================== 数据解析与存储 ====================

/**
 * 解析设备上报消息，提取物模型属性
 * 报文格式: {"id":198518,"version":"1.0","method":"thing.event.property.post","params":{"lorainfo":"1|v4-10|26.52955,109.39075|346","rssi":-49,"snr":7,"upDateDevice":"v3-12"}}
 * 
 * @param {Object} message - AMQP 消息体
 * @returns {Object|null} 解析后的设备数据
 */
function parseDeviceMessage(message) {
  try {
    const body = typeof message === 'string' ? JSON.parse(message) : message

    // 处理阿里云 IoT AMQP 消息体格式: { topic, mqttClientId, body, ... }
    let payload = body
    if (body.body) {
      try {
        payload = typeof body.body === 'string' ? JSON.parse(body.body) : body.body
      } catch (e) {
        payload = body.body
      }
    }

    // 解析物模型属性上报
    if (payload.method === 'thing.event.property.post' && payload.params) {
      const params = payload.params
      const now = new Date()
      const timeStr = now.getFullYear() + '/' +
        String(now.getMonth() + 1).padStart(2, '0') + '/' +
        String(now.getDate()).padStart(2, '0') + ' ' +
        String(now.getHours()).padStart(2, '0') + ':' +
        String(now.getMinutes()).padStart(2, '0') + ':' +
        String(now.getSeconds()).padStart(2, '0')

      return {
        deviceName: body.deviceName || body.mqttClientId || '',
        productKey: body.productKey || CONFIG.productKey,
        lorainfo: params.lorainfo || '',
        rssi: params.rssi !== undefined ? String(params.rssi) : '',
        snr: params.snr !== undefined ? String(params.snr) : '',
        upDateDevice: params.upDateDevice || '',
        time: timeStr,
        rawMessage: JSON.stringify(payload)
      }
    }

    // 非属性上报消息，返回原始数据
    return {
      deviceName: body.deviceName || body.mqttClientId || '',
      productKey: body.productKey || CONFIG.productKey,
      topic: body.topic || '',
      rawMessage: JSON.stringify(payload),
      time: new Date().toISOString()
    }
  } catch (e) {
    console.error('[AMQP] 解析消息失败:', e.message)
    return null
  }
}

/**
 * 将解析后的设备数据存入数据库 (通过 HTTP API)
 * @param {Object} deviceData - parseDeviceMessage 的返回结果
 * @returns {Promise<boolean>}
 */
async function storeDeviceData(deviceData) {
  if (!deviceData) return false
  if (!CONFIG.dbApiUrl) {
    console.log('[AMQP] DB_API_URL 未配置，跳过存储:', JSON.stringify(deviceData))
    return false
  }

  try {
    // 使用项目现有的 API 格式: { action, info }
    const response = await fetch(CONFIG.dbApiUrl, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        action: 'insertDeviceLotRefresh',
        info: {
          deviceId: deviceData.deviceName,
          lorastr: deviceData.lorainfo || deviceData.rawMessage,
          gps: '',                          // 从 lorainfo 解析 GPS 坐标
          time: deviceData.time,
          rssi: deviceData.rssi || '',
          snr: deviceData.snr || '',
          upDateDevice: deviceData.upDateDevice || ''
        }
      })
    })

    if (response.ok) {
      console.log('[AMQP] 数据已存储:', deviceData.deviceName, deviceData.lorainfo)
      return true
    } else {
      console.error('[AMQP] 存储失败, HTTP状态:', response.status)
      return false
    }
  } catch (err) {
    console.error('[AMQP] 存储异常:', err.message)
    return false
  }
}

// ==================== AMQP 订阅主流程 ====================

/**
 * 启动 AMQP 订阅 (基于 rhea 库)
 * 使用方法: 在云函数入口中调用此函数
 * 
 * @param {Object} opts - 可选配置覆盖
 * @returns {Promise<Object>} { connection, receiver }
 */
async function startAMQPSubscription(opts = {}) {
  // 合并配置
  Object.assign(CONFIG, opts)

  if (!CONFIG.uid || !CONFIG.accessKey || !CONFIG.accessSecret) {
    throw new Error('[AMQP] 缺少必要配置: IOT_UID, IOT_ACCESS_KEY, IOT_ACCESS_SECRET')
  }

  let rhea
  try {
    rhea = require('rhea')
  } catch (e) {
    throw new Error('[AMQP] 缺少 rhea 依赖，请执行: npm install rhea')
  }

  const username = generateUsername()
  const password = generatePassword()

  console.log('[AMQP] 连接参数:')
  console.log('  Host:', AMQP_HOST + ':' + AMQP_PORT)
  console.log('  Username:', username.substring(0, 50) + '...')
  console.log('  ConsumerGroup:', CONFIG.consumerGroup)

  // 创建 AMQP 连接
  const connection = rhea.connect({
    host: AMQP_HOST,
    port: AMQP_PORT,
    username: username,
    password: password,
    transport: 'tls',
    reconnect: true,
    reconnect_limit: 50,
    initial_reconnect_delay: 1000,
    max_reconnect_delay: 30000,
    // SASL 配置
    hostname: AMQP_HOST,
    container_id: 'dtu-amqp-subscriber-' + Date.now()
  })

  return new Promise((resolve, reject) => {
    connection.on('connection_open', () => {
      console.log('[AMQP] 连接已建立')

      // 创建接收器 — 订阅所有设备上行消息
      // topic 格式: /${productKey}/+/thing/event/property/post
      const sourceAddress = `/${CONFIG.productKey}/+/thing/event/property/post`

      const receiver = connection.open_receiver({
        source: {
          address: sourceAddress,
          filter: {}
        },
        autoaccept: true,
        credit_window: 100
      })

      receiver.on('message', (context) => {
        const msg = context.message
        let body = msg.body

        // 转换消息体
        if (Buffer.isBuffer(body)) {
          body = body.toString('utf-8')
        }

        console.log('[AMQP] 收到消息:', typeof body === 'string' ? body.substring(0, 200) : JSON.stringify(body).substring(0, 200))

        // 解析并存储
        const deviceData = parseDeviceMessage(body)
        if (deviceData) {
          storeDeviceData(deviceData).catch(err => {
            console.error('[AMQP] 存储失败:', err.message)
          })
        }
      })

      receiver.on('receiver_open', () => {
        console.log('[AMQP] 接收器已打开，订阅地址:', sourceAddress)
      })

      receiver.on('receiver_error', (_ctx) => {
        console.error('[AMQP] 接收器错误:', _ctx.error)
      })

      receiver.on('receiver_close', () => {
        console.log('[AMQP] 接收器已关闭')
      })

      resolve({ connection, receiver })
    })

    connection.on('connection_error', (ctx) => {
      console.error('[AMQP] 连接错误:', ctx.error)
    })

    connection.on('connection_close', () => {
      console.log('[AMQP] 连接已关闭')
    })

    connection.on('disconnected', (ctx) => {
      console.log('[AMQP] 连接断开，将自动重连...')
    })

    // 超时处理
    setTimeout(() => {
      if (!connection.is_open()) {
        reject(new Error('[AMQP] 连接超时 (10s)'))
      }
    }, 10000)
  })
}

/**
 * 停止 AMQP 订阅
 * @param {Object} subscription - startAMQPSubscription 的返回值
 */
function stopAMQPSubscription(subscription) {
  if (subscription && subscription.connection) {
    try {
      subscription.connection.close()
      console.log('[AMQP] 订阅已停止')
    } catch (e) {
      console.error('[AMQP] 停止订阅失败:', e.message)
    }
  }
}

// ==================== 云函数入口示例 ====================

/**
 * 阿里云函数入口 (事件函数)
 * 用法: 将此文件作为云函数代码，exports.handler 为入口
 * 
 * 部署步骤:
 *   1. npm init && npm install rhea
 *   2. 压缩 node_modules + 此文件 + index.js 为 zip
 *   3. 上传到阿里云函数，配置环境变量
 *   4. 设置触发器为定时触发（或 HTTP 触发）
 */
async function handler(event, context) {
  console.log('[AMQP云函数] 启动, event:', JSON.stringify(event))

  let subscription
  try {
    subscription = await startAMQPSubscription()

    // 云函数环境下需要保持进程运行以持续接收消息
    // 阿里云函数默认会等待事件循环清空才退出
    // 使用 keepAlive 保持连接
    console.log('[AMQP云函数] 订阅已启动，等待设备消息...')

    // 返回连接对象，由调用方管理生命周期
    return {
      statusCode: 200,
      body: JSON.stringify({
        success: true,
        message: 'AMQP订阅已启动',
        host: AMQP_HOST,
        consumerGroup: CONFIG.consumerGroup
      })
    }
  } catch (err) {
    console.error('[AMQP云函数] 启动失败:', err.message)
    return {
      statusCode: 500,
      body: JSON.stringify({
        success: false,
        error: err.message
      })
    }
  }
}

module.exports = {
  // 核心 API
  startAMQPSubscription,
  stopAMQPSubscription,
  parseDeviceMessage,
  storeDeviceData,

  // 工具
  generatePassword,
  generateUsername,

  // 配置
  AMQP_HOST,
  AMQP_PORT,
  CONFIG,

  // 云函数入口
  handler
}
