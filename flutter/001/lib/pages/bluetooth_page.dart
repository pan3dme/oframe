import 'dart:async';
import 'dart:convert';
import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:http/http.dart' as http;
import 'package:flutter/services.dart';
import '../utils/db_helper.dart';

/// 格式化时间为：2026/6/12 12:21:10
String formatTime(DateTime dateTime) {
  return '${dateTime.year}/${dateTime.month}/${dateTime.day} ${dateTime.hour}:${dateTime.minute.toString().padLeft(2, '0')}:${dateTime.second.toString().padLeft(2, '0')}';
}

class BluetoothPage extends StatefulWidget {
  const BluetoothPage({super.key});

  @override
  State<BluetoothPage> createState() => _BluetoothPageState();
}

class _BluetoothPageState extends State<BluetoothPage> {
  final List<BluetoothDevice> _scanResults = [];
  bool _isScanning = false;
  StreamSubscription<List<ScanResult>>? _scanResultSubscription;

  // ---- 静态蓝牙连接资源（跨页面存活，切换页面不中断） ----
  static BluetoothDevice? _connectedDevice;
  static BluetoothConnectionState _connectionState =
      BluetoothConnectionState.disconnected;
  static StreamSubscription<BluetoothConnectionState>? _connectionStateSubscription;

  // 静态同步状态
  static bool _isSyncing = false;
  static BluetoothCharacteristic? _writeCharacteristic;
  static BluetoothCharacteristic? _notifyCharacteristic;
  static StreamSubscription<List<int>>? _notifySubscription;

  // 静态接收数据（跨页面累积）
  static final List<String> _receivedData = [];

  // 页面实例变量
  List<Map<String, dynamic>> _cachedBluetoothData = []; // 缓存的蓝牙数据
  bool _isUploading = false; // 是否正在上传
  bool _uploadPaused = false; // 上传是否因断网暂停
  bool _scanCompleted = false; // 扫描是否已完成
  String? _filterType; // 数据过滤类型: null=全部, '1'=GPS, '2'=对时, '3'=电量
  
  // 声音/震动控制
  static const _soundChannel = MethodChannel('com.app/sound');
  bool _soundEnabled = false; // 是否开启声音
  
  // 为每条数据生成随机背景色（使用固定种子保证同一索引颜色不变）
  Color _getRandomLightColor(int index) {
    final random = Random(index);
    // 生成浅色背景：高亮度 + 低饱和度
    // HSL颜色空间更容易控制亮度
    final hue = random.nextDouble() * 360; // 色相 0-360
    final saturation = 0.15 + random.nextDouble() * 0.15; // 饱和度 0.15-0.3（较低）
    final lightness = 0.85 + random.nextDouble() * 0.10; // 亮度 0.85-0.95（很高）
    
    final hslColor = HSLColor.fromAHSL(
      1.0,
      hue,
      saturation,
      lightness,
    );
    
    return hslColor.toColor();
  }

  @override
  void initState() {
    super.initState();
    // 先加载缓存数据，加载完成后再开始扫描
    _loadCachedBluetoothData().then((_) {
      print('[蓝牙] 缓存数据加载完成，共 ${_cachedBluetoothData.length} 条');
      // 如果已有静态连接（从其他页面返回），恢复状态监听
      if (_connectedDevice != null && _connectionState == BluetoothConnectionState.connected) {
        print('[蓝牙] 检测到已有连接，恢复状态...');
        setState(() {}); // 触发UI重建
      } else {
        // 没有已有连接，正常开始扫描
        WidgetsBinding.instance.addPostFrameCallback((_) {
          print('[蓝牙] 页面加载完成，自动开始扫描...');
          _startScan();
        });
      }
    });
    
    // 加载声音设置
    _loadSoundSettings();
  }
  
  /// 加载声音设置
  Future<void> _loadSoundSettings() async {
    final enabled = await DBHelper().getBoolSetting(
      'bluetooth_sound_enabled',
      defaultValue: false,
    );
    setState(() {
      _soundEnabled = enabled;
    });
    print('[蓝牙] ========== 声音设置加载 ==========');
    print('[蓝牙] 声音开关状态: $enabled');
    if (enabled) {
      print('[蓝牙] ✓ 声音已开启,接收数据时将播放提示音+震动');
    } else {
      print('[蓝牙] ✗ 声音未开启,请在设置页面开启"蓝牙接收声音"');
    }
    print('[蓝牙] ====================================');
  }

  /// 加载缓存的蓝牙数据
  Future<void> _loadCachedBluetoothData() async {
    try {
      final cachedData = await DBHelper().getBluetoothData();
      if (mounted) {
        setState(() {
          _cachedBluetoothData = cachedData;
        });
      }
      print('[蓝牙] 加载缓存数据: ${cachedData.length} 条');
    } catch (e) {
      print('[蓝牙] 加载缓存数据失败: $e');
    }
  }
  
  /// 播放提示音+震动（通过iOS原生AudioServices，不经过AVAudioSession，不与蓝牙冲突）
  void _playNotificationSound() {
    if (!_soundEnabled) return;
    _soundChannel.invokeMethod('playNotification');
  }
  


  /// 切换上传/暂停上传状态
  void _toggleUpload() {
    if (_isUploading) {
      // 暂停上传
      setState(() {
        _isUploading = false;
        _uploadPaused = true;
      });
      print('[蓝牙上传] 用户暂停上传');
    } else {
      // 开始/恢复上传
      setState(() {
        _isUploading = true;
        _uploadPaused = false;
      });
      _processUploadQueue();
    }
  }

