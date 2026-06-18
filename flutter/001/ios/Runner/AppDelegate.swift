import Flutter
import UIKit
import AVFoundation

@main
@objc class AppDelegate: FlutterAppDelegate {
  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    // 配置音频会话，允许后台播放和与其他音频混合
    do {
      // .playback: 允许后台音频播放
      // .mixWithOthers: 允许与其他音频同时播放（不中断音乐播放器）
      // .duckOthers: 降低其他音频音量（可选）
      try AVAudioSession.sharedInstance().setCategory(
        .playback,
        mode: .default,
        options: [.mixWithOthers, .allowBluetooth]
      )
      try AVAudioSession.sharedInstance().setActive(true)
      print("[iOS音频] AVAudioSession配置成功 - 支持后台蓝牙音频播放")
    } catch {
      print("[iOS音频] AVAudioSession配置失败: \(error)")
    }
    
    GeneratedPluginRegistrant.register(with: self)
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }
}
