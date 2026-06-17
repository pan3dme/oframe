# 蓝牙提示音文件

此目录包含蓝牙数据接收提示音文件。

## 当前文件
- `notification.wav` - 880Hz蜂鸣声，时长0.2秒（已配置）

## 文件格式要求
- 支持格式：WAV、MP3
- 建议时长：0.1-0.5秒（短促提示音）
- 建议音量：中等音量，避免过于刺耳
- 采样率：44100 Hz
- 声道：单声道或立体声均可

## 自定义提示音
如需更换提示音：
1. 准备WAV或MP3格式的音频文件
2. 重命名为 `notification.wav` 或 `notification.mp3`
3. 替换此目录中的同名文件
4. 修改 `lib/pages/bluetooth_page.dart` 中的文件名
5. 重新编译应用

## 代码配置
音频文件路径在 `lib/pages/bluetooth_page.dart` 中配置：
```dart
await _audioPlayer.play(AssetSource('sounds/notification.wav'));
```

## iOS注意事项
- iOS需要配置AVAudioSession才能播放音频
- 配置位置：`ios/Runner/AppDelegate.swift`
- 确保设备未静音且音量足够