  /// 处理上传队列（持续运行，上传中新数据也会自动进入队列）
  Future<void> _processUploadQueue() async {
    while (_isUploading) {
      // 等待缓存数据加载完成
      await _loadCachedBluetoothData();
      
      if (_cachedBluetoothData.isEmpty) {
        // 没有数据时等待一下再检查（新数据可能正在保存中）
        await Future.delayed(const Duration(milliseconds: 500));
        continue;
      }

      final item = _cachedBluetoothData.first;
      try {
        final dataStr = item['data'] ?? '';
        final dataId = item['id'] as int;

        print('[蓝牙上传] 原始数据: $dataStr');
        
        String deviceId = '';
        String lorastr = '';
        String upDateDevice = '';
        String time = item['time'] ?? formatTime(DateTime.now());
        
        try {
          final jsonData = jsonDecode(dataStr) as Map<String, dynamic>;
          lorastr = jsonData['info'] ?? '';
          upDateDevice = jsonData['upDateDevice'] ?? '';
          time = jsonData['time'] ?? time;
          
          if (lorastr.contains('|')) {
            final parts = lorastr.split('|');
            if (parts.length >= 2) {
              deviceId = parts[1];
            }
          }
          
          print('[蓝牙上传] 解析数据: deviceId=$deviceId, lorastr=$lorastr, upDateDevice=$upDateDevice, time=$time');
        } catch (e) {
          print('[蓝牙上传] 解析数据失败: $e');
        }
        
        final resp = await http.post(
          Uri.parse('https://gpsmoveinfo.cn/fc/device'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({
            'action': 'insertlog',
            'info': {
              'deviceId': deviceId,
              'lorastr': lorastr,
              'upDateDevice': upDateDevice,
              'time': time,
            },
          }),
        );

        if (resp.statusCode == 200) {
          final json = jsonDecode(resp.body) as Map<String, dynamic>;
          if (json['status'] == 'success') {
            print('[蓝牙上传] 成功上传: deviceId=$deviceId');
            await DBHelper().deleteBluetoothDataById(dataId);
            await _loadCachedBluetoothData();
          } else {
            print('[蓝牙上传] 上传失败: ${json['msg']}');
            setState(() {
              _isUploading = false;
              _uploadPaused = true;
            });
            if (mounted) {
              ScaffoldMessenger.of(context).showSnackBar(
                SnackBar(
                  content: Text('上传失败: ${json['msg']}'),
                  backgroundColor: Colors.red,
                  duration: const Duration(seconds: 3),
                ),
              );
            }
            break;
          }
        } else {
          print('[蓝牙上传] HTTP错误: ${resp.statusCode}');
          setState(() {
            _isUploading = false;
            _uploadPaused = true;
          });
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text('HTTP错误: ${resp.statusCode}'),
                backgroundColor: Colors.red,
                duration: const Duration(seconds: 3),
              ),
            );
          }
          break;
        }
      } catch (e) {
        print('[蓝牙上传] 上传失败（可能断网）: $e');
        setState(() {
          _isUploading = false;
          _uploadPaused = true;
        });
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text('上传失败（可能断网）: $e'),
              backgroundColor: Colors.red,
              duration: const Duration(seconds: 3),
            ),
          );
        }
        break;
      }
    }
  }

  /// 刷新缓存计数
  Future<void> _refreshCacheCount() async {
    await _loadCachedBluetoothData();
  }

  /// 显示清空缓存确认对话框
  void _showClearCacheDialog() {
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('清空缓存记录'),
        content: Text(
          '确定要清空所有 ${_cachedBluetoothData.length} 条缓存记录吗？\n此操作不可恢复。',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('取消'),
          ),
          TextButton(
            onPressed: () {
              Navigator.pop(context);
              _clearAllCache();
            },
            child: const Text(
              '确定',
              style: TextStyle(color: Colors.red),
            ),
          ),
        ],
      ),
    );
  }

  /// 清空所有缓存数据
  Future<void> _clearAllCache() async {
    try {
      await DBHelper().clearBluetoothData();
      await _loadCachedBluetoothData();
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('已清空所有缓存记录'),
            backgroundColor: Colors.green,
          ),
        );
      }
      print('[蓝牙] 已清空所有缓存数据');
    } catch (e) {
      print('[蓝牙] 清空缓存失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('清空失败: $e'),
            backgroundColor: Colors.red,
          ),
        );
      }
    }
  }

  @override
  void dispose() {
    // 【重要】不清理蓝牙连接和同步状态！
    // 连接资源是static的，切换页面时保持活跃
    // 只有用户主动点击「断开连接」才会断开
    
    // 只清理扫描相关
    _scanResultSubscription?.cancel();
    try { FlutterBluePlus.stopScan(); } catch (_) {}
    
    // 停止上传（上传是页面级操作）
    if (_isUploading) {
      _isUploading = false;
      print('[蓝牙] 页面退出，停止上传');
    }
    
    super.dispose();
  }

  bool get _isConnected =>
      _connectionState == BluetoothConnectionState.connected;

  // ---- 扫描 ----
  Future<void> _startScan() async {
    setState(() {
      _scanResults.clear();
      _isScanning = true;
    });

    try {
      // 在开始扫描前，先停止任何正在进行的扫描
      await FlutterBluePlus.stopScan();
      // 短暂延迟，确保蓝牙协议栈稳定
      await Future.delayed(const Duration(milliseconds: 300));
      
      print('[蓝牙] 准备开始扫描...');
      
      _scanResultSubscription = FlutterBluePlus.scanResults.listen((results) {
        print('[蓝牙] 扫描结果: ${results.length} 个设备');
        setState(() {
          _scanResults.clear();
          for (final r in results) {
            final name = r.device.platformName;
            print('[蓝牙] 发现设备: "$name" (rssi: ${r.rssi})');
            // 只显示名称包含「牛羊」的设备
            if (name.contains('牛羊') &&
                !_scanResults.any((d) => d.remoteId == r.device.remoteId)) {
              print('[蓝牙] ✓ 添加到列表: $name');
              _scanResults.add(r.device);
            } else if (!name.contains('牛羊')) {
              print('[蓝牙] ✗ 过滤掉: $name');
            }
          }
        });
      });

      print('[蓝牙] 开始扫描，超时10秒，仅查找「牛羊」设备');
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
    } catch (e) {
      print('[蓝牙] 扫描失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('扫描失败: $e'),
            backgroundColor: Colors.orange,
          ),
        );
      }
    } finally {
      setState(() {
        _isScanning = false;
        _scanCompleted = true; // 标记扫描已完成
      });
      print('[蓝牙] 扫描结束，共发现 ${_scanResults.length} 个设备');
    }
  }

  // ---- 连接 ----
  Future<void> _connectToDevice(BluetoothDevice device) async {
    final deviceName = _getDeviceName(device);
    print('[蓝牙] 尝试连接设备: $deviceName, ID: ${device.remoteId.str}');
    
    try {
      await FlutterBluePlus.stopScan();
      setState(() {
        _isScanning = false;
      });

      print('[蓝牙] 开始连接...');
      await device.connect(
        timeout: const Duration(seconds: 30),
        autoConnect: false,
      );
      print('[蓝牙] 连接成功，监听状态变化');

      _connectionStateSubscription = device.connectionState.listen((state) {
        print('[蓝牙] 连接状态变化: $state');
        setState(() {
          _connectionState = state;
        });
        if (state == BluetoothConnectionState.connected) {
          _connectedDevice = device;
          print('[蓝牙] 已连接，开始发现服务');
          _discoverServices(device);
        } else if (state == BluetoothConnectionState.disconnected) {
          print('[蓝牙] 已断开');
          _connectedDevice = null;
          _writeCharacteristic = null;
          _notifyCharacteristic = null;
          _isSyncing = false;
          _connectionStateSubscription?.cancel();
          _connectionStateSubscription = null;
          _notifySubscription?.cancel();
          _notifySubscription = null;
        }
      });

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('已连接: $deviceName')),
        );
      }
    } catch (e) {
      print('[蓝牙] 连接失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('连接失败: $deviceName\n错误: $e'),
            duration: const Duration(seconds: 5),
            backgroundColor: Colors.red,
          ),
        );
      }
    }
  }

  // ---- 发现服务和特征值 ----
  Future<void> _discoverServices(BluetoothDevice device) async {
    print('[蓝牙] 开始发现服务...');
    try {
      final services = await device.discoverServices();
      print('[蓝牙] 发现 ${services.length} 个服务');
      
      BluetoothCharacteristic? writeChar;
      BluetoothCharacteristic? notifyChar;
      for (final service in services) {
        print('[蓝牙] 服务: ${service.uuid.str}');
        for (final characteristic in service.characteristics) {
          print('[蓝牙]   特征值: ${characteristic.characteristicUuid.str}, '
              'write=${characteristic.properties.write}, '
              'notify=${characteristic.properties.notify}');
          if (characteristic.properties.write && writeChar == null) {
            writeChar = characteristic;
            print('[蓝牙]   ✓ 找到可写特征值');
          }
          if (characteristic.properties.notify && notifyChar == null) {
            notifyChar = characteristic;
            print('[蓝牙]   ✓ 找到可通知特征值');
          }
        }
      }
      setState(() {
        _writeCharacteristic = writeChar;
        _notifyCharacteristic = notifyChar;
      });
      if (writeChar == null && mounted) {
        print('[蓝牙] 警告: 没有找到可写特征值');
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('该设备没有可写的特征值，无法同步'),
            backgroundColor: Colors.orange,
          ),
        );
      }
    } catch (e) {
      print('[蓝牙] 发现服务失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('发现服务失败: $e'),
            backgroundColor: Colors.red,
          ),
        );
      }
    }
  }

  // ---- 同步（发送 syncing=true + 订阅通知） ----
  Future<void> _startSync() async {
    if (_writeCharacteristic == null) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('没有可写的特征值')),
      );
      return;
    }
    try {
      final syncData = jsonEncode({
        'syncing': true,
        'time': formatTime(DateTime.now()),
      });
      await _writeCharacteristic!.write(utf8.encode(syncData));
      setState(() {
        _isSyncing = true;
        _receivedData.clear();
      });

      // 订阅通知特征值以接收数据
      if (_notifyCharacteristic != null) {
        await _notifyCharacteristic!.setNotifyValue(true);
        _notifySubscription =
            _notifyCharacteristic!.onValueReceived.listen((value) {
          final data = utf8.decode(value, allowMalformed: true);
          print('[蓝牙] 收到数据: $data');
          
          // 立即更新UI（不阻塞后续数据接收）
          setState(() {
            _receivedData.add(data);
          });
          
          // 后台处理：保存数据库、刷新缓存、播放声音（不阻塞监听器）
          _handleReceivedDataInBackground(data);
        });
      }

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('已开始同步')),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('同步失败: $e')),
        );
      }
    }
  }

  // ---- 停止同步（发送 syncing=false + 取消订阅） ----
  Future<void> _stopSync() async {
    if (_writeCharacteristic == null) return;
    try {
      final syncData = jsonEncode({
        'syncing': false,
        'time': formatTime(DateTime.now()),
      });
      await _writeCharacteristic!.write(utf8.encode(syncData));

      // 取消订阅通知
      if (_notifyCharacteristic != null) {
        await _notifyCharacteristic!.setNotifyValue(false);
        _notifySubscription?.cancel();
        _notifySubscription = null;
      }

      setState(() {
        _isSyncing = false;
      });
      
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('已停止同步')),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('停止同步失败: $e')),
        );
      }
    }
  }

  // ---- 断开连接 ----
  Future<void> _disconnect() async {
    try {
      if (_isSyncing && _writeCharacteristic != null) {
        final syncData = jsonEncode({
          'syncing': false,
          'time': formatTime(DateTime.now()),
        });
        await _writeCharacteristic!.write(utf8.encode(syncData));
        if (_notifyCharacteristic != null) {
          await _notifyCharacteristic!.setNotifyValue(false);
          _notifySubscription?.cancel();
          _notifySubscription = null;
        }
        setState(() {
          _isSyncing = false;
        });
      }
      if (_connectedDevice != null) {
        print('[蓝牙] 开始断开设备: ${_getDeviceName(_connectedDevice!)}');
        await _connectedDevice!.disconnect();
        print('[蓝牙] 设备已断开');
        
        // 取消订阅
        _connectionStateSubscription?.cancel();
        _connectionStateSubscription = null;
        _notifySubscription?.cancel();
        _notifySubscription = null;
        
        setState(() {
          _connectedDevice = null;
          _connectionState = BluetoothConnectionState.disconnected;
          _writeCharacteristic = null;
          _notifyCharacteristic = null;
          _receivedData.clear();
        });
        print('[蓝牙] 状态已重置');
        
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('已断开连接')),
          );
        }
      }
    } catch (e) {
      print('[蓝牙] 断开失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('断开失败: $e')),
        );
      }
    }
  }

  String _getDeviceName(BluetoothDevice device) {
    final name = device.platformName;
    if (name.isEmpty) return '未知设备';
    return name;
  }

  /// 后台处理接收到的蓝牙数据（不阻塞通知监听器）
  void _handleReceivedDataInBackground(String data) {
    // 保存数据库和刷新缓存用async执行
    _saveAndRefreshCache(data);
    // 播放声音（完全fire-and-forget）
    _playNotificationSound();
  }

  Future<void> _saveAndRefreshCache(String data) async {
    try {
      // 只缓存类型1(GPS)、2(对时)、3(电量)的数据，其它类型不入缓存
      String typeStr = '';
      try {
        final jsonData = jsonDecode(data) as Map<String, dynamic>;
        final info = jsonData['info'] as String? ?? '';
        if (info.contains('|')) {
          typeStr = info.split('|')[0];
        }
      } catch (_) {}
      
      if (typeStr != '1' && typeStr != '2' && typeStr != '3') {
        print('[蓝牙] 跳过非类型1/2/3数据，不入缓存: type=$typeStr');
        return;
      }
      
      final deviceName = _connectedDevice != null ? _getDeviceName(_connectedDevice!) : '未知设备';
      final deviceId = _connectedDevice?.remoteId.str ?? '';
      final time = formatTime(DateTime.now());
      await DBHelper().saveBluetoothData(deviceName, deviceId, data, time);
      if (mounted) {
        await _loadCachedBluetoothData();
      }
    } catch (e) {
      print('[蓝牙] 后台处理数据失败: $e');
    }
  }

  /// 格式化显示接收的JSON数据（根据info类型区分显示）
  Widget _formatReceivedData(String jsonData) {
    try {
      final data = jsonDecode(jsonData) as Map<String, dynamic>;
      final info = data['info'] ?? '';
      
      // 从info中提取类型号（第一个|前的数字）
      String typeStr = '';
      if (info.contains('|')) {
        typeStr = info.split('|')[0];
      }
      
      // 根据类型选择不同的显示方式
      if (typeStr == '1') {
        // GPS信息：完整显示 rssi/snr/info/time
        return _formatGpsData(data, info);
      } else if (typeStr == '2') {
        // 时间同步信息：显示 rssi/snr/info/time
        return _formatTimeSyncData(data, info);
      } else if (typeStr == '3') {
        // 电量信息：显示 rssi/snr/info/time
        return _formatBatteryData(data, info);
      } else {
        // 其它类型：只显示info内容
        return _formatOtherData(data, info);
      }
    } catch (e) {
      // 如果解析失败，直接显示原始文本
      return Text(
        jsonData,
        style: const TextStyle(fontSize: 17),
      );
    }
  }

  /// 格式化GPS数据（类型1）
  Widget _formatGpsData(Map<String, dynamic> data, String info) {
    final rssi = data['rssi'] ?? '';
    final snr = data['snr'] ?? '';
    final upDateDevice = data['upDateDevice'] ?? '';
    final time = data['time'] ?? '';
    
    String deviceId = '';
    if (info.contains('|')) {
      final parts = info.split('|');
      if (parts.length >= 2) {
        deviceId = parts[1];
      }
    }
    
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // 第一行：rssi和snr，数值部分绿色显示
        LayoutBuilder(
          builder: (context, constraints) {
            return RichText(
              text: TextSpan(
                style: const TextStyle(fontSize: 17, color: Colors.grey, fontWeight: FontWeight.normal),
                children: [
                  const TextSpan(text: '"rssi":'),
                  TextSpan(
                    text: '$rssi',
                    style: const TextStyle(color: Colors.green),
                  ),
                  const TextSpan(text: ',"snr":'),
                  TextSpan(
                    text: '$snr',
                    style: const TextStyle(color: Colors.green),
                  ),
                ],
              ),
              softWrap: true,
              overflow: TextOverflow.visible,
            );
          },
        ),
        const SizedBox(height: 4),
        // 第二行：GPS图标 + 完整info，其中deviceId部分红色显示
        if (info.isNotEmpty)
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Padding(
                padding: EdgeInsets.only(top: 2, right: 4),
                child: Icon(Icons.gps_fixed, size: 16, color: Colors.blue),
              ),
              Expanded(child: _buildInfoWithHighlightedDeviceId(info, deviceId)),
            ],
          ),
        const SizedBox(height: 4),
        // 第三行：时间和上传设备
        if (time.isNotEmpty || upDateDevice.isNotEmpty)
          Text(
            '${time.isNotEmpty ? time : ''}${upDateDevice.isNotEmpty ? ' ($upDateDevice)' : ''}',
            style: const TextStyle(fontSize: 15),
          ),
      ],
    );
  }

  /// 格式化时间同步数据（类型2）
  Widget _formatTimeSyncData(Map<String, dynamic> data, String info) {
    final rssi = data['rssi'] ?? '';
    final snr = data['snr'] ?? '';
    final upDateDevice = data['upDateDevice'] ?? '';
    final time = data['time'] ?? '';
    
    String deviceId = '';
    if (info.contains('|')) {
      final parts = info.split('|');
      if (parts.length >= 2) {
        deviceId = parts[1];
      }
    }
    
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // 第一行：rssi和snr
        LayoutBuilder(
          builder: (context, constraints) {
            return RichText(
              text: TextSpan(
                style: const TextStyle(fontSize: 17, color: Colors.grey, fontWeight: FontWeight.normal),
                children: [
                  const TextSpan(text: '"rssi":'),
                  TextSpan(
                    text: '$rssi',
                    style: const TextStyle(color: Colors.green),
                  ),
                  const TextSpan(text: ',"snr":'),
                  TextSpan(
                    text: '$snr',
                    style: const TextStyle(color: Colors.green),
                  ),
                ],
              ),
              softWrap: true,
              overflow: TextOverflow.visible,
            );
          },
        ),
        const SizedBox(height: 4),
        // 第二行：时间图标 + info内容
        if (info.isNotEmpty)
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Padding(
                padding: EdgeInsets.only(top: 2, right: 4),
                child: Icon(Icons.access_time, size: 16, color: Colors.orange),
              ),
              Expanded(child: _buildInfoWithHighlightedDeviceId(info, deviceId)),
            ],
          ),
        const SizedBox(height: 4),
        // 第三行：时间和上传设备
        if (time.isNotEmpty || upDateDevice.isNotEmpty)
          Text(
            '${time.isNotEmpty ? time : ''}${upDateDevice.isNotEmpty ? ' ($upDateDevice)' : ''}',
            style: const TextStyle(fontSize: 15),
          ),
      ],
    );
  }

  /// 格式化电量数据（类型3）
  Widget _formatBatteryData(Map<String, dynamic> data, String info) {
    final rssi = data['rssi'] ?? '';
    final snr = data['snr'] ?? '';
    final upDateDevice = data['upDateDevice'] ?? '';
    final time = data['time'] ?? '';
    
    String deviceId = '';
    if (info.contains('|')) {
      final parts = info.split('|');
      if (parts.length >= 2) {
        deviceId = parts[1];
      }
    }
    
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // 第一行：rssi和snr，数值部分绿色显示
        LayoutBuilder(
          builder: (context, constraints) {
            return RichText(
              text: TextSpan(
                style: const TextStyle(fontSize: 17, color: Colors.grey, fontWeight: FontWeight.normal),
                children: [
                  const TextSpan(text: '"rssi":'),
                  TextSpan(
                    text: '$rssi',
                    style: const TextStyle(color: Colors.green),
                  ),
                  const TextSpan(text: ',"snr":'),
                  TextSpan(
                    text: '$snr',
                    style: const TextStyle(color: Colors.green),
                  ),
                ],
              ),
              softWrap: true,
              overflow: TextOverflow.visible,
            );
          },
        ),
        const SizedBox(height: 4),
        // 第二行：电量图标 + 完整info，其中deviceId部分红色显示
        if (info.isNotEmpty)
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Padding(
                padding: EdgeInsets.only(top: 2, right: 4),
                child: Icon(Icons.battery_full, size: 16, color: Colors.green),
              ),
              Expanded(child: _buildInfoWithHighlightedDeviceId(info, deviceId)),
            ],
          ),
        const SizedBox(height: 4),
        // 第三行：时间和上传设备
        if (time.isNotEmpty || upDateDevice.isNotEmpty)
          Text(
            '${time.isNotEmpty ? time : ''}${upDateDevice.isNotEmpty ? ' ($upDateDevice)' : ''}',
            style: const TextStyle(fontSize: 15),
          ),
      ],
    );
  }

  /// 格式化其它类型数据：只显示info内容
  Widget _formatOtherData(Map<String, dynamic> data, String info) {
    if (info.isEmpty) {
      // 没有info字段时，显示完整JSON
      return Text(
        jsonEncode(data),
        style: const TextStyle(fontSize: 17),
      );
    }
    return Text(
      info,
      style: const TextStyle(fontSize: 17),
    );
  }

  /// 构建info文本，其中deviceId部分用红色显示，最后部分数字加粗
  Widget _buildInfoWithHighlightedDeviceId(String info, String deviceId) {
    if (deviceId.isEmpty || !info.contains(deviceId)) {
      return Text(
        info,
        style: const TextStyle(fontSize: 17),
      );
    }
    
    // 将info按deviceId分割，构建带高亮的文本
    final parts = info.split(deviceId);
    final List<TextSpan> spans = [];
    
    for (int i = 0; i < parts.length; i++) {
      if (i > 0) {
        // 添加红色的deviceId
        spans.add(TextSpan(
          text: deviceId,
          style: const TextStyle(
            fontSize: 17,
            color: Colors.red,
            fontWeight: FontWeight.bold,
          ),
        ));
      }
      // 添加普通文本，但需要检查是否是最后一部分且有数字
      if (i == parts.length - 1 && parts[i].contains('|')) {
        // 最后一部分，检查是否有最后的数字
        final lastParts = parts[i].split('|');
        for (int j = 0; j < lastParts.length; j++) {
          if (j > 0) {
            spans.add(const TextSpan(text: '|'));
          }
          if (j == lastParts.length - 1) {
            // 最后一部分数字，加粗加黑
            spans.add(TextSpan(
              text: lastParts[j],
              style: const TextStyle(
                fontSize: 17,
                fontWeight: FontWeight.bold,
              ),
            ));
          } else {
            spans.add(TextSpan(text: lastParts[j]));
          }
        }
      } else {
        // 添加普通文本
        spans.add(TextSpan(text: parts[i]));
      }
    }
    
    return LayoutBuilder(
      builder: (context, constraints) {
        return RichText(
          text: TextSpan(
            children: spans,
            style: DefaultTextStyle.of(context).style.copyWith(fontSize: 17),
          ),
          softWrap: true,
          overflow: TextOverflow.visible,
        );
      },
    );
  }

  // ---- 过滤接收到的数据 ----
  List<String> _getFilteredReceivedData() {
    if (_filterType == null) return _receivedData;
    return _receivedData.where((item) {
      try {
        final data = jsonDecode(item) as Map<String, dynamic>;
        final info = data['info'] ?? '';
        if (info.contains('|')) {
          return info.split('|')[0] == _filterType;
        }
      } catch (_) {}
      return false;
    }).toList();
  }

  // ---- 过滤缓存数据 ----
  List<Map<String, dynamic>> _getFilteredCachedData() {
    if (_filterType == null) return _cachedBluetoothData;
    return _cachedBluetoothData.where((item) {
      try {
        final dataStr = item['data'] as String? ?? '';
        final data = jsonDecode(dataStr) as Map<String, dynamic>;
        final info = data['info'] ?? '';
        if (info.contains('|')) {
          return info.split('|')[0] == _filterType;
        }
      } catch (_) {}
      return false;
    }).toList();
  }

  // ---- 构建数据列表 ----
  Widget _buildDataList() {
    final filteredData = _getFilteredReceivedData();
    // 如果已连接设备，显示接收到的数据（无论是否正在同步）
    if (_isConnected) {
      // 有接收数据时，显示数据列表
      if (filteredData.isNotEmpty) {
        return ListView.builder(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          itemCount: filteredData.length,
          itemBuilder: (context, index) {
            // 从后往前遍历，让最新的数据显示在最上面
            final dataIndex = filteredData.length - 1 - index;
            return Card(
              margin: const EdgeInsets.symmetric(vertical: 2),
              color: _getRandomLightColor(dataIndex), // 随机浅色背景
              child: Padding(
                padding:
                    const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                child: _formatReceivedData(filteredData[dataIndex]),
              ),
            );
          },
        );
      }
      
      // 没有接收数据时，显示提示
      return const Center(
        child: Text(
          '点击「同步」开始接收数据',
          textAlign: TextAlign.center,
          style: TextStyle(color: Colors.grey),
        ),
      );
    }
    
    // 未连接设备时，显示缓存的蓝牙数据
    final filteredCachedData = _getFilteredCachedData();
    if (filteredCachedData.isNotEmpty) {
      return ListView.builder(
        padding: const EdgeInsets.symmetric(horizontal: 16),
        itemCount: filteredCachedData.length,
        itemBuilder: (context, index) {
          // 数据库已按cached_at DESC排序，直接显示即可（最新在最上面）
          final item = filteredCachedData[index];
          final dataStr = item['data'] ?? '';
          
          return Card(
            margin: const EdgeInsets.symmetric(vertical: 2),
            color: _getRandomLightColor(index), // 随机浅色背景
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              child: _formatReceivedData(dataStr),
            ),
          );
        },
      );
    }
    
    // 没有缓存数据时的提示
    return const Center(
      child: Text(
        '暂无缓存数据\n点击「同步」开始接收',
        textAlign: TextAlign.center,
        style: TextStyle(color: Colors.grey),
      ),
    );
  }

  // ---- UI ----
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text('蓝牙'),
            const SizedBox(width: 8),
            Icon(
              _isConnected ? Icons.bluetooth_connected : Icons.bluetooth_disabled,
              size: 20,
              color: _isConnected ? Colors.green : Colors.grey,
            ),
            if (_isConnected && _connectedDevice != null) ...[
              const SizedBox(width: 4),
              Text(
                _getDeviceName(_connectedDevice!),
                style: const TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.normal,
                ),
              ),
            ],
          ],
        ),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: Column(
        children: [
          // 缓存数据Card
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 2),
            child: GestureDetector(
              onDoubleTap: _cachedBluetoothData.isEmpty ? null : _showClearCacheDialog,
              child: Card(
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
                  child: Row(
                    children: [
                      const Icon(Icons.cloud_upload, size: 20, color: Colors.blue),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Text(
                          '缓存记录: ${_cachedBluetoothData.length} 条',
                          style: const TextStyle(
                            fontSize: 13,
                            fontWeight: FontWeight.w500,
                          ),
                        ),
                      ),
                      ElevatedButton.icon(
                        onPressed: _toggleUpload,
                        icon: _isUploading
                            ? const SizedBox(
                                width: 14,
                                height: 14,
                                child: CircularProgressIndicator(
                                  strokeWidth: 2,
                                  color: Colors.white,
                                ),
                              )
                            : (_uploadPaused
                                ? const Icon(Icons.play_arrow, size: 16)
                                : const Icon(Icons.upload, size: 16)),
                        label: Text(_isUploading ? '上传中' : (_uploadPaused ? '暂停上传' : '上传')),
                        style: ElevatedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 10,
                            vertical: 6,
                          ),
                          backgroundColor: _isUploading ? Colors.green : (_uploadPaused ? Colors.orange : Colors.blue),
                          foregroundColor: Colors.white,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),

          // 操作按钮Card
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            child: Card(
              child: Padding(
                padding: const EdgeInsets.all(12),
                child: Row(
                  children: [
                    // 左按钮：未连接「连接蓝牙」 / 已连接「断开连接」
                    Expanded(
                      child: ElevatedButton.icon(
                        onPressed: _isConnected
                            ? _disconnect
                            : (_isScanning ? null : _startScan),
                        icon: Icon(
                          _isConnected
                              ? Icons.bluetooth_disabled
                              : (_isScanning
                                  ? Icons.search
                                  : Icons.bluetooth_searching),
                        ),
                        label: Text(
                          _isConnected
                              ? '断开连接'
                              : (_isScanning ? '扫描中...' : '连接蓝牙'),
                        ),
                        style: ElevatedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(vertical: 12),
                          backgroundColor: _isConnected ? Colors.red : Colors.blue,
                          foregroundColor: Colors.white,
                        ),
                      ),
                    ),
                    const SizedBox(width: 16),
                    // 右按钮：未连接灰色「同步数据」 / 已连接「同步」 / 同步中「停止同步」
                    Expanded(
                      child: ElevatedButton.icon(
                        onPressed: _isConnected
                            ? (_isSyncing ? _stopSync : _startSync)
                            : null,
                        icon: Icon(
                          _isSyncing ? Icons.sync_disabled : Icons.sync,
                        ),
                        label: Text(
                          _isSyncing
                              ? '停止同步'
                              : (_isConnected ? '同步' : '同步数据'),
                        ),
                        style: ElevatedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(vertical: 12),
                          backgroundColor: _isSyncing ? Colors.orange : Colors.blue,
                          foregroundColor: Colors.white,
                          disabledBackgroundColor: Colors.grey.shade300,
                          disabledForegroundColor: Colors.grey.shade600,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),

          // 扫描中提示
          if (_isScanning)
            const Padding(
              padding: EdgeInsets.all(8.0),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  SizedBox(
                    width: 16,
                    height: 16,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  ),
                  SizedBox(width: 8),
                  Text('正在扫描附近蓝牙设备...'),
                ],
              ),
            ),

          // 设备列表 / 已连接信息
          Expanded(
            child: _isConnected
                ? _buildConnectedInfo()
                : Column(
                    children: [
                      // 扫描结果区域（动态高度）
                      if (_isScanning)
                        // 正在扫描时显示提示
                        Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                          child: Card(
                            child: Padding(
                              padding: const EdgeInsets.all(12),
                              child: Row(
                                children: [
                                  const SizedBox(
                                    width: 16,
                                    height: 16,
                                    child: CircularProgressIndicator(strokeWidth: 2),
                                  ),
                                  const SizedBox(width: 12),
                                  Text(
                                    '正在扫描附近蓝牙设备...',
                                    style: TextStyle(color: Colors.grey[700]),
                                  ),
                                ],
                              ),
                            ),
                          ),
                        )
                      else if (_scanResults.isNotEmpty)
                        // 有扫描结果时，用Card列表显示（动态高度）
                        Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Padding(
                                padding: const EdgeInsets.only(left: 4, bottom: 4),
                                child: Row(
                                  children: [
                                    Icon(Icons.search, size: 16, color: Colors.blue[600]),
                                    const SizedBox(width: 4),
                                    Text(
                                      '扫描到的设备 (${_scanResults.length})',
                                      style: TextStyle(
                                        fontSize: 12,
                                        fontWeight: FontWeight.w600,
                                        color: Colors.blue[600],
                                      ),
                                    ),
                                  ],
                                ),
                              ),
                              ..._scanResults.map((device) {
                                return Card(
                                  margin: const EdgeInsets.symmetric(vertical: 2),
                                  child: ListTile(
                                    dense: true,
                                    leading: const Icon(Icons.bluetooth, size: 20),
                                    title: Text(_getDeviceName(device), style: const TextStyle(fontSize: 13)),
                                    subtitle: Text(device.remoteId.str, style: const TextStyle(fontSize: 11)),
                                    trailing: const Icon(Icons.chevron_right, size: 16),
                                    onTap: () => _connectToDevice(device),
                                  ),
                                );
                              }),
                            ],
                          ),
                        ),
                      
                      // 缓存数据区域（只在未连接设备时显示）
                      if (!_isConnected)
                        Expanded(
                          flex: 2,
                          child: Column(
                            children: [
                              // 缓存数据标题
                              Padding(
                                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 2),
                                child: Row(
                                  children: [
                                    const Icon(Icons.download, size: 16, color: Colors.blue),
                                    const SizedBox(width: 4),
                                    Text(
                                      '缓存数据',
                                      style: const TextStyle(
                                        fontWeight: FontWeight.w600,
                                        fontSize: 14,
                                      ),
                                    ),
                                    const SizedBox(width: 12),
                                    // GPS过滤按钮
                                    Expanded(
                                      child: GestureDetector(
                                        onTap: () {
                                          setState(() {
                                            _filterType = _filterType == '1' ? null : '1';
                                          });
                                        },
                                        child: Container(
                                          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
                                          decoration: BoxDecoration(
                                            color: _filterType == '1' ? Colors.blue.withValues(alpha: 0.12) : Colors.grey.withValues(alpha: 0.10),
                                            borderRadius: BorderRadius.circular(12),
                                          ),
                                          child: Row(
                                            mainAxisAlignment: MainAxisAlignment.center,
                                            mainAxisSize: MainAxisSize.min,
                                            children: [
                                              Icon(
                                                Icons.gps_fixed,
                                                size: 14,
                                                color: _filterType == '1' ? Colors.blue : Colors.grey.shade700,
                                              ),
                                              const SizedBox(width: 2),
                                              Text(
                                                'GPS',
                                                style: TextStyle(
                                                  fontSize: 11,
                                                  color: _filterType == '1' ? Colors.blue : Colors.grey.shade700,
                                                  fontWeight: _filterType == '1' ? FontWeight.w600 : FontWeight.w500,
                                                ),
                                              ),
                                            ],
                                          ),
                                        ),
                                      ),
                                    ),
                                    const SizedBox(width: 4),
                                    // 对时过滤按钮
                                    Expanded(
                                      child: GestureDetector(
                                        onTap: () {
                                          setState(() {
                                            _filterType = _filterType == '2' ? null : '2';
                                          });
                                        },
                                        child: Container(
                                          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
                                          decoration: BoxDecoration(
                                            color: _filterType == '2' ? Colors.orange.withValues(alpha: 0.12) : Colors.grey.withValues(alpha: 0.10),
                                            borderRadius: BorderRadius.circular(12),
                                          ),
                                          child: Row(
                                            mainAxisAlignment: MainAxisAlignment.center,
                                            mainAxisSize: MainAxisSize.min,
                                            children: [
                                              Icon(
                                                Icons.access_time,
                                                size: 14,
                                                color: _filterType == '2' ? Colors.orange : Colors.grey.shade700,
                                              ),
                                              const SizedBox(width: 2),
                                              Text(
                                                '对时',
                                                style: TextStyle(
                                                  fontSize: 11,
                                                  color: _filterType == '2' ? Colors.orange : Colors.grey.shade700,
                                                  fontWeight: _filterType == '2' ? FontWeight.w600 : FontWeight.w500,
                                                ),
                                              ),
                                            ],
                                          ),
                                        ),
                                      ),
                                    ),
                                    const SizedBox(width: 4),
                                    // 电量过滤按钮
                                    Expanded(
                                      child: GestureDetector(
                                        onTap: () {
                                          setState(() {
                                            _filterType = _filterType == '3' ? null : '3';
                                          });
                                        },
                                        child: Container(
                                          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
                                          decoration: BoxDecoration(
                                            color: _filterType == '3' ? Colors.green.withValues(alpha: 0.12) : Colors.grey.withValues(alpha: 0.10),
                                            borderRadius: BorderRadius.circular(12),
                                          ),
                                          child: Row(
                                            mainAxisAlignment: MainAxisAlignment.center,
                                            mainAxisSize: MainAxisSize.min,
                                            children: [
                                              Icon(
                                                Icons.battery_full,
                                                size: 14,
                                                color: _filterType == '3' ? Colors.green : Colors.grey.shade700,
                                              ),
                                              const SizedBox(width: 2),
                                              Text(
                                                '电量',
                                                style: TextStyle(
                                                  fontSize: 11,
                                                  color: _filterType == '3' ? Colors.green : Colors.grey.shade700,
                                                  fontWeight: _filterType == '3' ? FontWeight.w600 : FontWeight.w500,
                                                ),
                                              ),
                                            ],
                                          ),
                                        ),
                                      ),
                                    ),
                                    const SizedBox(width: 12),
                                    if (_cachedBluetoothData.isNotEmpty)
                                      Text(
                                        '${_getFilteredCachedData().length} 条',
                                        style: const TextStyle(color: Colors.grey, fontSize: 12),
                                      ),
                                  ],
                                ),
                              ),
                              // 缓存数据列表
                              Expanded(
                                child: _cachedBluetoothData.isEmpty
                                    ? const Center(
                                        child: Column(
                                          mainAxisSize: MainAxisSize.min,
                                          children: [
                                            Icon(Icons.bluetooth, size: 64, color: Colors.grey),
                                            SizedBox(height: 16),
                                            Text(
                                              '暂无缓存数据',
                                              style: TextStyle(color: Colors.grey),
                                            ),
                                          ],
                                        ),
                                      )
                                    : _buildDataList(),
                              ),
                            ],
                          ),
                        )
                      else
                        // 已连接设备时，显示同步提示
                        Expanded(
                          flex: 2,
                          child: Center(
                            child: Column(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                Icon(Icons.sync, size: 48, color: Colors.grey[400]),
                                const SizedBox(height: 16),
                                Text(
                                  '点击「同步」开始接收数据',
                                  style: TextStyle(
                                    color: Colors.grey[600],
                                    fontSize: 14,
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                    ],
                  ),
          ),
        ],
      ),
    );
  }

  Widget _buildConnectedInfo() {
    return Column(
      children: [
        // 设备信息卡片 - 精简版
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          child: Card(
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Row(
                children: [
                  const Icon(Icons.bluetooth_connected,
                      size: 32, color: Colors.blue),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Text(
                      _connectedDevice != null
                          ? _getDeviceName(_connectedDevice!)
                          : '未知设备',
                      style: const TextStyle(
                          fontSize: 16, fontWeight: FontWeight.bold),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),

        // 接收数据区域
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Row(
            children: [
              const Icon(Icons.download, size: 16, color: Colors.blue),
              const SizedBox(width: 4),
              Text(
                '接收数据',
                style: const TextStyle(
                    fontWeight: FontWeight.w600, fontSize: 14),
              ),
              const SizedBox(width: 12),
              // GPS过滤按钮
              Expanded(
                child: GestureDetector(
                  onTap: () {
                    setState(() {
                      _filterType = _filterType == '1' ? null : '1';
                    });
                  },
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
                    decoration: BoxDecoration(
                      color: _filterType == '1' ? Colors.blue.withValues(alpha: 0.12) : Colors.grey.withValues(alpha: 0.10),
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(
                          Icons.gps_fixed,
                          size: 14,
                          color: _filterType == '1' ? Colors.blue : Colors.grey.shade700,
                        ),
                        const SizedBox(width: 2),
                        Text(
                          'GPS',
                          style: TextStyle(
                            fontSize: 11,
                            color: _filterType == '1' ? Colors.blue : Colors.grey.shade700,
                            fontWeight: _filterType == '1' ? FontWeight.w600 : FontWeight.w500,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 4),
              // 对时过滤按钮
              Expanded(
                child: GestureDetector(
                  onTap: () {
                    setState(() {
                      _filterType = _filterType == '2' ? null : '2';
                    });
                  },
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
                    decoration: BoxDecoration(
                      color: _filterType == '2' ? Colors.orange.withValues(alpha: 0.12) : Colors.grey.withValues(alpha: 0.10),
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(
                          Icons.access_time,
                          size: 14,
                          color: _filterType == '2' ? Colors.orange : Colors.grey.shade700,
                        ),
                        const SizedBox(width: 2),
                        Text(
                          '对时',
                          style: TextStyle(
                            fontSize: 11,
                            color: _filterType == '2' ? Colors.orange : Colors.grey.shade700,
                            fontWeight: _filterType == '2' ? FontWeight.w600 : FontWeight.w500,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 4),
              // 电量过滤按钮
              Expanded(
                child: GestureDetector(
                  onTap: () {
                    setState(() {
                      _filterType = _filterType == '3' ? null : '3';
                    });
                  },
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
                    decoration: BoxDecoration(
                      color: _filterType == '3' ? Colors.green.withValues(alpha: 0.12) : Colors.grey.withValues(alpha: 0.10),
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(
                          Icons.battery_full,
                          size: 14,
                          color: _filterType == '3' ? Colors.green : Colors.grey.shade700,
                        ),
                        const SizedBox(width: 2),
                        Text(
                          '电量',
                          style: TextStyle(
                            fontSize: 11,
                            color: _filterType == '3' ? Colors.green : Colors.grey.shade700,
                            fontWeight: _filterType == '3' ? FontWeight.w600 : FontWeight.w500,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 12),
              if (_receivedData.isNotEmpty)
                Text(
                  '${_getFilteredReceivedData().length} 条',
                  style: const TextStyle(color: Colors.grey, fontSize: 12),
                ),
            ],
          ),
        ),
        const SizedBox(height: 4),

        Expanded(
          child: _buildDataList(),
        ),
      ],
    );
  }
  

}