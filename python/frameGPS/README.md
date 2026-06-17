# frameGPS - 百度地图GPS应用

## 📁 文件说明

- **index.py** - 主程序，1440x800窗口，左侧显示百度卫星地图，右侧4个按钮
- **baidu_map.html** - 百度地图HTML页面（包含诊断功能）
- **baidu_map_diagnostic.html** - 百度地图诊断页面
- **test_baidu_map.py** - 测试版本（带详细调试信息）
- **start_http_server.py** - HTTP服务器（用于测试地图）

## 🚀 快速开始

### 1. 配置百度地图AK

在运行程序前，需要先配置有效的百度地图AK：

1. 访问 http://lbsyun.baidu.com/apiconsole/key
2. 登录百度账号
3. 创建应用：
   - 应用名称：任意
   - 应用类型：**浏览器端** ⚠️
   - Referer白名单：`*`
4. 复制生成的AK

### 2. 更新AK到代码

打开 `baidu_map.html` 文件，找到第29行：
```html
<script type="text/javascript" src="https://api.map.baidu.com/api?v=3.0&ak=YOUR_AK_HERE"></script>
```

将 `YOUR_AK_HERE` 替换为您申请的AK。

### 3. 运行程序

```powershell
python index.py
```

## 🔧 故障排查

### 问题1：显示"百度地图API加载失败"

**原因**：AK密钥无效或配置错误

**解决方案**：
1. 检查AK是否正确复制
2. 确认应用类型为"浏览器端"
3. 确认Referer白名单设置为 `*`
4. 确认AK状态为"启用"

### 问题2：地图显示空白

**原因**：网络连接问题或AK未生效

**解决方案**：
1. 检查网络连接
2. 等待几分钟让AK配置生效
3. 使用诊断页面测试：在浏览器中打开 `baidu_map_diagnostic.html`

### 问题3：本地文件无法加载地图

**原因**：浏览器安全策略限制

**解决方案**：
使用HTTP服务器方式运行：
```powershell
python start_http_server.py
```
然后在浏览器中访问：http://localhost:8080/baidu_map.html

## 📝 功能特性

- ✅ 1440x800窗口尺寸
- ✅ 左右分栏布局
- ✅ 百度卫星地图显示
- ✅ 支持鼠标滚轮缩放
- ✅ 支持拖拽移动
- ✅ 导航控件和比例尺
- ✅ 右侧4个自定义按钮

## 🎯 下一步开发

可以添加的功能：
- GPS坐标标记
- 路线绘制
- 设备位置追踪
- 地图搜索
- 自定义标注

## 📞 技术支持

如有问题，请检查：
1. 百度地图控制台AK配置
2. 网络连接状态
3. Python和PyQt6版本兼容性
