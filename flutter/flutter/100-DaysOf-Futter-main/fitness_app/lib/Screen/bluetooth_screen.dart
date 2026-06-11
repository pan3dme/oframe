import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class BluetoothScreen extends StatefulWidget {
  const BluetoothScreen({super.key});

  @override
  State<BluetoothScreen> createState() => _BluetoothScreenState();
}

class _BluetoothScreenState extends State<BluetoothScreen> {
  List<ScanResult> _scanResults = [];
  bool _isScanning = false;
  BluetoothDevice? _connectedDevice;
  StreamSubscription<List<ScanResult>>? _scanSubscription;
  StreamSubscription<BluetoothAdapterState>? _adapterStateSubscription;
  BluetoothAdapterState _adapterState = BluetoothAdapterState.unknown;

  @override
  void initState() {
    super.initState();
    _initBluetooth();
  }

  @override
  void dispose() {
    _scanSubscription?.cancel();
    _adapterStateSubscription?.cancel();
    super.dispose();
  }

  Future<void> _initBluetooth() async {
    // 监听蓝牙适配器状态
    _adapterStateSubscription = FlutterBluePlus.adapterState.listen((state) {
      setState(() {
        _adapterState = state;
      });
      
      if (state == BluetoothAdapterState.on) {
        print('蓝牙已开启');
      } else {
        print('蓝牙状态: $state');
      }
    });

    // 获取当前蓝牙状态
    final currentState = await FlutterBluePlus.adapterState.first;
    setState(() {
      _adapterState = currentState;
    });
  }

