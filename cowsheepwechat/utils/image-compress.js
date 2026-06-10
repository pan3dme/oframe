// image-compress.js - 图片压缩工具：超过512px自动缩放到512以内
const MAX_SIZE = 512

/**
 * 压缩图片文件：如果宽或高超过 MAX_SIZE，等比缩放到 MAX_SIZE 以内
 * @param {string} filePath - 原始图片临时路径
 * @returns {Promise<string>} 压缩后的图片临时路径（无需压缩则返回原路径）
 */
function compressImage(filePath) {
  return new Promise((resolve) => {
    // 1) 获取原图尺寸
    wx.getImageInfo({
      src: filePath,
      success: (info) => {
        const srcW = info.width
        const srcH = info.height
        // 无需压缩
        if (srcW <= MAX_SIZE && srcH <= MAX_SIZE) {
          console.log('[图片压缩] 尺寸', srcW, 'x', srcH, '无需压缩，直接上传')
          resolve(filePath)
          return
        }
        // 计算等比缩放后的尺寸
        let dstW = srcW
        let dstH = srcH
        if (srcW >= srcH) {
          dstW = MAX_SIZE
          dstH = Math.round(srcH * MAX_SIZE / srcW)
        } else {
          dstH = MAX_SIZE
          dstW = Math.round(srcW * MAX_SIZE / srcH)
        }
        console.log('[图片压缩] 原始', srcW, 'x', srcH, '-> 压缩至', dstW, 'x', dstH)

        // 2) 用 canvas 缩放绘制
        const query = wx.createSelectorQuery()
        query.select('#compressCanvas')
          .fields({ node: true, size: true })
          .exec((res) => {
            if (!res || !res[0] || !res[0].node) {
              console.warn('[图片压缩] canvas 不可用，使用原图上传')
              resolve(filePath)
              return
            }
            const canvas = res[0].node
            canvas.width = dstW
            canvas.height = dstH
            const ctx = canvas.getContext('2d')
            const img = canvas.createImage()
            img.onload = () => {
              ctx.clearRect(0, 0, dstW, dstH)
              ctx.drawImage(img, 0, 0, dstW, dstH)
              wx.canvasToTempFilePath({
                canvas: canvas,
                fileType: 'jpg',
                quality: 0.85,
                success: (result) => {
                  console.log('[图片压缩] 完成:', result.tempFilePath)
                  resolve(result.tempFilePath)
                },
                fail: (err) => {
                  console.warn('[图片压缩] canvasToTempFilePath 失败:', err, '使用原图')
                  resolve(filePath)
                }
              })
            }
            img.onerror = (err) => {
              console.warn('[图片压缩] 图片加载失败:', err, '使用原图')
              resolve(filePath)
            }
            img.src = filePath
          })
      },
      fail: (err) => {
        console.warn('[图片压缩] getImageInfo 失败:', err, '使用原图')
        resolve(filePath)
      }
    })
  })
}

module.exports = { compressImage, MAX_SIZE }
