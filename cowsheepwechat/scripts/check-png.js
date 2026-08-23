const fs = require('fs'), zlib = require('zlib');

function verify(path) {
  const b = fs.readFileSync(path);
  const w = b.readUInt32BE(16), h = b.readUInt32BE(20);
  let i = 8;
  const idatChunks = [];
  while (i < b.length) {
    const len = b.readUInt32BE(i);
    const type = b.slice(i + 4, i + 8).toString('ascii');
    if (type === 'IDAT') idatChunks.push(b.slice(i + 8, i + 8 + len));
    if (type === 'IEND') break;
    i += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idatChunks));
  console.log(path.split('/').pop(), w + 'x' + h, 'raw rows:', raw.length / (1 + w * 4));
}

verify('d:/oframesrc/cowsheepwechat/images/place_pin.png');
verify('d:/oframesrc/cowsheepwechat/images/place_dot.png');
verify('d:/oframesrc/cowsheepwechat/images/device_pin.png');
