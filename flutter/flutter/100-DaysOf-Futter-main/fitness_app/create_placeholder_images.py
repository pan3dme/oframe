from PIL import Image, ImageDraw, ImageFont
import os

# 创建assets目录
assets_dir = "assets"
os.makedirs(assets_dir, exist_ok=True)

# 1. 创建 image.png - 启动页面主图 (800x600)
img1 = Image.new('RGB', (800, 600), color=(66, 133, 244))
draw1 = ImageDraw.Draw(img1)
# 添加一些简单的图形
draw1.ellipse([200, 150, 600, 450], fill=(255, 255, 255, 128))
draw1.text((250, 280), "Fitness", fill=(255, 255, 255))
img1.save(os.path.join(assets_dir, "image.png"))
print("Created image.png")

# 2. 创建 avatar.png - 用户头像 (200x200)
img2 = Image.new('RGBA', (200, 200), color=(0, 0, 0, 0))
draw2 = ImageDraw.Draw(img2)
# 绘制圆形头像背景
draw2.ellipse([10, 10, 190, 190], fill=(100, 150, 200))
# 绘制简单的人形
draw2.ellipse([70, 40, 130, 100], fill=(255, 220, 180))  # 头
draw2.ellipse([50, 100, 150, 180], fill=(100, 150, 200))  # 身体
img2.save(os.path.join(assets_dir, "avatar.png"))
print("Created avatar.png")

# 3. 创建 running.png - 跑步图标 (200x200)
img3 = Image.new('RGBA', (200, 200), color=(0, 0, 0, 0))
draw3 = ImageDraw.Draw(img3)
# 绘制简单的跑步人形
draw3.ellipse([80, 30, 120, 70], fill=(255, 100, 100))  # 头
draw3.line([(100, 70), (100, 130)], fill=(255, 100, 100), width=8)  # 身体
draw3.line([(100, 90), (60, 110)], fill=(255, 100, 100), width=6)  # 左臂
draw3.line([(100, 90), (140, 100)], fill=(255, 100, 100), width=6)  # 右臂
draw3.line([(100, 130), (70, 170)], fill=(255, 100, 100), width=6)  # 左腿
draw3.line([(100, 130), (150, 160)], fill=(255, 100, 100), width=6)  # 右腿
img3.save(os.path.join(assets_dir, "running.png"))
print("Created running.png")

print("\n所有图片已创建成功！")
