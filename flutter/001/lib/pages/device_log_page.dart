import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';
import '../main.dart'; // 全局 globalWechatId
import 'device_log_map_page.dart';

/// FC 地址常量
const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

/// 云端记录页面（分页加载，支持下拉刷新+上拉加载更多）
class DeviceLogPage extends StatefulWidget {
  const DeviceLogPage({super.key});

  @override
  State<DeviceLogPage> createState() => _DeviceLogPageState();
}

class _DeviceLogPageState extends State<DeviceLogPage> {
  // 三个tab独立数据：0=全部, 1=GPS, 2=对时
  final Map<int, List<Map<String, dynamic>>> _logsMap = {0: [], 1: [], 2: []};
  final Map<int, bool> _isLoadingMap = {0: false, 1: false, 2: false};
  final Map<int, bool> _isLoadingMoreMap = {0: false, 1: false, 2: false};
  final Map<int, bool> _hasMoreMap = {0: true, 1: true, 2: true};
  final Map<int, int> _pageMap = {0: 1, 1: 1, 2: 1};
  static const int _pageSize = 10;
  String _errorMessage = '';
  bool _isFromCache = false;
  int _filterType = 0; // 当前选中的tab: 0=全部, 1=GPS, 2=对时

  // 快捷访问当前tab的状态
  List<Map<String, dynamic>> get _logs => _logsMap[_filterType]!;
  bool get _isLoading => _isLoadingMap[_filterType]!;
  bool get _isLoadingMore => _isLoadingMoreMap[_filterType]!;
  bool get _hasMore => _hasMoreMap[_filterType]!;
  int get _page => _pageMap[_filterType]!;

  // 设备ID -> 别名映射
  final Map<String, String> _deviceRenameMap = {};
  bool _timestampConvertEnabled = false; // 对时时间戳转换开关

  /// 7种鲜艳颜色用于区分不同上报设备
  static const List<Color> _deviceColors = [
    Color(0xFF2196F3), // 蓝色
    Color(0xFF4CAF50), // 绿色
    Color(0xFFF44336), // 红色
    Color(0xFFFF9800), // 橙色
    Color(0xFF9C27B0), // 紫色
    Color(0xFF00BCD4), // 青色
    Color(0xFFE91E63), // 粉色
  ];

  @override
  void initState() {
    super.initState();
    _loadDeviceRenameMap();
    _loadTimestampConvertSetting();
    _loadLogs(reset: true);
  }


  /// 加载所有设备别名映射
  Future<void> _loadDeviceRenameMap() async {
    try {
      final devices = await DBHelper().getDevices();
      setState(() {
        for (final d in devices) {
          final deviceId = d['deviceId']?.toString() ?? '';
          final rename = d['rename']?.toString() ?? '';
          if (deviceId.isNotEmpty && rename.isNotEmpty) {
            _deviceRenameMap[deviceId] = rename;
          }
        }
      });
    } catch (e) {
      debugPrint('[云端记录] 加载设备别名失败: $e');
    }
  }

  /// 加载对时时间戳转换设置
  Future<void> _loadTimestampConvertSetting() async {
    try {
      final value = await DBHelper().getBoolSetting(
        'timestamp_convert_enabled',
        defaultValue: false,
      );
      setState(() {
        _timestampConvertEnabled = value;
      });
    } catch (e) {
      debugPrint('[云端记录] 加载时间戳转换设置失败: $e');
    }
  }

