# 高德地图Key申请指南

## 🚀 快速开始（比百度简单很多！）

### 步骤1：注册高德开放平台账号

访问：**https://console.amap.com/dev/key/app**

如果没有账号，使用支付宝或淘宝账号直接登录即可。

### 步骤2：创建应用

1. 点击 **"创建新应用"**
2. 填写应用名称：例如 "我的地图应用"
3. 应用类型：选择 **"Web端(JS API)"**
4. 点击 **"确定"**

### 步骤3：添加Key

1. 在刚创建的应用下，点击 **"添加Key"**
2. Key名称：填写任意名称，如 "主Key"
3. **服务平台**：选择 **"Web端(JS API)"**
4. **白名单**：可以留空或填写 `*`（允许所有域名）
5. 点击 **"提交"**

### 步骤4：复制Key

创建成功后，会显示一个Key，格式类似：
```
xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

### 步骤5：更新到代码

打开 `gaode_map.html` 文件，找到第35行：
```html
<script type="text/javascript" src="https://webapi.amap.com/maps?v=2.0&key=您的高德地图Key"></script>
```

将 `您的高德地图Key` 替换为您刚才复制的Key。

### 步骤6：运行程序

```powershell
python index.py
```

---

## ✅ 高德地图的优势

相比百度地图：
- ✅ 申请流程更简单
- ✅ 本地文件支持更好
- ✅ 不需要配置复杂的Referer白名单
- ✅ 卫星地图效果清晰
- ✅ API文档完善

---

## 🎯 如果仍然无法显示

### 检查清单：

1. **Key是否正确复制**
   - 确保没有多余空格
   - 确保完整复制

2. **应用类型是否正确**
   - 必须是 "Web端(JS API)"

3. **网络连接**
   - 确保可以访问 `webapi.amap.com`

4. **查看浏览器控制台**
   - 按 F12 打开开发者工具
   - 查看 Console 标签页的错误信息

---

## 📝 常用功能

高德地图支持的功能：
- ✅ 卫星地图/普通地图切换
- ✅ GPS坐标标记
- ✅ 路线规划
- ✅ 搜索功能
- ✅ 自定义标注
- ✅ 轨迹回放

---

## 🔗 相关链接

- 高德开放平台：https://lbs.amap.com/
- 控制台：https://console.amap.com/
- API文档：https://lbs.amap.com/api/javascript-api-v2/summary
