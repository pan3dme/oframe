import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'bluetooth_page.dart';
import 'device_manage_page.dart';
import 'livestock_manage_page.dart';
import 'settings_page.dart';
import '../utils/db_helper.dart';

/// FC 地址常量
const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

/// 格式化时间为：2026/6/12 12:21:10
String formatTime(DateTime dateTime) {
  return '${dateTime.year}/${dateTime.month}/${dateTime.day} ${dateTime.hour}:${dateTime.minute.toString().padLeft(2, '0')}:${dateTime.second.toString().padLeft(2, '0')}';
}

class FunctionListPage extends StatefulWidget {
  const FunctionListPage({super.key});

  @override
  State<FunctionListPage> createState() => _FunctionListPageState();
}

class _FunctionListPageState extends State<FunctionListPage> {
  // 日志相关状态
  List<Map<String, dynamic>> _logs = [];
  bool _isLoadingLogs = false;
  String _logErrorMessage = '';

  @override
  void initState() {
    super.initState();
    _loadLogs();
  }

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
                        _loadLogs();
                      },
                    ),
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
                // 第二行：牛羊管理、设置、连接蓝牙
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
                      icon: Icons.settings,
                      label: '设置',
                      color: Colors.orange,
                      onTap: () {
                        Navigator.push(
                          context,
                          MaterialPageRoute(
                            builder: (context) => const SettingsPage(),
                          ),
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
          // 下部分：显示日志记录
          Expanded(
            child: Container(
              margin: const EdgeInsets.fromLTRB(16, 0, 16, 16),
              decoration: BoxDecoration(
                color: Colors.grey.shade100,
                borderRadius: BorderRadius.circular(12),
                border: Border.all(color: Colors.grey.shade300),
              ),
              child: _buildLogContent(),
            ),
          ),
        ],
      ),
    );
  }

  /// 加载所有设备的最近10条日志记录
  Future<void> _loadLogs() async {
    // 直接从网络获取最新数据，不使用缓存
    await _loadLogsFromNetwork();
  }

  /// 从网络加载日志
  Future<void> _loadLogsFromNetwork() async {
    try {
      debugPrint('请求最近10条日志记录');

      setState(() {
        _isLoadingLogs = true;
        _logErrorMessage = '';
      });

      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getlastlog',
        }),
      );

      debugPrint('日志响应状态: ${resp.statusCode}');
      debugPrint('日志响应体: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;

        if (json['status'] == 'success') {
          final data = json['data'];

          if (data is List) {
            // 解析 TableStore 返回的数据结构
            final parsedLogs = data.map((item) {
              if (item is Map<String, dynamic>) {
                final parsedItem = <String, dynamic>{};
                
                // 解析主键
                final primaryKey = item['primaryKey'] as List?;
                if (primaryKey != null) {
                  for (var pk in primaryKey) {
                    if (pk is Map<String, dynamic>) {
                      final name = pk['name']?.toString() ?? '';
                      final value = pk['value']?.toString() ?? '';
                      parsedItem[name] = value;
                    }
                  }
                }
                
                // 解析属性列
                final attributes = item['attributes'] as List?;
                if (attributes != null) {
                  for (var attr in attributes) {
                    if (attr is Map<String, dynamic>) {
                      final columnName = attr['columnName']?.toString() ?? '';
                      final columnValue = attr['columnValue'];
                      parsedItem[columnName] = columnValue;
                    }
                  }
                }
                
                return parsedItem;
              }
              return <String, dynamic>{};
            }).toList();
            
            setState(() {
              _logs = parsedLogs;
              _isLoadingLogs = false;
              _logErrorMessage = '';
            });
            debugPrint('从网络加载日志数据: ${parsedLogs.length} 条');
          } else {
            setState(() {
              _logErrorMessage = '数据格式错误';
              _isLoadingLogs = false;
            });
          }
        } else {
          setState(() {
            _logErrorMessage = json['msg'] ?? '加载失败';
            _isLoadingLogs = false;
          });
        }
      } else {
        setState(() {
          _logErrorMessage = 'HTTP错误: ${resp.statusCode}';
          _isLoadingLogs = false;
        });
      }
    } catch (e) {
      debugPrint('加载日志失败: $e');
      setState(() {
        _logErrorMessage = '加载失败: $e';
        _isLoadingLogs = false;
      });
    }
  }

  /// 构建日志内容区域
  Widget _buildLogContent() {
    // 加载中状态
    if (_isLoadingLogs) {
      return const Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            CircularProgressIndicator(),
            SizedBox(height: 16),
            Text('加载日志记录中...'),
          ],
        ),
      );
    }

    // 错误状态
    if (_logErrorMessage.isNotEmpty) {
      return Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(
              Icons.error_outline,
              size: 48,
              color: Colors.red,
            ),
            const SizedBox(height: 16),
            Text(
              _logErrorMessage,
              style: const TextStyle(
                color: Colors.red,
              ),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 16),
            ElevatedButton.icon(
              onPressed: _loadLogs,
              icon: const Icon(Icons.refresh),
              label: const Text('刷新'),
            ),
          ],
        ),
      );
    }

    // 空状态
    if (_logs.isEmpty) {
      return const Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(
              Icons.inbox,
              size: 48,
              color: Colors.grey,
            ),
            SizedBox(height: 16),
            Text(
              '暂无日志记录',
              style: TextStyle(color: Colors.grey),
            ),
          ],
        ),
      );
    }

    // 日志列表
    return RefreshIndicator(
      onRefresh: _loadLogs,
      child: ListView.builder(
        padding: const EdgeInsets.all(12),
        itemCount: _logs.length,
        itemBuilder: (context, index) {
          final log = _logs[index];
          return _buildLogCard(log, index);
        },
      ),
    );
  }

  /// 构建日志卡片
  Widget _buildLogCard(Map<String, dynamic> log, int index) {
    final deviceId = log['deviceId']?.toString() ?? '—';
    final lorastr = log['lorastr']?.toString() ?? '—';
    final time = log['time']?.toString() ?? '—';
    final upDateDevice = log['upDateDevice']?.toString() ?? '—';
    final picurl = log['picurl']?.toString() ?? '';
    final relativeTime = _getRelativeTime(time);

    return Card(
      margin: const EdgeInsets.only(bottom: 12),
      elevation: 2,
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 序号、设备ID和时间
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Row(
                  children: [
                    Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 10,
                        vertical: 4,
                      ),
                      decoration: BoxDecoration(
                        color: Colors.blue.withOpacity(0.1),
                        borderRadius: BorderRadius.circular(16),
                      ),
                      child: Text(
                        '#${index + 1}',
                        style: const TextStyle(
                          fontWeight: FontWeight.bold,
                          color: Colors.blue,
                          fontSize: 12,
                        ),
                      ),
                    ),
                    const SizedBox(width: 8),
                    Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 10,
                        vertical: 4,
                      ),
                      decoration: BoxDecoration(
                        color: Colors.purple.withOpacity(0.1),
                        borderRadius: BorderRadius.circular(16),
                      ),
                      child: Text(
                        deviceId,
                        style: const TextStyle(
                          fontWeight: FontWeight.bold,
                          color: Colors.purple,
                          fontSize: 12,
                        ),
                      ),
                    ),
                  ],
                ),
                Row(
                  children: [
                    Text(
                      time,
                      style: TextStyle(
                        fontSize: 11,
                        color: Colors.grey.shade600,
                      ),
                    ),
                    // 如果时间不超过1年，才显示相对时间
                    if (_shouldShowRelativeTime(time)) ...[
                      const Text(' (', style: TextStyle(fontSize: 11, color: Colors.grey)),
                      Text(
                        relativeTime,
                        style: const TextStyle(
                          fontSize: 11,
                          fontWeight: FontWeight.bold,
                          color: Colors.orange,
                        ),
                      ),
                      const Text(')', style: TextStyle(fontSize: 11, color: Colors.grey)),
                    ],
                  ],
                ),
              ],
            ),
            const SizedBox(height: 8),

            // LORA 数据
            const Text(
              'LORA 数据:',
              style: TextStyle(
                fontWeight: FontWeight.bold,
                fontSize: 12,
                color: Colors.grey,
              ),
            ),
            const SizedBox(height: 4),
            Text(
              lorastr,
              style: const TextStyle(fontSize: 13),
            ),

            // 图片(如果有)
            if (picurl.isNotEmpty && picurl != '—') ...[
              const SizedBox(height: 8),
              ClipRRect(
                borderRadius: BorderRadius.circular(6),
                child: Image.network(
                  picurl,
                  height: 120,
                  width: double.infinity,
                  fit: BoxFit.cover,
                  errorBuilder: (context, error, stackTrace) {
                    return Container(
                      height: 120,
                      color: Colors.grey.shade200,
                      child: const Center(
                        child: Text(
                          '图片加载失败',
                          style: TextStyle(color: Colors.grey, fontSize: 12),
                        ),
                      ),
                    );
                  },
                ),
              ),
            ],

            const SizedBox(height: 8),

            // 更新设备
            Row(
              children: [
                const Icon(Icons.device_hub, size: 14, color: Colors.grey),
                const SizedBox(width: 4),
                Text(
                  '更新来源: $upDateDevice',
                  style: TextStyle(
                    fontSize: 11,
                    color: Colors.grey.shade600,
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  /// 判断是否应该显示相对时间（不超过1年）
  bool _shouldShowRelativeTime(String timeStr) {
    try {
      final parts = timeStr.split(' ');
      if (parts.length != 2) return false;
      
      final dateParts = parts[0].split('/');
      if (dateParts.length != 3) return false;
      
      final year = int.parse(dateParts[0]);
      final month = int.parse(dateParts[1]);
      final day = int.parse(dateParts[2]);
      
      final timeParts = parts[1].split(':');
      if (timeParts.length != 3) return false;
      
      final hour = int.parse(timeParts[0]);
      final minute = int.parse(timeParts[1]);
      final second = int.parse(timeParts[2]);
      
      final messageTime = DateTime(year, month, day, hour, minute, second);
      final now = DateTime.now();
      final difference = now.difference(messageTime);
      
      // 如果超过365天（1年），不显示相对时间
      return difference.inDays < 365;
    } catch (e) {
      debugPrint('解析时间失败: $e');
      return false;
    }
  }

  /// 计算相对时间
  String _getRelativeTime(String timeStr) {
    try {
      // 解析时间格式：2026/6/12 08:32:25
      final parts = timeStr.split(' ');
      if (parts.length != 2) return '未知';
      
      final dateParts = parts[0].split('/');
      if (dateParts.length != 3) return '未知';
      
      final year = int.parse(dateParts[0]);
      final month = int.parse(dateParts[1]);
      final day = int.parse(dateParts[2]);
      
      final timeParts = parts[1].split(':');
      if (timeParts.length != 3) return '未知';
      
      final hour = int.parse(timeParts[0]);
      final minute = int.parse(timeParts[1]);
      final second = int.parse(timeParts[2]);
      
      final messageTime = DateTime(year, month, day, hour, minute, second);
      final now = DateTime.now();
      final difference = now.difference(messageTime);
      
      if (difference.inSeconds < 60) {
        return '${difference.inSeconds}秒前';
      } else if (difference.inMinutes < 60) {
        return '${difference.inMinutes}分钟前';
      } else if (difference.inHours < 24) {
        return '${difference.inHours}小时前';
      } else if (difference.inDays < 30) {
        return '${difference.inDays}天前';
      } else if (difference.inDays < 365) {
        final months = (difference.inDays / 30).floor();
        return '$months个月前';
      } else {
        final years = (difference.inDays / 365).floor();
        return '$years年前';
      }
    } catch (e) {
      debugPrint('解析时间失败: $e');
      return '未知';
    }
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
      // 生成时间格式：2026/6/12 12:15:17
      final timeStr = formatTime(DateTime.now());
      
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'insertlog',
          'info': {
            'deviceId': deviceId,
            'lorastr': loraData,
            'upDateDevice': 'FLUTTER',
            'time': timeStr,
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
          padding: const EdgeInsets.symmetric(vertical: 14, horizontal: 8),
          decoration: BoxDecoration(
            color: color.withOpacity(0.1),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: color.withOpacity(0.3), width: 1.5),
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon, size: 26, color: color),
              const SizedBox(height: 4),
              Text(
                label,
                textAlign: TextAlign.center,
                style: TextStyle(
                  fontSize: 11,
                  fontWeight: FontWeight.bold,
                  color: color,
                ),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            ],
          ),
        ),
      ),
    );
  }
}
