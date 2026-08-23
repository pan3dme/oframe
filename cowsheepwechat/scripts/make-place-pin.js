// 生成两张 PNG：place_pin.png（红水滴 30x32）+ device_pin.png（绿圆 28x28）
const fs = require('fs');
const zlib = require('zlib');

const crcTable = new Uint32Array(256);
for (let n = 0; n < 256; n++) {
  let c = n;
  for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
  crcTable[n] = c >>> 0;
}
function crc32(buf) {
  let c = 0xFFFFFFFF >>> 0;
  for (let i = 0; i < buf.length; i++) c = (crcTable[(c ^ buf[i]) & 0xFF] ^ (c >>> 8)) >>> 0;
  return (c ^ 0xFFFFFFFF) >>> 0;
}
function u32BE(n) { const b = Buffer.alloc(4); b.writeUInt32BE(n >>> 0, 0); return b; }
function chunk(type, data) {
  const t = Buffer.from(type, 'ascii');
  return Buffer.concat([u32BE(data.length), t, data, u32BE(crc32(Buffer.concat([t, data])))]);
}

function makePNG(W, H, drawFn) {
  const stride = W * 4;
  const px = Buffer.alloc(stride * H, 0);
  function setPx(x, y, r, g, b, a) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    const i = (y * stride) + x * 4;
    px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = a;
  }
  drawFn(setPx, W, H);
  const raw = Buffer.alloc(H * (1 + stride));
  for (let y = 0; y < H; y++) {
    raw[y * (1 + stride)] = 0;
    px.copy(raw, y * (1 + stride) + 1, y * stride, (y + 1) * stride);
  }
  const sig = Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4);
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  const idat = zlib.deflateSync(raw, { level: 9 });
  return Buffer.concat([sig, chunk('IHDR', ihdr), chunk('IDAT', idat), chunk('IEND', Buffer.alloc(0))]);
}

function fillTri(setPx, ax, ay, bx, by, cx3, cy3, color) {
  const minY = Math.floor(Math.min(ay, by, cy3));
  const maxY = Math.ceil(Math.max(ay, by, cy3));
  const edges = [[ax, ay, bx, by], [bx, by, cx3, cy3], [cx3, cy3, ax, ay]];
  for (let y = minY; y <= maxY; y++) {
    const ys = y + 0.5;
    const xs = [];
    edges.forEach(([xa, ya, xb, yb]) => {
      if ((ya <= ys && yb >= ys) || (yb <= ys && ya >= ys)) {
        if (yb === ya) return;
        const t = (ys - ya) / (yb - ya);
        xs.push(xa + t * (xb - xa));
      }
    });
    if (xs.length < 2) continue;
    const xL = Math.min(...xs), xR = Math.max(...xs);
    for (let x = Math.ceil(xL); x <= Math.floor(xR); x++) setPx(x, y, color[0], color[1], color[2], 255);
  }
}

// ====== place_pin.png：红色水滴 30x32（缩小高度，避开微信 marker 32 像素上限） ======
{
  const W = 30, H = 32;
  // 圆心 (15, 11), 半径 10；三角形尖端 (15, 30) 到 (8, 12) 和 (22, 12)
  const cx = 15, cy = 11, r = 10;
  const png = makePNG(W, H, (setPx) => {
    // 红色圆
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const dx = x + 0.5 - cx, dy = y + 0.5 - cy;
      if (dx * dx + dy * dy <= r * r) setPx(x, y, 229, 57, 53, 255);
    }
    // 三角形尖端
    fillTri(setPx, cx, 30, 8, 12, 22, 12, [229, 57, 53]);
    // 深红描边
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const dx = x + 0.5 - cx, dy = y + 0.5 - cy;
      const d = Math.sqrt(dx * dx + dy * dy);
      if (d >= r - 0.8 && d <= r + 0.3) setPx(x, y, 183, 28, 28, 255);
    }
    // 中心白点 (r=4)
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const dx = x + 0.5 - cx, dy = y + 0.5 - cy;
      if (dx * dx + dy * dy <= 4 * 4) setPx(x, y, 255, 255, 255, 255);
    }
  });
  fs.writeFileSync('d:/oframesrc/cowsheepwechat/images/place_pin.png', png);
  console.log('place_pin.png', png.length, 'bytes', W + 'x' + H);
}

// ====== place_dot.png：红色圆点 20x20（最简单，规避图标加载问题） ======
{
  const W = 20, H = 20;
  const cx = W / 2, cy = H / 2, r = 8;
  const png = makePNG(W, H, (setPx) => {
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const dx = x + 0.5 - cx, dy = y + 0.5 - cy;
      const d = Math.sqrt(dx * dx + dy * dy);
      if (d <= r) setPx(x, y, 229, 57, 53, 255);            // 红
      if (d > r && d <= r + 1) setPx(x, y, 183, 28, 28, 255);  // 深红描边
    }
  });
  fs.writeFileSync('d:/oframesrc/cowsheepwechat/images/place_dot.png', png);
  console.log('place_dot.png', png.length, 'bytes', W + 'x' + H);
}

// ====== device_pin.png：白底 + 绿色描边 + 绿色倒三角 28x28 ======
{
  const W = 28, H = 28;
  const cx = W / 2, cy = H / 2, r = 10;
  const png = makePNG(W, H, (setPx) => {
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const dx = x + 0.5 - cx, dy = y + 0.5 - cy;
      if (dx * dx + dy * dy <= r * r) setPx(x, y, 255, 255, 255, 255);
    }
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const dx = x + 0.5 - cx, dy = y + 0.5 - cy;
      const d = Math.sqrt(dx * dx + dy * dy);
      if (d > r && d <= r + 2) setPx(x, y, 0, 200, 83, 255);
    }
    fillTri(setPx, cx - 5, cy - 5, cx, cy + 5, cx + 5, cy - 5, [0, 200, 83]);
  });
  fs.writeFileSync('d:/oframesrc/cowsheepwechat/images/device_pin.png', png);
  console.log('device_pin.png', png.length, 'bytes', W + 'x' + H);
}
