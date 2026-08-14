import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';

/// FC 地址常量
const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

/// 云端记录页面（分页加载，支持下拉刷新+上拉加载更多）
class DeviceLogPage extends StatefulWidget {
  const DeviceLogPage({super.key});

  @override
  State<DeviceLogPage> createState() => _DeviceLogPageState();
}

class _DeviceLogPageState extends State<DeviceLogPage> {
  List<Map<String, dynamic>> _logs = [];
  bool _isLoading = true;
  bool _isLoadingMore = false;
  bool _hasMore = true;
  int _page = 1;
  static const int _pageSize = 10;
  String _errorMessage = '';
  bool _isFromCache = false; // 是否使用蓝牙缓存数据
  String? _filterType; // 数据过滤类型: null=全部, '1'=GPS, '2'=对时

  @override
  void initState() {
    super.initState();
    _loadLogs(reset: true);
  }

  /// 加载日志（reset=true 从头加载，reset=false 追加下一页）
  Future<void> _loadLogs({bool reset = false}) async {
    if (reset) {
      setState(() {
        _isLoading = true;
        _page = 1;
        _hasMore = true;
        _errorMessage = '';
      });
    } else {
      if (_isLoadingMore || !_hasMore) return;
      setState(() {
        _isLoadingMore = true;
      });
    }

    final currentPage = reset ? 1 : _page + 1;

    debugPrint('[云端记录] 请求: page=$currentPage, limit=$_pageSize');

    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getlastlog',
          'info': {
            'page': currentPage,
            'limit': _pageSize,
          },
        }),
      );

      debugPrint('[云端记录] 响应状态: ${resp.statusCode}');
      debugPrint('[云端记录] 响应body: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        debugPrint('[云端记录] 解析JSON status=${json['status']}');
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

            debugPrint('[云端记录] 解析到 ${parsedLogs.length} 条数据');
            setState(() {
              if (reset) {
                _logs = parsedLogs;
              } else {
                _logs.addAll(parsedLogs);
              }
              _page = currentPage;
              _hasMore = parsedLogs.length >= _pageSize;
              _isLoading = false;
              _isLoadingMore = false;
              _isFromCache = false; // 网络成功，恢复标题
            });
          } else {
            debugPrint('[云端记录] data不是List类型: ${data.runtimeType}');
            setState(() {
              _errorMessage = '数据格式错误';
              _isLoading = false;
              _isLoadingMore = false;
            });
          }
        } else {
          setState(() {
            _errorMessage = json['msg'] ?? '加载失败';
            _isLoading = false;
            _isLoadingMore = false;
          });
        }
      } else {
        setState(() {
          _errorMessage = 'HTTP错误: ${resp.statusCode}';
          _isLoading = false;
          _isLoadingMore = false;
        });
      }
    } catch (e) {
      debugPrint('[云端记录] 网络失败: $e，切换到蓝牙缓存');
      setState(() { _isFromCache = true; });
      await _loadFromBluetoothCache(reset: reset);
    }
  }

  /// 从蓝牙缓存加载记录（断网回退）
  Future<void> _loadFromBluetoothCache({bool reset = false}) async {
    try {
      final allBluetoothData = await DBHelper().getBluetoothData();
      debugPrint('[云端缓存] 蓝牙缓存共 ${allBluetoothData.length} 条');

      final matchedLogs = <Map<String, dynamic>>[];
      for (final item in allBluetoothData) {
        final dataStr = item['data'] as String?;
        if (dataStr == null || dataStr.isEmpty) continue;

        try {
          final jsonData = jsonDecode(dataStr) as Map<String, dynamic>;
          final info = jsonData['info'] as String? ?? '';
          if (!info.contains('|')) continue;

          final parts = info.split('|');
          final type = parts.isNotEmpty ? parts[0] : '';

          matchedLogs.add({
            'time': jsonData['time'] ?? item['time'] ?? '',
            'lorastr': info,
            'upDateDevice': jsonData['upDateDevice'] ?? '',
            'type': type,
            'rssi': jsonData['rssi'],
            'snr': jsonData['snr'],
          });
        } catch (_) {
          continue;
        }
      }

      // 按时间降序排序（最新在最上面）
      matchedLogs.sort((a, b) {
        final ta = _parseCacheTime(a['time']?.toString() ?? '');
        final tb = _parseCacheTime(b['time']?.toString() ?? '');
        return tb.compareTo(ta);
      });

      debugPrint('[云端缓存] 解析到 ${matchedLogs.length} 条记录');
      setState(() {
        _logs = matchedLogs;
        _hasMore = false; // 缓存数据不分页
        _isLoading = false;
        _isLoadingMore = false;
      });
    } catch (e) {
      debugPrint('[云端缓存] 蓝牙缓存加载失败: $e');
      setState(() {
        _errorMessage = '无网络且无缓存数据';
        _isLoading = false;
        _isLoadingMore = false;
      });
    }
  }

  /// 解析缓存时间字符串（格式如 "2026/6/12 13:12:44"）
  DateTime _parseCacheTime(String timeStr) {
    try {
      final datePart = timeStr.split(' ')[0];
      final timePart = timeStr.split(' ').length > 1 ? timeStr.split(' ')[1] : '00:00:00';
      final dp = datePart.split('/');
      final tp = timePart.split(':');
      return DateTime(
        int.parse(dp[0]),
        dp.length > 1 ? int.parse(dp[1]) : 1,
        dp.length > 2 ? int.parse(dp[2]) : 1,
        tp.length > 0 ? int.parse(tp[0]) : 0,
        tp.length > 1 ? int.parse(tp[1]) : 0,
        tp.length > 2 ? int.parse(tp[2]) : 0,
      );
    } catch (_) {
      return DateTime.fromMillisecondsSinceEpoch(0);
    }
  }

  /// 获取过滤后的日志列表
  List<Map<String, dynamic>> get _filteredLogs {
    if (_filterType == null) return _logs;
    return _logs.where((log) {
      final type = log['type']?.toString() ?? '';
      if (_filterType == '1') return type == '1' || type == '5'; // GPS显示1和5
      return type == _filterType;
    }).toList();
  }

  @override
  Widget build(BuildContext context) {
    final filteredLogs = _filteredLogs;

    return Scaffold(
      appBar: AppBar(
        title: Text(_isFromCache ? '云端记录(断网)' : '云端记录'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 16),
            child: Center(
              child: Text(
                '${filteredLogs.length} 条',
                style: const TextStyle(fontSize: 14, color: Colors.black54),
              ),
            ),
          ),
        ],
      ),
      body: Column(
        children: [
          // 顶部过滤栏
          _buildFilterBar(),
          // 内容区域
          Expanded(
            child: _isLoading && _logs.isEmpty
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
                              onPressed: () => _loadLogs(reset: true),
                              icon: const Icon(Icons.refresh),
                              label: const Text('刷新'),
                            ),
                          ],
                        ),
                      )
                    : filteredLogs.isEmpty
                        ? const Center(
                            child: Column(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                Icon(Icons.inbox, size: 48, color: Colors.grey),
                                SizedBox(height: 16),
                                Text('暂无记录', style: TextStyle(color: Colors.grey)),
                              ],
                            ),
                          )
                        : NotificationListener<ScrollNotification>(
                            onNotification: (scrollInfo) {
                              if (scrollInfo.metrics.pixels >= scrollInfo.metrics.maxScrollExtent - 100) {
                                if (_hasMore && !_isLoadingMore && !_isLoading) {
                                  _loadLogs(reset: false);
                                }
                              }
                              return false;
                            },
                            child: RefreshIndicator(
                              onRefresh: () => _loadLogs(reset: true),
                              child: ListView.builder(
                                padding: const EdgeInsets.all(12),
                                itemCount: filteredLogs.length + (_hasMore && !_isLoading ? 1 : 0),
                                itemBuilder: (context, index) {
                                  if (index >= filteredLogs.length) {
                                    return _buildLoadingMoreIndicator();
                                  }
                                  return _buildLogCard(filteredLogs[index], index);
                                },
                              ),
                            ),
                          ),
          ),
        ],
      ),
    );
  }

  /// 构建顶部过滤栏
  Widget _buildFilterBar() {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: Colors.white,
        boxShadow: [
          BoxShadow(
            color: Colors.grey.withOpacity(0.1),
            blurRadius: 4,
            offset: const Offset(0, 2),
          ),
        ],
      ),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          _buildFilterChip(null, _isFromCache ? '蓝牙记录' : '云端记录', _isFromCache ? Icons.bluetooth : Icons.cloud),
          const SizedBox(width: 8),
          _buildFilterChip('1', 'GPS', Icons.gps_fixed),
          const SizedBox(width: 8),
          _buildFilterChip('2', '对时', Icons.access_time),
        ],
      ),
    );
  }

  /// 构建过滤标签
  Widget _buildFilterChip(String? type, String label, IconData icon) {
    final isSelected = _filterType == type;
    return FilterChip(
      selected: isSelected,
      label: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 14, color: isSelected ? Colors.white : Colors.blue),
          const SizedBox(width: 4),
          Text(label),
        ],
      ),
      onSelected: (selected) {
        setState(() {
          _filterType = selected ? type : null;
        });
      },
      selectedColor: Colors.blue,
      backgroundColor: Colors.grey[100],
      checkmarkColor: Colors.white,
      labelStyle: TextStyle(
        color: isSelected ? Colors.white : Colors.black87,
        fontSize: 12,
      ),
    );
  }

  /// 加载更多指示器
  Widget _buildLoadingMoreIndicator() {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 16),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const SizedBox(
            width: 16,
            height: 16,
            child: CircularProgressIndicator(strokeWidth: 2),
          ),
          const SizedBox(width: 8),
          Text('加载中...', style: TextStyle(color: Colors.grey[600], fontSize: 12)),
        ],
      ),
    );
  }

  /// 构建日志卡片（参照截图样式）
  Widget _buildLogCard(Map<String, dynamic> log, int index) {
    final lorastr = log['lorastr']?.toString() ?? '—';
    final time = log['time']?.toString() ?? '—';
    final upDateDevice = log['upDateDevice']?.toString() ?? '—';
    final rssiVal = log['rssi'];
    final snrVal = log['snr'];
    final rssi = rssiVal != null ? '${rssiVal}dBm' : '—';
    final snr = snrVal != null ? snrVal.toString() : '—';
    final typeStr = log['type']?.toString() ?? '';

    // 解析 lorastr 获取开头的 type 数字
    String leadingType = typeStr;
    if (lorastr.contains('|')) {
      final parts = lorastr.split('|');
      if (parts.isNotEmpty) {
        leadingType = parts[0];
      }
    }

    // 交替背景色：偶数行绿色，奇数行蓝色
    final isEven = index % 2 == 0;
    final bgColor = isEven ? const Color(0xFFE8F5E9) : const Color(0xFFE3F2FD);

    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: bgColor,
        borderRadius: BorderRadius.circular(8),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 左侧序号
          Container(
            width: 24,
            height: 24,
            decoration: BoxDecoration(
              color: Colors.grey[300],
              shape: BoxShape.circle,
            ),
            child: Center(
              child: Text(
                '${index + 1}',
                style: const TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.bold,
                  color: Colors.black54,
                ),
              ),
            ),
          ),
          const SizedBox(width: 10),
          // 右侧内容
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // 第一行：type(红) | lorastr(绿) | type(红)
                RichText(
                  text: TextSpan(
                    style: const TextStyle(fontSize: 14, fontFamily: 'monospace'),
                    children: [
                      TextSpan(
                        text: leadingType,
                        style: const TextStyle(color: Colors.red, fontWeight: FontWeight.bold),
                      ),
                      TextSpan(
                        text: '|',
                        style: const TextStyle(color: Colors.green),
                      ),
                      TextSpan(
                        text: lorastr.contains('|') ? lorastr.substring(lorastr.indexOf('|') + 1) : lorastr,
                        style: const TextStyle(color: Colors.green),
                      ),
                      if (typeStr.isNotEmpty && typeStr != leadingType) ...[
                        TextSpan(text: '|', style: const TextStyle(color: Colors.green)),
                        TextSpan(
                          text: typeStr,
                          style: const TextStyle(color: Colors.red, fontWeight: FontWeight.bold),
                        ),
                      ],
                    ],
                  ),
                ),
                const SizedBox(height: 4),
                // 第二行：时间 + (upDateDevice 蓝色)
                Row(
                  children: [
                    Text(
                      time,
                      style: const TextStyle(fontSize: 12, color: Colors.black54),
                    ),
                    const SizedBox(width: 8),
                    Text(
                      '($upDateDevice)',
                      style: const TextStyle(
                        fontSize: 13,
                        fontWeight: FontWeight.bold,
                        color: Colors.blue,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 4),
                // 第三行：RSSI + SNR
                Row(
                  children: [
                    Text(
                      'rssi:$rssi',
                      style: const TextStyle(fontSize: 12, color: Colors.black54),
                    ),
                    const SizedBox(width: 16),
                    Text(
                      'snr:$snr',
                      style: const TextStyle(fontSize: 12, color: Colors.black54),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
