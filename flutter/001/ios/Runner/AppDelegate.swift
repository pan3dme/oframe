import Flutter
import UIKit
import AVFoundation

@main
@objc class AppDelegate: FlutterAppDelegate {
  private var notificationPlayer: AVAudioPlayer?
  
  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    // 配置音频会话
    do {
      try AVAudioSession.sharedInstance().setCategory(
        .playback,
        mode: .default,
        options: [.mixWithOthers, .allowBluetooth]
      )
      try AVAudioSession.sharedInstance().setActive(true)
      print("[iOS音频] AVAudioSession配置成功")
    } catch {
      print("[iOS音频] AVAudioSession配置失败: \(error)")
    }
    
    // 预加载音效文件
    preloadNotificationSound()
    
    // 注册声音播放MethodChannel
    if let controller = window?.rootViewController as? FlutterViewController {
      let channel = FlutterMethodChannel(name: "com.app/sound", binaryMessenger: controller.binaryMessenger)
      channel.setMethodCallHandler { [weak self] (call, result) in
        if call.method == "playNotification" {
          self?.playNotificationSound()
          result(nil)
        } else {
          result(FlutterMethodNotImplemented)
        }
      }
    }
    
    GeneratedPluginRegistrant.register(with: self)
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }
  
  /// 预加载音效文件
  private func preloadNotificationSound() {
    // 尝试从Flutter asset bundle加载
    if let flutterAssetsPath = Bundle.main.path(forResource: "Frameworks/App.framework/flutter_assets/assets/sounds/notification", ofType: "wav") {
      let url = URL(fileURLWithPath: flutterAssetsPath)
      do {
        notificationPlayer = try AVAudioPlayer(contentsOf: url)
        notificationPlayer?.prepareToPlay()
        notificationPlayer?.volume = 0.8
        print("[iOS音频] 成功加载notification.wav")
        return
      } catch {
        print("[iOS音频] 加载notification.wav失败: \(error)")
      }
    }
    
    // 备用：从main bundle查找
    if let path = Bundle.main.path(forResource: "notification", ofType: "wav") {
      let url = URL(fileURLWithPath: path)
      do {
        notificationPlayer = try AVAudioPlayer(contentsOf: url)
        notificationPlayer?.prepareToPlay()
        notificationPlayer?.volume = 0.8
        print("[iOS音频] 成功加载main bundle中的notification.wav")
        return
      } catch {
        print("[iOS音频] 加载main bundle wav失败: \(error)")
      }
    }
    
    print("[iOS音频] 警告: 未找到notification.wav文件")
  }
  
  /// 播放提示音
  private func playNotificationSound() {
    if let player = notificationPlayer {
      player.currentTime = 0 // 重置到开头（支持快速连续播放）
      player.play()
    }
  }
}
