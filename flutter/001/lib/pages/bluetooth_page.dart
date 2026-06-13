import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:http/http.dart' as http;
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
  BluetoothDevice? _connectedDevice;
  BluetoothConnectionState _connectionState =
      BluetoothConnectionState.disconnected;
  StreamSubscription<BluetoothConnectionState>? _connectionStateSubscription;
  StreamSubscription<List<ScanResult>>? _scanResultSubscription;

  // 同步状态
  bool _isSyncing = false;
  BluetoothCharacteristic? _writeCharacteristic;
  BluetoothCharacteristic? _notifyCharacteristic;
  StreamSubscription<List<int>>? _notifySubscription;

  // 接收到的数据
  final List<String> _receivedData = [];
  List<Map<String, dynamic>> _cachedBluetoothData = []; // 缓存的蓝牙数据
  bool _isUploading = false; // 是否正在上传
  bool _uploadPaused = false; // 上传是否因断网暂停

  @override
  void initState() {
    super.initState();
    _loadCachedBluetoothData();
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

  /// 上传缓存数据到数据中心
  Future<void> _uploadCachedData() async {
    if (_cachedBluetoothData.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('没有缓存数据可上传'),
          backgroundColor: Colors.orange,
        ),
      );
      return;
    }

    setState(() {
      _isUploading = true;
      _uploadPaused = false;
    });

    // 逐条上传，确保上次成功后再上报下一个，间隔1秒
    while (_cachedBluetoothData.isNotEmpty && _isUploading) {
      final item = _cachedBluetoothData.first;
      try {
        // 解析蓝牙数据，提取deviceId和lorastr
        final dataStr = item['data'] ?? '';
        final dataId = item['id'] as int; // 获取数据库ID

        print('[蓝牙上传] 原始数据: $dataStr');
        
        String deviceId = '';
        String lorastr = '';
        String upDateDevice = '';
        String time = item['time'] ?? formatTime(DateTime.now());
        
        try {
          // dataStr 是JSON字符串: {"info":"1|v4-3|26.52956,109.39073|368","upDateDevice":"v4-1","time":"2026/6/12 13:12:44"}
          final jsonData = jsonDecode(dataStr) as Map<String, dynamic>;
          
          // 提取info字段
          lorastr = jsonData['info'] ?? ''; // "1|v4-3|26.52956,109.39073|368"
          upDateDevice = jsonData['upDateDevice'] ?? ''; // "v4-1"
          time = jsonData['time'] ?? time; // "2026/6/12 13:12:44"
          
          // 从info中提取deviceId（第2部分）
          if (lorastr.contains('|')) {
            final parts = lorastr.split('|');
            if (parts.length >= 2) {
              deviceId = parts[1]; // v4-3
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
            print('[蓝牙上传] 成功上传: deviceId=$deviceId, data=$dataStr');
            // 上传成功后只删除该条数据
            await DBHelper().deleteBluetoothDataById(dataId);
            // 重新加载缓存
            await _loadCachedBluetoothData();
            
            // 上传成功后立即继续下一条，不延迟
          } else {
            print('[蓝牙上传] 上传失败: ${json['msg']}');
            // 服务端返回失败，停止上传
            setState(() {
              _uploadPaused = true;
              _isUploading = false;
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
            _uploadPaused = true;
            _isUploading = false;
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
          _uploadPaused = true;
          _isUploading = false;
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
        break; // 断网就停止
      }
    }

    if (_isUploading && mounted) {
      setState(() {
        _isUploading = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('上传完成'),
          backgroundColor: Colors.green,
        ),
      );
    }
  }

  /// 刷新缓存计数
  Future<void> _refreshCacheCount() async {
    await _loadCachedBluetoothData();
  }

  @override
  void dispose() {
    // 取消上传
    if (_isUploading) {
      _isUploading = false;
      print('[蓝牙] 页面退出，取消上传');
    }
    
    _connectionStateSubscription?.cancel();
    _scanResultSubscription?.cancel();
    _notifySubscription?.cancel();
    FlutterBluePlus.stopScan();
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
            _notifyCharacteristic!.onValueReceived.listen((value) async {
          final data = utf8.decode(value, allowMalformed: true);
          setState(() {
            _receivedData.add(data);
          });
          
          // 保存到本地数据库
          final deviceName = _connectedDevice != null ? _getDeviceName(_connectedDevice!) : '未知设备';
          final deviceId = _connectedDevice?.remoteId.str ?? '';
          final time = formatTime(DateTime.now());
          await DBHelper().saveBluetoothData(deviceName, deviceId, data, time);
          
          // 刷新缓存记录数
          await _loadCachedBluetoothData();
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
        await _connectedDevice!.disconnect();
        _connectionStateSubscription?.cancel();
        _connectionStateSubscription = null;
        setState(() {
          _connectedDevice = null;
          _connectionState = BluetoothConnectionState.disconnected;
          _writeCharacteristic = null;
          _notifyCharacteristic = null;
          _receivedData.clear();
        });
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('已断开连接')),
          );
        }
      }
    } catch (e) {
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
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
            child: Card(
              child: Padding(
                padding: const EdgeInsets.all(12),
                child: Row(
                  children: [
                    const Icon(Icons.cloud_upload, size: 24, color: Colors.blue),
                    const SizedBox(width: 12),
                    Expanded(
                      child: Text(
                        '缓存记录: ${_cachedBluetoothData.length} 条',
                        style: const TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w500,
                        ),
                      ),
                    ),
                    ElevatedButton.icon(
                      onPressed: _isUploading || _cachedBluetoothData.isEmpty
                          ? null
                          : _uploadCachedData,
                      icon: _isUploading
                          ? const SizedBox(
                              width: 16,
                              height: 16,
                              child: CircularProgressIndicator(
                                strokeWidth: 2,
                                color: Colors.white,
                              ),
                            )
                          : const Icon(Icons.upload, size: 18),
                      label: Text(_isUploading ? '上传中...' : '上传数据中心'),
                      style: ElevatedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 12,
                          vertical: 8,
                        ),
                        backgroundColor: _uploadPaused ? Colors.orange : null,
                        disabledBackgroundColor: Colors.grey.withValues(alpha: 0.3),
                      ),
                    ),
                  ],
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
                          backgroundColor: _isConnected ? Colors.red : null,
                          foregroundColor: _isConnected ? Colors.white : null,
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
                          backgroundColor: _isSyncing ? Colors.orange : null,
                          foregroundColor: _isSyncing ? Colors.white : null,
                          disabledBackgroundColor:
                              Colors.grey.withValues(alpha: 0.3),
                          disabledForegroundColor: Colors.grey,
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
                : (_scanResults.isEmpty && !_isScanning
                    ? const Center(
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Icon(Icons.bluetooth,
                                size: 64, color: Colors.grey),
                            SizedBox(height: 16),
                            Text(
                              '点击「连接蓝牙」搜索附近设备',
                              style: TextStyle(color: Colors.grey),
                            ),
                            SizedBox(height: 4),
                            Text(
                              '仅显示名称包含「牛羊」的设备',
                              style:
                                  TextStyle(color: Colors.grey, fontSize: 12),
                            ),
                          ],
                        ),
                      )
                    : ListView.builder(
                        itemCount: _scanResults.length,
                        itemBuilder: (context, index) {
                          final device = _scanResults[index];
                          return ListTile(
                            leading: const Icon(Icons.bluetooth),
                            title: Text(_getDeviceName(device)),
                            subtitle: Text(device.remoteId.str),
                            trailing: const Icon(Icons.chevron_right),
                            onTap: () => _connectToDevice(device),
                          );
                        },
                      )),
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
              const Spacer(),
              if (_receivedData.isNotEmpty)
                Text(
                  '${_receivedData.length} 条',
                  style: const TextStyle(color: Colors.grey, fontSize: 12),
                ),
            ],
          ),
        ),
        const SizedBox(height: 4),

        Expanded(
          child: _receivedData.isEmpty
              ? const Center(
                  child: Text(
                    '暂无接收数据\n点击「同步」开始接收',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: Colors.grey),
                  ),
                )
              : ListView.builder(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  itemCount: _receivedData.length,
                  itemBuilder: (context, index) {
                    return Card(
                      margin: const EdgeInsets.symmetric(vertical: 2),
                      child: Padding(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 12, vertical: 8),
                        child: Text(
                          _receivedData[index],
                          style: const TextStyle(fontSize: 13),
                        ),
                      ),
                    );
                  },
                ),
        ),
      ],
    );
  }
}