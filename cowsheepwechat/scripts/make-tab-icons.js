// 生成 TAB 图标：首页/设备/功能/地图，各 2 态（灰 #999999 + 绿 #07c160），81x81 抗锯齿 PNG
const fs = require('fs');
const zlib = require('zlib');

const SIZE = 81;          // 输出像素（微信推荐 81x81）
const LOGICAL = 24;       // 设计坐标系
const S = SIZE / LOGICAL; // 物理像素 / 逻辑单位

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
function makePNG(W, H, rgbaFn) {
  const stride = W * 4;
  const px = Buffer.alloc(stride * H, 0);
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const [r, g, b, a] = rgbaFn(x, y);
      const i = y * stride + x * 4;
      px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = a;
    }
  }
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

// ====== SDF 基础 ======
function clamp01(v) { return Math.max(0, Math.min(1, v)); }
function sdCircle(x, y, cx, cy, r) { return Math.hypot(x - cx, y - cy) - r; }
function sdBox(x, y, cx, cy, hx, hy) {
  const qx = Math.abs(x - cx) - hx;
  const qy = Math.abs(y - cy) - hy;
  return Math.min(Math.max(qx, qy), 0) + Math.hypot(Math.max(qx, 0), Math.max(qy, 0));
}
function sdRoundBox(x, y, cx, cy, hx, hy, r) {
  const qx = Math.abs(x - cx) - hx + r;
  const qy = Math.abs(y - cy) - hy + r;
  return Math.min(Math.max(qx, qy), 0) + Math.hypot(Math.max(qx, 0), Math.max(qy, 0)) - r;
}
function sdSegment(x, y, ax, ay, bx, by, r) {
  const pax = x - ax, pay = y - ay;
  const bax = bx - ax, bay = by - ay;
  const h = clamp01((pax * bax + pay * bay) / (bax * bax + bay * bay));
  return Math.hypot(pax - bax * h, pay - bay * h) - r;
}
// 简单多边形 SDF（内部为负）
function sdPolygon(x, y, v) {
  const n = v.length;
  let d = (x - v[0][0]) * (x - v[0][0]) + (y - v[0][1]) * (y - v[0][1]);
  let s = 1;
  for (let i = 0, j = n - 1; i < n; j = i, i++) {
    const ex = v[j][0] - v[i][0], ey = v[j][1] - v[i][1];
    const wx = x - v[i][0], wy = y - v[i][1];
    const h = clamp01((wx * ex + wy * ey) / (ex * ex + ey * ey));
    const bx = wx - ex * h, by = wy - ey * h;
    d = Math.min(d, bx * bx + by * by);
    const c0 = y >= v[i][1];
    const c1 = y < v[j][1];
    const c2 = ex * wy > ey * wx;
    if ((c0 && c1 && c2) || (!c0 && !c1 && !c2)) s = -s;
  }
  return s * Math.sqrt(d);
}
const min = Math.min, max = Math.max;
function sub(a, b) { return max(a, -b); } // a - b（镂空）

// ====== 各图标 SDF（24 逻辑坐标系，y 向下） ======
// 首页：房子（屋顶三角 + 圆角房身，下方开门洞）
function sdfHome(x, y) {
  const roof = sdPolygon(x, y, [[12, 1.8], [3, 10.4], [21, 10.4]]);
  const body = sdRoundBox(x, y, 12, 15.7, 7.4, 5.3, 1.2);
  const door = sdBox(x, y, 12, 19, 2.1, 2.1);
  return sub(min(roof, body), door);
}
// 功能：2x2 圆角方块（应用网格）
function sdfFeatures(x, y) {
  const c = [[7, 7], [17, 7], [7, 17], [17, 17]];
  let d = Infinity;
  c.forEach(([cx, cy]) => { d = min(d, sdRoundBox(x, y, cx, cy, 3.9, 3.9, 1.8)); });
  return d;
}
// 设备：路由器（圆角机身 + 左右天线）
function sdfDevice(x, y) {
  const body = sdRoundBox(x, y, 12, 14.6, 7.3, 4.4, 2);
  const antL = sdSegment(x, y, 7.6, 10.2, 5.8, 5.2, 0.9);
  const antR = sdSegment(x, y, 16.4, 10.2, 18.2, 5.2, 0.9);
  return min(body, min(antL, antR));
}
// 地图：定位图钉（圆 + 三角尖，中心镂空）
function sdfMap(x, y) {
  const head = sdCircle(x, y, 12, 8.8, 5.8);
  const tip = sdPolygon(x, y, [[12, 21.5], [5.9, 10.8], [18.1, 10.8]]);
  const hole = sdCircle(x, y, 12, 8.8, 2.6);
  return sub(min(head, tip), hole);
}

// ====== 渲染 ======
const COLORS = {
  normal: [153, 153, 153],   // #999999 与 color 一致
  active: [7, 193, 96]       // #07c160 与 selectedColor 一致
};
const ICONS = { home: sdfHome, device: sdfDevice, features: sdfFeatures, map: sdfMap };

Object.keys(ICONS).forEach((name) => {
  const fn = ICONS[name];
  Object.keys(COLORS).forEach((state) => {
    const [fr, fg, fb] = COLORS[state];
    const png = makePNG(SIZE, SIZE, (px, py) => {
      const lx = (px + 0.5) / S, ly = (py + 0.5) / S; // 像素中心 → 逻辑坐标
      const dPhys = fn(lx, ly) * S;
      const a = Math.round(255 * Math.max(0, Math.min(1, 0.5 - dPhys)));
      return a > 0 ? [fr, fg, fb, a] : [0, 0, 0, 0];
    });
    const file = `d:/oframesrc/cowsheepwechat/images/tab_${name}${state === 'active' ? '_active' : ''}.png`;
    fs.writeFileSync(file, png);
    console.log(file.split('/').pop(), SIZE + 'x' + SIZE, png.length + 'B');
  });
});
console.log('done');
