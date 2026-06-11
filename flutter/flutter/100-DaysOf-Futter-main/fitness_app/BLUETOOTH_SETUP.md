# 蓝牙功能配置说明

## 📱 功能概述

已创建蓝牙管理页面，支持 iOS 和 Android 平台的蓝牙设备搜索、连接和断开功能。

## 🎯 主要功能

1. **顶部双按钮**
   - 🔵 连接蓝牙：开始扫描附近的蓝牙设备
   - 🔴 断开蓝牙：断开当前已连接的设备

2. **设备列表**
   - 实时显示扫描到的蓝牙设备
   - 显示设备名称、MAC 地址、信号强度
   - 点击"连接"按钮连接设备
   - 已连接设备会有绿色标记

3. **状态提示**
   - 蓝牙适配器状态（开启/关闭）
   - 扫描状态指示器
   - 已连接设备信息卡片

## 📦 依赖包

已在 `pubspec.yaml` 中添加：
```yaml
flutter_blue_plus: ^1.34.5
```

## ⚙️ 平台配置

### Android 配置

#### 1. AndroidManifest.xml 权限
已添加以下权限：
```xml
<!-- 蓝牙权限 -->
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.BLUETOOTH_ADVERTISE" />
<!-- 位置权限（扫描蓝牙需要） -->
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" />
```

#### 2. build.gradle 最低版本
已将 `minSdkVersion` 设置为 21（Android 5.0+）

### iOS 配置

#### Info.plist 权限描述
已添加以下权限描述：
```xml
<!-- 蓝牙权限描述 -->
<key>NSBluetoothAlwaysUsageDescription</key>
<string>需要使用蓝牙功能来连接和管理附近的设备</string>
<key>NSBluetoothPeripheralUsageDescription</key>
<string>需要访问蓝牙外设来连接和管理设备</string>
<key>NSLocationWhenInUseUsageDescription</key>
<string>需要位置权限来扫描附近的蓝牙设备</string>
```

## 🚀 使用方法

### 1. 安装依赖
运行以下命令安装依赖包：
```bash
flutter pub get
```

### 2. 导航到蓝牙页面
从功能列表页面点击"链接蓝牙"按钮即可进入。

### 3. 连接设备流程
1. 点击"连接蓝牙"按钮开始扫描
2. 等待 10 秒扫描附近的蓝牙设备
3. 在设备列表中看到可用的设备
4. 点击设备右侧的"连接"按钮
5. 连接成功后会显示"已连接"状态

### 4. 断开连接
点击顶部的"断开蓝牙"按钮即可断开当前连接。

## 📝 注意事项

### Android
- Android 6.0+ 需要位置权限才能扫描蓝牙设备
- Android 12+ 需要新的蓝牙权限（BLUETOOTH_SCAN, BLUETOOTH_CONNECT）
- 首次使用时会弹出权限请求对话框

### iOS
- iOS 13+ 需要在 Info.plist 中添加蓝牙权限描述
- 首次使用时会弹出权限请求对话框
- 需要在真机上测试（模拟器不支持蓝牙）

### 通用
- 确保设备蓝牙已开启
- 确保目标设备处于可配对状态
- 某些功能可能需要额外的 BLE 服务/特征码处理

## 🔧 后续扩展

可以根据实际需求添加以下功能：
1. 蓝牙数据读写（发送/接收数据）
2. 蓝牙服务和特征码发现
3. 自动重连功能
4. 蓝牙设备收藏/历史记录
5. 蓝牙通知监听
6. MTU 协商
7. 连接超时处理

## 🐛 常见问题

### 1. 扫描不到设备
- 检查蓝牙是否开启
- 检查位置权限是否授予（Android）
- 确保目标设备处于广播状态

### 2. 连接失败
- 检查设备是否已被其他应用连接
- 检查设备是否在有效范围内
- 查看日志获取详细错误信息

### 3. 权限被拒绝
- 引导用户到系统设置中手动授予权限
- 使用 permission_handler 包进行权限管理

## 📚 参考文档

- flutter_blue_plus: https://pub.dev/packages/flutter_blue_plus
- Android 蓝牙权限: https://developer.android.com/guide/topics/connectivity/bluetooth/permissions
- iOS 蓝牙权限: https://developer.apple.com/documentation/corebluetooth
