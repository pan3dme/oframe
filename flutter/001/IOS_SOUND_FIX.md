# iOS蓝牙接收声音配置指南

## 问题描述
在iOS设备上,蓝牙接收数据时没有播放提示音。

## 已完成的修复

### 1. 配置AVAudioSession (AppDelegate.swift)
已在 `ios/Runner/AppDelegate.swift` 中添加音频会话配置:

```swift
import AVFoundation

// 在application方法中配置
try AVAudioSession.sharedInstance().setCategory(.playback, mode: .default, options: [.mixWithOthers])
try AVAudioSession.sharedInstance().setActive(true)
```

**作用**: 
- `.playback`: 允许应用播放音频
- `.mixWithOthers`: 允许与其他音频(如音乐播放器)同时播放
- `setActive(true)`: 激活音频会话

### 2. 优化声音播放逻辑
在 `lib/pages/bluetooth_page.dart` 中增强了日志输出,方便调试。

## 排查步骤

### 步骤1: 确认设置已开启
1. 进入应用的"设置"页面
2. 确保"蓝牙接收声音"开关已打开
3. 查看控制台日志,确认看到:
   ```
   [蓝牙] ✓ 声音已开启,接收数据时将播放提示音
   ```

### 步骤2: 检查音频文件
确认以下文件存在:
- `assets/sounds/notification.mp3` (0.3KB)
- `pubspec.yaml` 中已配置:
  ```yaml
  assets:
    - assets/sounds/
  ```

### 步骤3: 重新编译iOS应用
修改了原生代码(AppDelegate.swift),必须重新编译:

```bash
# 清理构建缓存
flutter clean

# 重新获取依赖
flutter pub get

# 进入iOS目录
cd ios

# 更新CocoaPods
pod install --repo-update

# 返回项目根目录
cd ..

# 运行应用
flutter run
```

### 步骤4: 查看运行时日志
连接设备后,观察控制台输出:

**正常情况应该看到**:
```
[iOS音频] AVAudioSession配置成功
[蓝牙] ========== 声音设置加载 ==========
[蓝牙] ✓ 声音已开启,接收数据时将播放提示音
[蓝牙] 音频文件: assets/sounds/notification.mp3
[蓝牙] 请确认iOS AVAudioSession已配置(见AppDelegate.swift)
```

**接收数据时应该看到**:
```
[蓝牙] ========== 开始播放提示音 ==========
[蓝牙] 音量: 0.8, 连续次数: 1
[蓝牙] 尝试播放本地音频: assets/sounds/notification.mp3
[蓝牙] ✓ 播放命令已发送
[蓝牙] ==========================================
```

**如果失败会看到**:
```
[蓝牙] ✗ 本地音频文件播放失败: <错误信息>
[蓝牙] 请检查:
[蓝牙] 1. assets/sounds/notification.mp3 是否存在
[蓝牙] 2. pubspec.yaml 中是否正确配置了 assets
[蓝牙] 3. iOS AVAudioSession 是否配置成功
```

### 步骤5: 检查设备音量
1. 确保iOS设备未静音(侧边静音开关关闭)
2. 调高系统音量
3. 测试其他应用的声音是否正常

### 步骤6: 真机测试
⚠️ **重要**: iOS模拟器可能无法正确播放音频,建议使用真机测试。

## 常见问题

### Q1: 看到 "AVAudioSession配置失败" 错误
**原因**: iOS权限问题或系统限制
**解决**: 
- 重启设备
- 检查应用是否有音频权限
- 确认Info.plist配置正确

### Q2: 日志显示播放成功但听不到声音
**可能原因**:
1. 设备静音或音量太低
2. 蓝牙设备占用了音频通道
3. 音频文件格式问题

**解决**:
- 检查设备音量
- 断开其他蓝牙音频设备
- 尝试替换notification.mp3文件

### Q3: 连续接收数据时声音间隔太长
**调整**: 修改 `_minSoundInterval` 参数(当前500ms)
```dart
static const Duration _minSoundInterval = Duration(milliseconds: 500);
```

### Q4: 想要更大的音量
**调整**: 修改默认音量参数
```dart
double volume = 1.0; // 最大音量
```

## 高级调试

### 启用audioplayers详细日志
在 `main.dart` 中添加:
```dart
import 'package:audioplayers/audioplayers.dart';

void main() {
  AudioPlayer.global.logLevel = LogLevel.verbose;
  runApp(const MyApp());
}
```

### 检查iOS音频会话状态
在Xcode控制台中输入:
```lldb
po AVAudioSession.sharedInstance().category
po AVAudioSession.sharedInstance().isActive
```

## 验证修复

完成所有步骤后:
1. ✅ 设置页面开启"蓝牙接收声音"
2. ✅ 连接蓝牙设备
3. ✅ 点击"同步"开始接收数据
4. ✅ 每次接收数据时应该听到短促的提示音
5. ✅ 连续接收时音量会逐渐增大(增强提示)

## 联系支持

如果以上步骤都无法解决问题,请提供:
1. 完整的控制台日志
2. iOS版本号
3. 设备型号
4. 是否使用真机测试
