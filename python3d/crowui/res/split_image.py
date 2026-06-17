"""将 baidumapall.png 切割成 12列 x 4行 的小图片"""
import os
from PIL import Image

# 图片路径
src_path = os.path.join(os.path.dirname(__file__), "..", "pingjie", "result.jpg")
src_path = os.path.abspath(src_path)

# 输出目录
out_dir = os.path.join(os.path.dirname(__file__))
os.makedirs(out_dir, exist_ok=True)

# 打开图片
img = Image.open(src_path)
w, h = img.size
print(f"原图尺寸: {w} x {h}")

cols = 10
rows = 10
tile_w = w // cols
tile_h = h // rows
print(f"每格尺寸: {tile_w} x {tile_h}")

for r in range(rows):
    for c in range(cols):
        left = c * tile_w
        upper = r * tile_h
        right = left + tile_w
        lower = upper + tile_h
        tile = img.crop((left, upper, right, lower))
        name = f"tile_{r}_{c}.png"
        tile.save(os.path.join(out_dir, name))
        print(f"保存: {name}")

print("切割完成！")
