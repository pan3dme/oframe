import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

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

  @override
  void dispose() {
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
        setState(() {
          _scanResults.clear();
          for (final r in results) {
            final name = r.device.platformName;
            // 只显示名称包含「牛羊」的设备
            if (name.contains('牛羊') &&
                !_scanResults.any((d) => d.remoteId == r.device.remoteId)) {
              _scanResults.add(r.device);
            }
          }
        });
      });

      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('扫描失败: $e')),
        );
      }
    } finally {
      setState(() {
        _isScanning = false;
      });
    }
  }

  // ---- 连接 ----
  Future<void> _connectToDevice(BluetoothDevice device) async {
    try {
      await FlutterBluePlus.stopScan();
      setState(() {
        _isScanning = false;
      });

      await device.connect(timeout: const Duration(seconds: 15));

      _connectionStateSubscription = device.connectionState.listen((state) {
        setState(() {
          _connectionState = state;
        });
        if (state == BluetoothConnectionState.connected) {
          _connectedDevice = device;
          _discoverServices(device);
        } else if (state == BluetoothConnectionState.disconnected) {
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
          SnackBar(content: Text('已连接: ${_getDeviceName(device)}')),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('连接失败: $e')),
        );
      }
    }
  }

  // ---- 发现服务和特征值 ----
  Future<void> _discoverServices(BluetoothDevice device) async {
    try {
      final services = await device.discoverServices();
      BluetoothCharacteristic? writeChar;
      BluetoothCharacteristic? notifyChar;
      for (final service in services) {
        for (final characteristic in service.characteristics) {
          if (characteristic.properties.write && writeChar == null) {
            writeChar = characteristic;
          }
          if (characteristic.properties.notify && notifyChar == null) {
            notifyChar = characteristic;
          }
        }
      }
      setState(() {
        _writeCharacteristic = writeChar;
        _notifyCharacteristic = notifyChar;
      });
      if (writeChar == null && mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('该设备没有可写的特征值，无法同步')),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('发现服务失败: $e')),
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
        'time': DateTime.now().toIso8601String(),
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
          setState(() {
            _receivedData.add(data);
          });
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
        'time': DateTime.now().toIso8601String(),
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
          'time': DateTime.now().toIso8601String(),
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
        title: const Text('连接蓝牙'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: Column(
        children: [
          // 连接状态
          _buildConnectionStatus(),

          // 操作按钮
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
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

  Widget _buildConnectionStatus() {
    Color color;
    String text;
    IconData icon;

    if (_isConnected) {
      color = Colors.green;
      final devName =
          _connectedDevice != null ? _getDeviceName(_connectedDevice!) : '';
      text = '已连接: $devName';
      icon = Icons.bluetooth_connected;
    } else {
      color = Colors.grey;
      text = '未连接';
      icon = Icons.bluetooth_disabled;
    }

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
      color: color.withValues(alpha: 0.1),
      child: Row(
        children: [
          Icon(icon, color: color, size: 20),
          const SizedBox(width: 8),
          Text(
            text,
            style: TextStyle(color: color, fontWeight: FontWeight.w500),
          ),
          if (_isSyncing) ...[
            const SizedBox(width: 12),
            Container(
              padding:
                  const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
              decoration: BoxDecoration(
                color: Colors.orange.withValues(alpha: 0.2),
                borderRadius: BorderRadius.circular(12),
              ),
              child: const Text(
                '同步中',
                style: TextStyle(
                    color: Colors.orange, fontWeight: FontWeight.w600),
              ),
            ),
          ],
        ],
      ),
    );
  }

  Widget _buildConnectedInfo() {
    return Column(
      children: [
        // 设备信息卡片
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          child: Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Row(
                children: [
                  const Icon(Icons.bluetooth_connected,
                      size: 48, color: Colors.blue),
                  const SizedBox(width: 16),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          _connectedDevice != null
                              ? _getDeviceName(_connectedDevice!)
                              : '未知设备',
                          style: const TextStyle(
                              fontSize: 18, fontWeight: FontWeight.bold),
                        ),
                        const SizedBox(height: 4),
                        Text(
                          _connectedDevice?.remoteId.str ?? '',
                          style:
                              const TextStyle(color: Colors.grey, fontSize: 13),
                        ),
                        const SizedBox(height: 4),
                        if (_writeCharacteristic != null)
                          Text(
                            '写: ${_writeCharacteristic!.characteristicUuid.str}',
                            style:
                                const TextStyle(fontSize: 11, color: Colors.grey),
                          )
                        else
                          const Text(
                            '无可写特征值',
                            style: TextStyle(fontSize: 11, color: Colors.red),
                          ),
                        if (_notifyCharacteristic != null)
                          Text(
                            '读: ${_notifyCharacteristic!.characteristicUuid.str}',
                            style:
                                const TextStyle(fontSize: 11, color: Colors.grey),
                          ),
                      ],
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