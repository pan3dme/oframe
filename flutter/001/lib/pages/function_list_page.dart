import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'bluetooth_page.dart';
import 'device_manage_page.dart';
import 'livestock_manage_page.dart';

/// FC 地址常量
const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

class FunctionListPage extends StatefulWidget {
  const FunctionListPage({super.key});

  @override
  State<FunctionListPage> createState() => _FunctionListPageState();
}

class _FunctionListPageState extends State<FunctionListPage> {

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('功能列表'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: Column(
        children: [
          // 上部分：6个功能按钮（两行三列）
          Container(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                // 第一行：最近10条记录、上报设备LORA、设备管理
                Row(
                  children: [
                    _buildFunctionButton(
                      context,
                      icon: Icons.history,
                      label: '最近10条记录',
                      color: Colors.blue,
                      onTap: () {
                        ScaffoldMessenger.of(context).showSnackBar(
                          const SnackBar(content: Text('功能开发中...')),
                        );
                      },
                    ),
                    const SizedBox(width: 12),
                    _buildFunctionButton(
                      context,
                      icon: Icons.upload,
                      label: '上报设备LORA',
                      color: Colors.green,
                      onTap: () {
                        _showLoraReportDialog(context);
                      },
                    ),
                    const SizedBox(width: 12),
                    _buildFunctionButton(
                      context,
                      icon: Icons.devices,
                      label: '设备管理',
                      color: Colors.purple,
                      onTap: () {
                        Navigator.push(
                          context,
                          MaterialPageRoute(
                            builder: (context) => const DeviceManagePage(),
                          ),
                        );
                      },
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                // 第二行：牛羊管理、查看设备轨迹、连接蓝牙
                Row(
                  children: [
                    _buildFunctionButton(
                      context,
                      icon: Icons.pets,
                      label: '牛羊管理',
                      color: Colors.teal,
                      onTap: () {
                        Navigator.push(
                          context,
                          MaterialPageRoute(
                            builder: (context) => const LivestockManagePage(),
                          ),
                        );
                      },
                    ),
                    const SizedBox(width: 12),
                    _buildFunctionButton(
                      context,
                      icon: Icons.route,
                      label: '查看设备轨迹',
                      color: Colors.red,
                      onTap: () {
                        ScaffoldMessenger.of(context).showSnackBar(
                          const SnackBar(content: Text('功能开发中...')),
                        );
                      },
                    ),
                    const SizedBox(width: 12),
                    _buildFunctionButton(
                      context,
                      icon: Icons.bluetooth,
                      label: '连接蓝牙',
                      color: Colors.indigo,
                      onTap: () {
                        Navigator.push(
                          context,
                          MaterialPageRoute(
                            builder: (context) => const BluetoothPage(),
                          ),
                        );
                      },
                    ),
                  ],
                ),
              ],
            ),
          ),
          // 下部分：显示其他信息的区域
          Expanded(
            child: Container(
              margin: const EdgeInsets.fromLTRB(16, 0, 16, 16),
              decoration: BoxDecoration(
                color: Colors.grey.shade100,
                borderRadius: BorderRadius.circular(12),
                border: Border.all(color: Colors.grey.shade300),
              ),
              child: const Center(
                child: Text(
                  '其他信息区域',
                  style: TextStyle(
                    fontSize: 16,
                    color: Colors.grey,
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  /// 显示 LORA 上报对话框
  void _showLoraReportDialog(BuildContext context) {
    final deviceIdController = TextEditingController(text: 'v4-1');
    final loraDataController = TextEditingController(
      text: '1|v3-1|26.530033, 109.390391|flutter',
    );

    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('上报设备LORA'),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              TextField(
                controller: deviceIdController,
                decoration: const InputDecoration(
                  labelText: '设备ID',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.devices),
                ),
              ),
              const SizedBox(height: 16),
              TextField(
                controller: loraDataController,
                decoration: const InputDecoration(
                  labelText: 'LORA数据',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.data_usage),
                ),
                maxLines: 3,
              ),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('取消'),
          ),
          ElevatedButton(
            onPressed: () {
              _reportLoraData(
                deviceIdController.text,
                loraDataController.text,
              );
              Navigator.pop(context);
            },
            child: const Text('上报'),
          ),
        ],
      ),
    );
  }

  /// 上报 LORA 数据到 FC
  Future<void> _reportLoraData(String deviceId, String loraData) async {
    debugPrint('上报LORA: deviceId=$deviceId, loraData=$loraData');

    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'insertlog',
          'info': {
            'deviceId': deviceId,
            'lorastr': loraData,
            'upDateDevice': 'FLUTTER',
            'time': DateTime.now().toIso8601String(),
          },
        }),
      );


      debugPrint('上报响应: ${resp.statusCode} - ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          debugPrint('上报成功');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(
                content: Text('上报成功'),
                backgroundColor: Colors.green,
              ),
            );
          }
        } else {
          debugPrint('上报失败: ${json['msg']}');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text('上报失败: ${json['msg']}'),
                backgroundColor: Colors.red,
              ),
            );
          }
        }
      } else {
        debugPrint('HTTP错误: ${resp.statusCode}');
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text('HTTP错误: ${resp.statusCode}'),
              backgroundColor: Colors.red,
            ),
          );
        }
      }
    } catch (e) {
      debugPrint('上报LORA失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('上报失败: $e'),
            backgroundColor: Colors.red,
          ),
        );
      }
    }
  }

  /// 构建功能按钮
  Widget _buildFunctionButton(
    BuildContext context, {
    required IconData icon,
    required String label,
    required Color color,
    required VoidCallback onTap,
  }) {
    return Expanded(
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Container(
          padding: const EdgeInsets.symmetric(vertical: 20, horizontal: 8),
          decoration: BoxDecoration(
            color: color.withOpacity(0.1),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: color.withOpacity(0.3), width: 1.5),
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon, size: 36, color: color),
              const SizedBox(height: 8),
              Text(
                label,
                textAlign: TextAlign.center,
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.bold,
                  color: color,
                ),
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
            ],
          ),
        ),
      ),
    );
  }
}