  // 开始扫描
  Future<void> _startScan() async {
    if (_isScanning) return;

    // 检查蓝牙是否开启
    if (_adapterState != BluetoothAdapterState.on) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('请先开启蓝牙'),
          backgroundColor: Colors.red,
        ),
      );
      return;
    }

    setState(() {
      _isScanning = true;
      _scanResults.clear();
    });

    try {
      // 开始扫描
      _scanSubscription = FlutterBluePlus.scanResults.listen((results) {
        setState(() {
          _scanResults = results;
        });
      }, onError: (e) {
        print('扫描错误: $e');
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('扫描失败: $e'),
            backgroundColor: Colors.red,
          ),
        );
      });

      await FlutterBluePlus.startScan(
        timeout: const Duration(seconds: 10),
      );

      // 10秒后停止扫描
      Future.delayed(const Duration(seconds: 10), () {
        setState(() {
          _isScanning = false;
        });
      });
    } catch (e) {
      setState(() {
        _isScanning = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('启动扫描失败: $e'),
          backgroundColor: Colors.red,
        ),
      );
    }
  }

  // 停止扫描
  Future<void> _stopScan() async {
    await FlutterBluePlus.stopScan();
    setState(() {
      _isScanning = false;
    });
  }

  // 连接设备
  Future<void> _connectToDevice(BluetoothDevice device) async {
    try {
      setState(() {
        _connectedDevice = device;
      });

      // 连接到设备
      await device.connect();
      
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('已连接到: ${device.advName}'),
          backgroundColor: Colors.green,
        ),
      );

      print('成功连接到设备: ${device.name}');
    } catch (e) {
      setState(() {
        _connectedDevice = null;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('连接失败: $e'),
          backgroundColor: Colors.red,
        ),
      );
      print('连接失败: $e');
    }
  }

  // 断开设备
  Future<void> _disconnectDevice() async {
    if (_connectedDevice != null) {
      try {
        await _connectedDevice!.disconnect();
        setState(() {
          _connectedDevice = null;
        });
        
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('已断开连接'),
            backgroundColor: Colors.blue,
          ),
        );
        
        print('已断开设备连接');
      } catch (e) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('断开失败: $e'),
            backgroundColor: Colors.red,
          ),
        );
        print('断开失败: $e');
      }
    }
  }

  // 请求蓝牙权限（Android）
  Future<void> _requestPermissions() async {
    // flutter_blue_plus 会自动处理权限请求
    // 但可以在这里添加额外的权限检查
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('蓝牙管理'),
        centerTitle: true,
        elevation: 0,
      ),
      body: Column(
        children: [
          // 顶部按钮区域
          Container(
            padding: const EdgeInsets.all(16),
            child: Row(
              children: [
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _isScanning ? null : _startScan,
                    icon: const Icon(Icons.bluetooth_searching),
                    label: const Text('连接蓝牙'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.blue,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(12),
                      ),
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _connectedDevice != null ? _disconnectDevice : null,
                    icon: const Icon(Icons.bluetooth_disabled),
                    label: const Text('断开蓝牙'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.red,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(12),
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),

          // 蓝牙状态提示
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Row(
              children: [
                Icon(
                  _adapterState == BluetoothAdapterState.on
                      ? Icons.bluetooth_connected
                      : Icons.bluetooth_disabled,
                  color: _adapterState == BluetoothAdapterState.on
                      ? Colors.green
                      : Colors.grey,
                  size: 20,
                ),
                const SizedBox(width: 8),
                Text(
                  '蓝牙状态: ${_getAdapterStateText(_adapterState)}',
                  style: TextStyle(
                    color: _adapterState == BluetoothAdapterState.on
                        ? Colors.green
                        : Colors.grey,
                    fontWeight: FontWeight.w500,
                  ),
                ),
              ],
            ),
          ),

          const Divider(height: 20),

          // 已连接的设备
          if (_connectedDevice != null)
            Container(
              margin: const EdgeInsets.all(16),
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: Colors.green.withOpacity(0.1),
                borderRadius: BorderRadius.circular(12),
                border: Border.all(color: Colors.green.withOpacity(0.3)),
              ),
              child: Row(
                children: [
                  const Icon(Icons.check_circle, color: Colors.green),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Text(
                          '已连接设备',
                          style: TextStyle(
                            fontSize: 12,
                            color: Colors.grey,
                          ),
                        ),
                        const SizedBox(height: 4),
                        Text(
                          _connectedDevice!.advName.isNotEmpty
                              ? _connectedDevice!.advName
                              : _connectedDevice!.name,
                          style: const TextStyle(
                            fontSize: 16,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),

          // 扫描状态
          Padding(
            padding: const EdgeInsets.all(16),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text(
                  _isScanning ? '正在搜索设备...' : '附近的蓝牙设备',
                  style: const TextStyle(
                    fontSize: 18,
                    fontWeight: FontWeight.bold,
                  ),
                ),
                if (_isScanning)
                  const SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  ),
              ],
            ),
          ),

          // 设备列表
          Expanded(
            child: _scanResults.isEmpty && !_isScanning
                ? Center(
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Icon(
                          Icons.bluetooth_searching,
                          size: 80,
                          color: Colors.grey.withOpacity(0.3),
                        ),
                        const SizedBox(height: 16),
                        const Text(
                          '点击"连接蓝牙"开始搜索设备',
                          style: TextStyle(
                            color: Colors.grey,
                            fontSize: 16,
                          ),
                        ),
                      ],
                    ),
                  )
                : ListView.builder(
                    padding: const EdgeInsets.symmetric(horizontal: 16),
                    itemCount: _scanResults.length,
                    itemBuilder: (context, index) {
                      final result = _scanResults[index];
                      final device = result.device;
                      
                      return Card(
                        margin: const EdgeInsets.only(bottom: 12),
                        elevation: 2,
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(12),
                        ),
                        child: ListTile(
                          leading: Container(
                            width: 50,
                            height: 50,
                            decoration: BoxDecoration(
                              color: Colors.blue.withOpacity(0.1),
                              borderRadius: BorderRadius.circular(10),
                            ),
                            child: Icon(
                              _connectedDevice?.remoteId == device.remoteId
                                  ? Icons.check_circle
                                  : Icons.bluetooth,
                              color: _connectedDevice?.remoteId == device.remoteId
                                  ? Colors.green
                                  : Colors.blue,
                            ),
                          ),
                          title: Text(
                            device.advName.isNotEmpty
                                ? device.advName
                                : '未知设备',
                            style: const TextStyle(
                              fontWeight: FontWeight.bold,
                              fontSize: 16,
                            ),
                          ),
                          subtitle: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              const SizedBox(height: 4),
                              Text(
                                'MAC: ${device.remoteId}',
                                style: const TextStyle(
                                  fontSize: 12,
                                  color: Colors.grey,
                                ),
                              ),
                              const SizedBox(height: 2),
                              Text(
                                '信号强度: ${result.rssi} dBm',
                                style: TextStyle(
                                  fontSize: 12,
                                  color: _getSignalStrengthColor(result.rssi),
                                ),
                              ),
                            ],
                          ),
                          trailing: ElevatedButton(
                            onPressed: _connectedDevice?.remoteId == device.remoteId
                                ? null
                                : () => _connectToDevice(device),
                            child: Text(
                              _connectedDevice?.remoteId == device.remoteId
                                  ? '已连接'
                                  : '连接',
                            ),
                          ),
                        ),
                      );
                    },
                  ),
          ),
        ],
      ),
    );
  }

  String _getAdapterStateText(BluetoothAdapterState state) {
    switch (state) {
      case BluetoothAdapterState.on:
        return '已开启';
      case BluetoothAdapterState.off:
        return '已关闭';
      case BluetoothAdapterState.turningOn:
        return '正在开启...';
      case BluetoothAdapterState.turningOff:
        return '正在关闭...';
      default:
        return '未知状态';
    }
  }

  Color _getSignalStrengthColor(int rssi) {
    if (rssi >= -60) {
      return Colors.green;
    } else if (rssi >= -75) {
      return Colors.orange;
    } else {
      return Colors.red;
    }
  }
}
