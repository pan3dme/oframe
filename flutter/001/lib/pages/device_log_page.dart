import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

/// FC 地址常量
const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

/// 设备日志记录页面（最近10条记录）
class DeviceLogPage extends StatefulWidget {
  const DeviceLogPage({super.key});

  @override
  State<DeviceLogPage> createState() => _DeviceLogPageState();
}

class _DeviceLogPageState extends State<DeviceLogPage> {
  List<Map<String, dynamic>> _logs = [];
  bool _isLoading = true;
  String _errorMessage = '';

  @override
  void initState() {
    super.initState();
    _loadLogs();
  }

  /// 加载日志
  Future<void> _loadLogs() async {
    setState(() {
      _isLoading = true;
      _errorMessage = '';
    });

    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getlastlog'}),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final data = json['data'];
          if (data is List) {
            final parsedLogs = data.map((item) {
              if (item is Map<String, dynamic>) {
                final parsedItem = <String, dynamic>{};
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
              _isLoading = false;
            });
          } else {
            setState(() {
              _errorMessage = '数据格式错误';
              _isLoading = false;
            });
          }
        } else {
          setState(() {
            _errorMessage = json['msg'] ?? '加载失败';
            _isLoading = false;
          });
        }
      } else {
        setState(() {
          _errorMessage = 'HTTP错误: ${resp.statusCode}';
          _isLoading = false;
        });
      }
    } catch (e) {
      setState(() {
        _errorMessage = '加载失败: $e';
        _isLoading = false;
      });
    }
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
      return now.difference(messageTime).inDays < 365;
    } catch (e) {
      return false;
    }
  }

  /// 计算相对时间
  String _getRelativeTime(String timeStr) {
    try {
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
      if (difference.inSeconds < 60) return '${difference.inSeconds}秒前';
      if (difference.inMinutes < 60) return '${difference.inMinutes}分钟前';
      if (difference.inHours < 24) return '${difference.inHours}小时前';
      if (difference.inDays < 30) return '${difference.inDays}天前';
      if (difference.inDays < 365) return '${(difference.inDays / 30).floor()}个月前';
      return '${(difference.inDays / 365).floor()}年前';
    } catch (e) {
      return '未知';
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('最近10条记录'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : _errorMessage.isNotEmpty
              ? Center(
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      const Icon(Icons.error_outline, size: 48, color: Colors.red),
                      const SizedBox(height: 16),
                      Text(_errorMessage, style: const TextStyle(color: Colors.red)),
                      const SizedBox(height: 16),
                      ElevatedButton.icon(
                        onPressed: _loadLogs,
                        icon: const Icon(Icons.refresh),
                        label: const Text('刷新'),
                      ),
                    ],
                  ),
                )
              : _logs.isEmpty
                  ? const Center(
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(Icons.inbox, size: 48, color: Colors.grey),
                          SizedBox(height: 16),
                          Text('暂无日志记录', style: TextStyle(color: Colors.grey)),
                        ],
                      ),
                    )
                  : RefreshIndicator(
                      onRefresh: _loadLogs,
                      child: ListView.builder(
                        padding: const EdgeInsets.all(12),
                        itemCount: _logs.length,
                        itemBuilder: (context, index) {
                          return _buildLogCard(_logs[index], index);
                        },
                      ),
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
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Row(
                  children: [
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                      decoration: BoxDecoration(
                        color: Colors.blue.withOpacity(0.1),
                        borderRadius: BorderRadius.circular(16),
                      ),
                      child: Text(
                        '#${index + 1}',
                        style: const TextStyle(fontWeight: FontWeight.bold, color: Colors.blue, fontSize: 12),
                      ),
                    ),
                    const SizedBox(width: 8),
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                      decoration: BoxDecoration(
                        color: Colors.purple.withOpacity(0.1),
                        borderRadius: BorderRadius.circular(16),
                      ),
                      child: Text(
                        deviceId,
                        style: const TextStyle(fontWeight: FontWeight.bold, color: Colors.purple, fontSize: 12),
                      ),
                    ),
                  ],
                ),
                Row(
                  children: [
                    Text(time, style: TextStyle(fontSize: 11, color: Colors.grey.shade600)),
                    if (_shouldShowRelativeTime(time)) ...[
                      const Text(' (', style: TextStyle(fontSize: 11, color: Colors.grey)),
                      Text(relativeTime, style: const TextStyle(fontSize: 11, fontWeight: FontWeight.bold, color: Colors.orange)),
                      const Text(')', style: TextStyle(fontSize: 11, color: Colors.grey)),
                    ],
                  ],
                ),
              ],
            ),
            const SizedBox(height: 8),
            const Text('LORA 数据:', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 12, color: Colors.grey)),
            const SizedBox(height: 4),
            Text(lorastr, style: const TextStyle(fontSize: 13)),
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
                      child: const Center(child: Text('图片加载失败', style: TextStyle(color: Colors.grey, fontSize: 12))),
                    );
                  },
                ),
              ),
            ],
            const SizedBox(height: 8),
            Row(
              children: [
                const Icon(Icons.device_hub, size: 14, color: Colors.grey),
                const SizedBox(width: 4),
                Text('更新来源: $upDateDevice', style: TextStyle(fontSize: 11, color: Colors.grey.shade600)),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
