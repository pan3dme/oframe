# iOS蓝牙接收震动提示功能

## 问题背景
iOS设备上音频播放可能存在以下问题:
- AVAudioSession配置复杂
- 音频文件格式要求严格
- 模拟器可能无法播放音频
- 某些设备权限限制

## 解决方案:添加震动提示

### 已完成的修改

#### 1. 添加vibration依赖
在 `pubspec.yaml` 中添加:
```yaml
dependencies:
  vibration: ^1.8.4
```

#### 2. 导入震动库
在 `lib/pages/bluetooth_page.dart` 中添加:
```dart
import 'package:vibration/vibration.dart';
```

#### 3. 修改播放提示音函数
在 `_playNotificationSound()` 方法中:
- **先执行震动**(更可靠)
- 再尝试播放声音(作为补充)

**震动强度根据连续接收次数调整**:
- 连续接收 ≤ 5次: 轻震动 (100ms)
- 连续接收 > 5次: 强震动 (200ms)

### 工作原理

```dart
// 检查设备是否支持震动
bool hasVibrator = await Vibration.hasVibrator() ?? false;
if (hasVibrator) {
  // 根据连续次数调整震动强度
  if (_consecutiveCount > 5) {
    Vibration.vibrate(duration: 200); // 强震动
  } else {
    Vibration.vibrate(duration: 100); // 轻震动
  }
}
```

### 优势

1. **更可靠**: 震动不依赖音频会话配置
2. **即时反馈**: 震动响应速度快
3. **跨平台**: iOS和Android都支持
4. **无需文件**: 不需要音频文件资源
5. **省电**: 震动比音频播放更省电

### 测试步骤

1. **重新编译应用**:
   ```bash
   flutter clean
   flutter pub get
   cd ios && pod install
   cd ..
   flutter run
   ```

2. **开启声音设置**:
   - 进入"设置"页面
   - 打开"蓝牙接收声音"开关
   - 控制台应显示: `[蓝牙] ✓ 声音已开启,接收数据时将播放提示音+震动`

3. **连接蓝牙并同步**:
   - 进入蓝牙页面
   - 连接设备
   - 点击"同步"开始接收数据

4. **感受震动**:
   - 每次接收数据时手机会震动
   - 前5次:轻微短促震动(100ms)
   - 第6次起:较强震动(200ms)增强提示

5. **查看日志**:
   ```
   [蓝牙] 设备支持震动，执行震动提示
   [蓝牙] ✓ 轻震动 (100ms)
   或
   [蓝牙] ✓ 强震动 (200ms)
   ```

### iOS注意事项

#### 权限配置
vibration插件在iOS上**不需要**额外权限配置,可以直接使用。

#### 静音模式
- ✅ **震动在静音模式下仍然工作**
- ✅ 不受侧边静音开关影响
- ✅ 即使音量为0也能感受到震动

#### 真机测试
⚠️ **重要**: 
- iOS模拟器不支持震动
- 必须使用真机测试
- 确保手持设备以感受震动

### 常见问题

#### Q1: 没有感觉到震动?
**检查**:
1. 确认使用的是真机而非模拟器
2. 确认"蓝牙接收声音"开关已打开
3. 查看控制台是否有 `[蓝牙] 设备支持震动` 日志
4. 检查设备震动马达是否正常(可以测试系统震动)

#### Q2: 震动太弱或太强?
**调整**: 修改 `bluetooth_page.dart` 中的震动时长:
```dart
// 当前配置
Vibration.vibrate(duration: 100); // 轻震动
Vibration.vibrate(duration: 200); // 强震动

// 可以调整为
Vibration.vibrate(duration: 50);  // 更轻
Vibration.vibrate(duration: 300); // 更强
```

#### Q3: 想要固定震动强度?
**修改**: 移除条件判断,使用固定时长:
```dart
// 始终使用150ms震动
Vibration.vibrate(duration: 150);
```

#### Q4: 只想震动不要声音?
**修改**: 注释掉音频播放代码:
```dart
// 只震动,不播放声音
if (hasVibrator) {
  Vibration.vibrate(duration: 100);
}
// 注释掉下面的音频播放代码
// await _audioPlayer.play(...);
```

### 高级用法

#### 自定义震动模式
vibration插件支持震动模式(震动-暂停-震动):
```dart
// 震动200ms,暂停100ms,再震动200ms
Vibration.vibrate(pattern: [200, 100, 200]);
```

#### 检查震动支持
```dart
bool? hasVibrator = await Vibration.hasVibrator();
bool? hasCustomVibrationsSupport = await Vibration.hasCustomVibrationsSupport();
```

### 与声音的配合

当前实现是**震动+声音**双重提示:
- ✅ 震动: 即时、可靠的触觉反馈
- ✅ 声音: 额外的听觉提示(如果音频配置正确)

即使声音播放失败,震动仍然会执行,确保用户能收到通知。

### 性能考虑

- 震动操作是异步的,不会阻塞UI
- 已有500ms间隔控制,避免频繁震动
- 震动马达有硬件保护,不会因频繁调用损坏

### 总结

震动提示是一个**更可靠、更简单**的通知方式:
1. 不依赖复杂的音频配置
2. 在静音模式下仍然有效
3. 提供即时的触觉反馈
4. 跨平台兼容性好

如果声音仍然无法播放,**震动已经可以作为完美的替代方案**!
