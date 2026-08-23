// 用 Node 生成 30x36 红色水滴 PNG（零依赖）
const fs = require('fs');
const zlib = require('zlib');

const W = 30, H = 36;

// CRC32 表
const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    t[n] = c >>> 0;
  }
  return t;
})();
function crc32(buf) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < buf.length; i++) c = crcTable[(c ^ buf[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length, 0);
  const t = Buffer.from(type, 'ascii');
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(Buffer.concat([t, data])), 0);
  return Buffer.concat([len, t, data, crc]);
}

// 像素 RGBA
const px = Buffer.alloc(W * H * 4, 0);
function setPx(x, y, r, g, b, a = 255) {
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  const i = (y * W + x) * 4;
  px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = a;
}

// 1) 填充圆 (圆心 15,12, 半径 11)
for (let y = 0; y < H; y++) {
  for (let x = 0; x < W; x++) {
    const dx = x + 0.5 - 15, dy = y + 0.5 - 12;
    if (dx * dx + dy * dy <= 11 * 11) setPx(x, y, 229, 57, 53);
  }
}

// 2) 三角形尖端 (15,34) (7,13) (23,13) - 扫描线填充
function fillTri(ax, ay, bx, by, cx, cy) {
  const minY = Math.floor(Math.min(ay, by, cy));
  const maxY = Math.ceil(Math.max(ay, by, cy));
  const edges = [[ax, ay, bx, by], [bx, by, cx, cy], [cx, cy, ax, ay]];
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
    for (let x = Math.ceil(xL); x <= Math.floor(xR); x++) setPx(x, y, 229, 57, 53);
  }
}
fillTri(15, 34, 7, 13, 23, 13);

// 3) 圆描边 (深红 #B71C1C)
for (let y = 0; y < H; y++) {
  for (let x = 0; x < W; x++) {
    const dx = x + 0.5 - 15, dy = y + 0.5 - 12;
    const d = Math.sqrt(dx * dx + dy * dy);
    if (d >= 10.3 && d <= 11.2) setPx(x, y, 183, 28, 28);
  }
}

// 4) 中心白点 (半径 4)
for (let y = 0; y < H; y++) {
  for (let x = 0; x < W; x++) {
    const dx = x + 0.5 - 15, dy = y + 0.5 - 12;
    if (dx * dx + dy * dy <= 4 * 4) setPx(x, y, 255, 255, 255);
  }
}

// 5) PNG 编码
// 每行前加 0 滤波字节
const raw = Buffer.alloc(H * (1 + W * 4));
for (let y = 0; y < H; y++) {
  raw[y * (1 + W * 4)] = 0;
  px.copy(raw, y * (1 + W * 4) + 1, y * W * 4, (y + 1) * W * 4);
}
const idat = zlib.deflateSync(raw);

const sig = Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(W, 0);
ihdr.writeUInt32BE(H, 4);
ihdr[8] = 8;   // bit depth
ihdr[9] = 6;   // color type RGBA
ihdr[10] = 0;  // compression
ihdr[11] = 0;  // filter
ihdr[12] = 0;  // interlace

const png = Buffer.concat([sig, chunk('IHDR', ihdr), chunk('IDAT', idat), chunk('IEND', Buffer.alloc(0))]);

const outDir = 'd:/oframesrc/cowsheepwechat/images';
if (!fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });
const outPath = outDir + '/place_pin.png';
fs.writeFileSync(outPath, png);
console.log('Wrote', outPath, png.length, 'bytes');