  /// 加载日志（reset=true 从头加载，reset=false 追加下一页）
  Future<void> _loadLogs({bool reset = false}) async {
    final ft = _filterType;
    if (reset) {
      if (_isLoadingMap[ft]!) return;
      setState(() {
        _isLoadingMap[ft] = true;
        _pageMap[ft] = 1;
        _hasMoreMap[ft] = true;
        _errorMessage = '';
      });
    } else {
      if (_isLoadingMoreMap[ft]! || !_hasMoreMap[ft]!) return;
      setState(() {
        _isLoadingMoreMap[ft] = true;
      });
    }

    final currentPage = reset ? 1 : _pageMap[ft]! + 1;

    // 构建请求参数：GPS=type1, 对时=type2, 全部不传type
    final info = <String, dynamic>{
      'page': currentPage,
      'limit': _pageSize,
      'wechatid': globalWechatId,
    };
    if (ft == 1) {
      info['type'] = 1; // GPS
    } else if (ft == 2) {
      info['type'] = 2; // 对时
    }

    debugPrint('[云端记录] 请求: tab=$ft, page=$currentPage, limit=$_pageSize');

    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getlastlog',
          'info': info,
        }),
      );

      debugPrint('[云端记录] 响应状态: ${resp.statusCode}');

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

            debugPrint('[云端记录] tab=$ft 解析到 ${parsedLogs.length} 条数据');
            setState(() {
              if (reset) {
                _logsMap[ft] = parsedLogs;
              } else {
                _logsMap[ft]!.addAll(parsedLogs);
              }
              _pageMap[ft] = currentPage;
              _hasMoreMap[ft] = parsedLogs.length >= _pageSize;
              _isLoadingMap[ft] = false;
              _isLoadingMoreMap[ft] = false;
              _isFromCache = false;
            });
          } else {
            debugPrint('[云端记录] data不是List类型: ${data.runtimeType}');
            setState(() {
              _errorMessage = '数据格式错误';
              _isLoadingMap[ft] = false;
              _isLoadingMoreMap[ft] = false;
            });
          }
        } else {
          setState(() {
            _errorMessage = json['msg'] ?? '加载失败';
            _isLoadingMap[ft] = false;
            _isLoadingMoreMap[ft] = false;
          });
        }
      } else {
        setState(() {
          _errorMessage = 'HTTP错误: ${resp.statusCode}';
          _isLoadingMap[ft] = false;
          _isLoadingMoreMap[ft] = false;
        });
      }
    } catch (e) {
      debugPrint('[云端记录] 网络失败: $e，切换到蓝牙缓存');
      setState(() { _isFromCache = true; });
      await _loadFromBluetoothCache(reset: reset, filterType: ft);
    }
  }

  /// 从蓝牙缓存加载记录（断网回退）
  Future<void> _loadFromBluetoothCache({bool reset = false, int filterType = 0}) async {
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

          // 根据filterType过滤缓存数据
          if (filterType == 1 && type != '1' && type != '5') continue;
          if (filterType == 2 && type != '2') continue;

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

      debugPrint('[云端缓存] tab=$filterType 解析到 ${matchedLogs.length} 条记录');
      setState(() {
        _logsMap[filterType] = matchedLogs;
        _hasMoreMap[filterType] = false;
        _isLoadingMap[filterType] = false;
        _isLoadingMoreMap[filterType] = false;
      });
    } catch (e) {
      debugPrint('[云端缓存] 蓝牙缓存加载失败: $e');
      setState(() {
        _errorMessage = '无网络且无缓存数据';
        _isLoadingMap[filterType] = false;
        _isLoadingMoreMap[filterType] = false;
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

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('云端记录'),
        backgroundColor: Colors.white,
        foregroundColor: Colors.black87,
        elevation: 0,
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 16),
            child: Center(
              child: Text(
                '${_logs.length} 条',
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
          const Divider(height: 1),
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
                    : _logs.isEmpty
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
                              child: _buildLogList(),
                            ),
                          ),
          ),
        ],
      ),
    );
  }

  /// 日志列表
  Widget _buildLogList() {
    if (_isLoading && _logs.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }

    if (_logs.isEmpty) {
      return ListView(
        children: const [
          SizedBox(height: 100),
          Center(child: Text('暂无记录')),
        ],
      );
    }

    return ListView.builder(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 0),
      itemCount: _logs.length + (_hasMore ? 1 : 0),
      itemBuilder: (context, index) {
        if (index >= _logs.length) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            _loadLogs(reset: false);
          });
          return const Center(
            child: Padding(
              padding: EdgeInsets.all(16),
              child: CircularProgressIndicator(),
            ),
          );
        }
        return _buildLogCard(_logs[index], index);
      },
    );
  }

  /// 构建顶部过滤栏
  Widget _buildFilterBar() {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      child: Row(
        children: [
          _buildFilterChip(0, '全部'),
          const SizedBox(width: 12),
          _buildFilterChip(1, 'GPS'),
          const SizedBox(width: 12),
          _buildFilterChip(2, '对时'),
        ],
      ),
    );
  }

  /// 构建过滤标签（与设备记录页面一致，切换时独立加载数据）
  Widget _buildFilterChip(int type, String label) {
    final isSelected = _filterType == type;
    return GestureDetector(
      onTap: () {
        if (_filterType == type) return;
        setState(() {
          _filterType = type;
        });
        // 如果该tab还没有数据，自动加载
        if (_logsMap[type]!.isEmpty && !_isLoadingMap[type]!) {
          _loadLogs(reset: true);
        }
      },
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        decoration: BoxDecoration(
          color: isSelected ? const Color(0xFF1976D2) : Colors.grey[200],
          borderRadius: BorderRadius.circular(20),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (isSelected)
              const Icon(Icons.check, size: 16, color: Colors.white),
            if (isSelected) const SizedBox(width: 4),
            Text(
              label,
              style: TextStyle(
                fontSize: 14,
                color: isSelected ? Colors.white : Colors.black87,
                fontWeight: isSelected ? FontWeight.bold : FontWeight.normal,
              ),
            ),
          ],
        ),
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

  /// 根据type获取标签文字和颜色
  ({String label, Color color}) _getTypeInfo(dynamic typeVal) {
    final type = typeVal is int ? typeVal : int.tryParse(typeVal?.toString() ?? '') ?? 0;
    switch (type) {
      case 1:
        return (label: '定位', color: const Color(0xFF1976D2));
      case 2:
        return (label: '对时', color: const Color(0xFFFFC107));
      case 5:
        return (label: '跟踪', color: const Color(0xFF9C27B0));
      case 6:
        return (label: '配置', color: const Color(0xFF9C27B0));
      default:
        return (label: type.toString(), color: Colors.grey);
    }
  }

  /// 获取设备ID颜色
  Color _getDeviceIdColor(String deviceId) {
    final match = RegExp(r'v4-(\d+)').firstMatch(deviceId);
    if (match != null) {
      final num = int.tryParse(match.group(1) ?? '') ?? 0;
      return _deviceColors[(num * 3) % 11 % _deviceColors.length];
    }
    return _deviceColors[deviceId.hashCode.abs() % _deviceColors.length];
  }

  String _str(dynamic v) {
    if (v == null || v.toString().isEmpty) return '—';
    return v.toString();
  }

  /// 转换对时lorastr中的时间戳为可读时间
  String _convertSyncLorastrTimestamp(String lorastr) {
    if (lorastr.isEmpty || !lorastr.contains('|')) return lorastr;
    final parts = lorastr.split('|');
    if (parts.length < 4) return lorastr;
    final timestamp = int.tryParse(parts[2]);
    if (timestamp == null || timestamp < 1000000000) return lorastr;
    final dt = DateTime.fromMillisecondsSinceEpoch(timestamp * 1000);
    final timeStr = '${dt.year}-${_pad(dt.month)}-${_pad(dt.day)} ${_pad(dt.hour)}:${_pad(dt.minute)}:${_pad(dt.second)}';
    parts[2] = timeStr;
    return parts.join('|');
  }

  String _pad(int n) => n.toString().padLeft(2, '0');

  /// 构建日志卡片（与设备记录页面标准一致）
  Widget _buildLogCard(Map<String, dynamic> log, int index) {
    final time = _str(log['time']);
    final rawLorastr = _str(log['lorastr']);
    final upDateDevice = _str(log['upDateDevice']);
    final typeInfo = _getTypeInfo(log['type']);
    final typeStr = log['type']?.toString() ?? '';

    // 对时记录且开启时间戳转换时，转换lorastr中的时间戳
    final String lorastr;
    if (typeStr == '2' && _timestampConvertEnabled) {
      lorastr = _convertSyncLorastrTimestamp(rawLorastr);
    } else {
      lorastr = rawLorastr;
    }

    final rssiVal = log['rssi'];
    final snrVal = log['snr'];
    final rssi = rssiVal != null ? '${rssiVal}dBm' : '—';
    final snr = snrVal != null ? snrVal.toString() : '—';

    final bool hasGps = typeStr == '1' || typeStr == '5';

    // 交替背景色：偶数行绿色，奇数行蓝色
    final isEven = index % 2 == 0;

    Widget card = Container(
      margin: EdgeInsets.zero,
      decoration: BoxDecoration(
        color: isEven ? const Color(0xFFE8F5E9) : const Color(0xFFE3F2FD),
        borderRadius: BorderRadius.circular(8),
      ),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 第一行：时间 | 设备名（含别名）
          Row(
            children: [
              Text(
                time,
                style: const TextStyle(
                  fontSize: 14,
                  color: Colors.black87,
                ),
              ),
              const Spacer(),
              RichText(
                text: TextSpan(
                  children: [
                    TextSpan(
                      text: '| $upDateDevice',
                      style: TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.bold,
                        color: _getDeviceIdColor(upDateDevice),
                      ),
                    ),
                    if (_deviceRenameMap.containsKey(upDateDevice))
                      TextSpan(
                        text: ' (${_deviceRenameMap[upDateDevice]})',
                        style: const TextStyle(
                          fontSize: 10,
                          fontWeight: FontWeight.bold,
                          color: Colors.black87,
                        ),
                      ),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          // 第二行：类型标签 + lorastr
          Row(
            children: [
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                decoration: BoxDecoration(
                  color: typeInfo.color,
                  borderRadius: BorderRadius.circular(4),
                ),
                child: Text(
                  typeInfo.label,
                  style: const TextStyle(
                    fontSize: 12,
                    color: Colors.white,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  lorastr,
                  style: const TextStyle(
                    fontSize: 14,
                    color: Colors.black87,
                  ),
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          // 第三行：RSSI和SNR
          Row(
            children: [
              Text(
                'RSSI: $rssi',
                style: const TextStyle(
                  fontSize: 14,
                  color: Colors.black87,
                ),
              ),
              const Spacer(),
              Text(
                'SNR: $snr',
                style: const TextStyle(
                  fontSize: 14,
                  color: Colors.black87,
                ),
              ),
            ],
          ),
        ],
      ),
    );

    // type 1/5 可点击查看地图
    if (hasGps) {
      return GestureDetector(
        onTap: () => _openLogMap(log, lorastr, typeStr),
        child: card,
      );
    }
    return card;
  }

  /// 打开日志定位地图
  void _openLogMap(Map<String, dynamic> log, String lorastr, String type) {
    final parts = lorastr.split('|');
    if (parts.length < 3) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('该记录无GPS坐标信息')),
      );
      return;
    }
    final gpsStr = parts[2];
    final gpsParts = gpsStr.split(',');
    if (gpsParts.length < 2) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('GPS坐标格式异常')),
      );
      return;
    }
    final lat = double.tryParse(gpsParts[0].trim());
    final lng = double.tryParse(gpsParts[1].trim());
    if (lat == null || lng == null || (lat == 0 && lng == 0)) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('GPS坐标无效')),
      );
      return;
    }

    final deviceId = parts.length >= 2 ? parts[1] : '';
    final rename = _deviceRenameMap[deviceId] ?? '';
    final deviceName = rename.isNotEmpty ? '$deviceId ($rename)' : deviceId;

    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => DeviceLogMapPage(
          latitude: lat,
          longitude: lng,
          time: _str(log['time']),
          deviceId: deviceId,
          type: type,
          deviceName: deviceName,
        ),
      ),
    );
  }
}
