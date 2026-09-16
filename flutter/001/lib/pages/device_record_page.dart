import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';
import '../main.dart';
import 'device_log_map_page.dart';

/// 设备记录页面
class DeviceRecordPage extends StatefulWidget {
  final Map<String, dynamic> device;
  final Map<String, dynamic>? deviceLot;

  const DeviceRecordPage({
    super.key,
    required this.device,
    this.deviceLot,
  });

  @override
  State<DeviceRecordPage> createState() => _DeviceRecordPageState();
}

class _DeviceRecordPageState extends State<DeviceRecordPage> {
  // 三个tab独立数据：0=全部, 1=定位, 2=对时
  final Map<int, List<Map<String, dynamic>>> _logsMap = {0: [], 1: [], 2: []};
  final Map<int, bool> _isLoadingMap = {0: false, 1: false, 2: false};
  final Map<int, bool> _isLoadingMoreMap = {0: false, 1: false, 2: false};
  final Map<int, bool> _hasMoreMap = {0: true, 1: true, 2: true};
  final Map<int, int> _offsetMap = {0: 0, 1: 0, 2: 0};
  static const int _logLimit = 20;
  int _filterType = 0; // 当前选中的tab

  // 快捷访问当前tab的状态
  List<Map<String, dynamic>> get _logs => _logsMap[_filterType]!;
  bool get _isLoading => _isLoadingMap[_filterType]!;
  bool get _isLoadingMore => _isLoadingMoreMap[_filterType]!;
  bool get _hasMore => _hasMoreMap[_filterType]!;
  int get _logOffset => _offsetMap[_filterType]!;

  static const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

  @override
  void initState() {
    super.initState();
    _loadLogs(reset: true);
  }

  /// 加载设备日志记录（每个tab独立数据）
  Future<void> _loadLogs({bool reset = false}) async {
    final ft = _filterType;
    if (reset) {
      if (_isLoadingMap[ft]!) return;
      setState(() {
        _isLoadingMap[ft] = true;
        _offsetMap[ft] = 0;
        _hasMoreMap[ft] = true;
      });
    } else {
      if (_isLoadingMoreMap[ft]! || !_hasMoreMap[ft]!) return;
      setState(() {
        _isLoadingMoreMap[ft] = true;
      });
    }

    final deviceId = widget.device['deviceId']?.toString() ?? '';

    try {
      // 构建info参数，根据当前tab添加type字段
      final info = <String, dynamic>{
        'deviceId': deviceId,
        'limit': _logLimit,
        'offset': _offsetMap[ft]!,
      };
      
      // 定位=1，对时=2，全部不传type
      if (ft == 1) {
        info['type'] = 1;
      } else if (ft == 2) {
        info['type'] = 2;
      }
      
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getDeviceLogbyId',
          'info': info,
        }),
      );

      debugPrint('设备日志响应: ${resp.statusCode}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;

        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>? ?? [];
          final parsedLogs = rawRows.map((row) {
            final rowMap = row as Map<String, dynamic>;
            final result = <String, dynamic>{};

            // 解析主键
            final pkList = rowMap['primaryKey'] as List<dynamic>? ?? [];
            for (final pk in pkList) {
              final pkMap = pk as Map<String, dynamic>;
              result[pkMap['name'] as String] = pkMap['value'];
            }

            // 解析属性列
            final attrList = rowMap['attributes'] as List<dynamic>? ?? [];
            for (final attr in attrList) {
              final attrMap = attr as Map<String, dynamic>;
              result[attrMap['columnName'] as String] = attrMap['columnValue'];
            }

            return result;
          }).toList();

          setState(() {
            if (reset) {
              _logsMap[ft] = parsedLogs;
            } else {
              _logsMap[ft]!.addAll(parsedLogs);
            }
            _offsetMap[ft] = _logsMap[ft]!.length;
            _hasMoreMap[ft] = parsedLogs.length >= _logLimit;
            _isLoadingMap[ft] = false;
            _isLoadingMoreMap[ft] = false;
          });

          debugPrint('加载日志(tab=$ft): ${parsedLogs.length} 条，总计 ${_logsMap[ft]!.length} 条');
        } else {
          debugPrint('日志请求错误: ${json['msg']}');
          setState(() {
            _isLoadingMap[ft] = false;
            _isLoadingMoreMap[ft] = false;
          });
        }
      } else {
        debugPrint('HTTP错误: ${resp.statusCode}');
        setState(() {
          _isLoadingMap[ft] = false;
          _isLoadingMoreMap[ft] = false;
        });
      }
    } catch (e) {
      debugPrint('加载日志失败(网络): $e，尝试从蓝牙缓存加载');
      setState(() {
        _isLoadingMap[ft] = false;
        _isLoadingMoreMap[ft] = false;
      });
      await _loadLogsFromBluetoothCache(deviceId, reset: reset, filterType: ft);
    }
  }

  /// 从蓝牙缓存加载设备日志（离线回退）
  Future<void> _loadLogsFromBluetoothCache(String deviceId, {bool reset = false, int filterType = 0}) async {
    try {
      final allBluetoothData = await DBHelper().getBluetoothData();
      debugPrint('[离线日志] 蓝牙缓存共 ${allBluetoothData.length} 条');

      final matchedLogs = <Map<String, dynamic>>[];
      for (final item in allBluetoothData) {
        final dataStr = item['data'] as String?;
        if (dataStr == null || dataStr.isEmpty) continue;

        try {
          final jsonData = jsonDecode(dataStr) as Map<String, dynamic>;
          final info = jsonData['info'] as String? ?? '';
          if (!info.contains('|')) continue;

          final parts = info.split('|');
          if (parts.length < 4) continue;

          final deviceMarker = parts[1];
          if (deviceMarker == deviceId) {
            final typeStr = parts[0];
            // 根据filterType过滤缓存数据
            if (filterType == 1 && typeStr != '1' && typeStr != '5') continue;
            if (filterType == 2 && typeStr != '2') continue;

            final log = <String, dynamic>{
              'time': jsonData['time'] ?? '',
              'deviceId': deviceId,
              'lorastr': info,
              'upDateDevice': jsonData['upDateDevice'] ?? '',
              'type': typeStr,
              'rssi': jsonData['rssi'],
              'snr': jsonData['snr'],
              '_fromCache': true,
            };
            matchedLogs.add(log);
          }
        } catch (_) {
          continue;
        }
      }

      debugPrint('[离线日志] 匹配到 ${matchedLogs.length} 条记录(tab=$filterType)');

      setState(() {
        if (reset) {
          _logsMap[filterType] = matchedLogs;
        } else {
          _logsMap[filterType]!.addAll(matchedLogs);
        }
        _offsetMap[filterType] = _logsMap[filterType]!.length;
        _hasMoreMap[filterType] = false;
      });
    } catch (e) {
      debugPrint('[离线日志] 加载失败: $e');
    }
  }

  /// 根据type获取标签文字和颜色
  ({String label, Color color}) _getTypeInfo(dynamic typeVal) {
    final type = typeVal is int ? typeVal : int.tryParse(typeVal?.toString() ?? '') ?? 0;
    switch (type) {
      case 1:
        return (label: '定位', color: const Color(0xFF1976D2));
      case 2:
        return (label: '对时', color: const Color(0xFF4CAF50));
      case 5:
        return (label: '跟踪', color: const Color(0xFFFF9800));
      case 6:
        return (label: '配置', color: const Color(0xFF9C27B0));
      default:
        return (label: type.toString(), color: Colors.grey);
    }
  }

  /// 获取设备ID颜色
  Color _getDeviceIdColor(String deviceId) {
    if (deviceId.contains('v4-27')) return const Color(0xFF1976D2);
    if (deviceId.contains('v4-29')) return const Color(0xFF4CAF50);
    return Colors.black87;
  }

  /// 打开日志定位地图
  void _openLogMap(Map<String, dynamic> log, String lorastr, String deviceId, String type) {
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

    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => DeviceLogMapPage(
          latitude: lat,
          longitude: lng,
          time: _str(log['time']),
          deviceId: deviceId,
          type: type,
        ),
      ),
    );
  }

  String _str(dynamic v) {
    if (v == null || v.toString().isEmpty) return '—';
    return v.toString();
  }

  @override
  Widget build(BuildContext context) {
    final deviceId = widget.device['deviceId']?.toString() ?? '';
    final rename = widget.device['rename']?.toString() ?? '';
    final displayName = rename.isNotEmpty ? '$deviceId ($rename)' : deviceId;

    return Scaffold(
      appBar: AppBar(
        title: const Text('设备记录'),
        backgroundColor: Colors.white,
        foregroundColor: Colors.black87,
        elevation: 0,
      ),
      body: Column(
        children: [
          // 筛选标签
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
            child: Row(
              children: [
                _buildFilterChip('全部', 0),
                const SizedBox(width: 12),
                _buildFilterChip('定位', 1),
                const SizedBox(width: 12),
                _buildFilterChip('对时', 2),
              ],
            ),
          ),
          const Divider(height: 1),
          // 日志列表
          Expanded(
            child: RefreshIndicator(
              onRefresh: () => _loadLogs(reset: true),
              child: _buildLogList(),
            ),
          ),
        ],
      ),
    );
  }

  /// 筛选标签（切换时独立加载数据）
  Widget _buildFilterChip(String label, int type) {
    final isSelected = _filterType == type;
    return GestureDetector(
      onTap: () {
        if (_filterType == type) return; // 已选中不重复操作
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

  /// 日志列表（直接使用当前tab的独立数据）
  Widget _buildLogList() {
    if (_isLoading && _logs.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }

    final displayLogs = _logs; // 已经是当前tab的独立数据

    if (displayLogs.isEmpty) {
      return ListView(
        children: const [
          SizedBox(height: 100),
          Center(child: Text('暂无记录')),
        ],
      );
    }

    return ListView.builder(
      padding: const EdgeInsets.all(16),
      itemCount: displayLogs.length + (_hasMore ? 1 : 0),
      itemBuilder: (context, index) {
        if (index >= displayLogs.length) {
          // 延迟到build完成后触发加载更多，避免setState during build
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

        final log = displayLogs[index];
        final isEven = index % 2 == 0;

        return _buildLogCard(log, isEven);
      },
    );
  }

  /// 日志卡片
  Widget _buildLogCard(Map<String, dynamic> log, bool isEven) {
    final time = _str(log['time']);
    final logDeviceId = _str(log['deviceId']);
    final lorastr = _str(log['lorastr']);
    final upDateDevice = _str(log['upDateDevice']);
    final typeInfo = _getTypeInfo(log['type']);
    final typeStr = log['type']?.toString() ?? '';

    final rssiVal = log['rssi'];
    final snrVal = log['snr'];
    final rssi = rssiVal != null ? '${rssiVal}dBm' : '—';
    final snr = snrVal != null ? snrVal.toString() : '—';

    final bool hasGps = typeStr == '1' || typeStr == '5';

    Widget card = Container(
      margin: const EdgeInsets.only(bottom: 12),
      decoration: BoxDecoration(
        color: isEven ? const Color(0xFFE8F5E9) : const Color(0xFFE3F2FD),
        borderRadius: BorderRadius.circular(8),
      ),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 第一行：时间 | 设备名
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
              Text(
                '| $upDateDevice',
                style: TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.bold,
                  color: _getDeviceIdColor(upDateDevice),
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
        onTap: () => _openLogMap(log, lorastr, logDeviceId, typeStr),
        child: card,
      );
    }
    return card;
  }
}
