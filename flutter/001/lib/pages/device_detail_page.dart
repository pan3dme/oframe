import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';
import '../main.dart'; // 导入全局 globalSelectedDevice, globalSelectedDeviceLot, deviceSelectedNotifier
import 'device_log_map_page.dart';
import 'device_trajectory_page.dart';
import 'device_record_page.dart';
import 'bluetooth_page.dart';
import 'device_dtu_command_page.dart';

/// 功能按钮数据类
class _FunctionButton {
  final IconData icon;
  final String label;
  final Color color;
  final VoidCallback onTap;
  _FunctionButton({required this.icon, required this.label, required this.color, required this.onTap});
}

/// 设备详情页面
class DeviceDetailPage extends StatefulWidget {
  final Map<String, dynamic> device;
  final Map<String, dynamic>? deviceLot;

  const DeviceDetailPage({
    super.key,
    required this.device,
    this.deviceLot,
  });

  @override
  State<DeviceDetailPage> createState() => _DeviceDetailPageState();
}

class _DeviceDetailPageState extends State<DeviceDetailPage> {
  List<Map<String, dynamic>> _logs = [];
  bool _isLoadingLogs = false;
  bool _isLoadingMore = false;
  bool _hasMore = true;
  int _logOffset = 0;
  static const int _logLimit = 10;
  String _reportInterval = '—'; // 上报周期
  String _bootTime = '—';       // 开机时间
  String _locationTime = '—';   // 定位时间
  String _rawConfigValue = '';  // 原始配置值（用于配置下发指令）
  String _rawLorastr = '';  // 原始完整lorastr（调试显示用）
  Map<String, dynamic> _configAttributes = {};  // getDeviceConfigAll完整属性
  bool _isBluetoothConnected = false; // 蓝牙连接状态
  bool _isFromCache = false; // 标记是否使用缓存数据（断网）

  // FC地址
  static const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

  @override
  void initState() {
    super.initState();
    _checkBluetoothConnection();
    _loadLogs(reset: true);
    // 初始化直接读缓存，不下拉刷新不发网络请求
    final deviceId = widget.device['deviceId']?.toString() ?? '';
    _loadDeviceConfigFromCache(deviceId);
  }

  /// 检查蓝牙连接状态
  void _checkBluetoothConnection() {
    setState(() {
      _isBluetoothConnected = BluetoothPage.isConnected;
    });
  }

  /// 加载设备配置数据（getDeviceConfigAll）
  Future<void> _loadDeviceConfig() async {
    final deviceId = widget.device['deviceId']?.toString() ?? '';
    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getDeviceConfigAll',
          'info': {'limit': 99, 'wechatid': globalWechatId},
        }),
      );

      debugPrint('设备配置响应: ${resp.statusCode}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>? ?? [];
          // 遍历所有设备配置，全部缓存，并找到当前设备进行解析
          for (final rawRow in rawRows) {
            final row = rawRow as Map<String, dynamic>;
            final parsed = <String, dynamic>{};

            final pkList = row['primaryKey'] as List<dynamic>? ?? [];
            for (final pk in pkList) {
              final pkMap = pk as Map<String, dynamic>;
              parsed[pkMap['name'] as String] = pkMap['value'];
            }

            final attrList = row['attributes'] as List<dynamic>? ?? [];
            for (final attr in attrList) {
              final attrMap = attr as Map<String, dynamic>;
              parsed[attrMap['columnName'] as String] = attrMap['columnValue'];
            }

            final configDeviceId = parsed['deviceId']?.toString() ?? '';
            if (configDeviceId.isNotEmpty) {
              // 缓存所有设备的配置
              await DBHelper().saveDeviceConfig(configDeviceId, parsed);
            }

            // 找到当前设备，保存完整配置并解析显示
            if (configDeviceId == deviceId) {
              setState(() {
                _configAttributes = Map<String, dynamic>.from(parsed);
              });
              final lorastr = parsed['lorastr']?.toString() ?? '';
              _parseConfigLorastr(lorastr);
              debugPrint('设备配置匹配: deviceId=$deviceId, lorastr=$lorastr, 属性数=${parsed.length}');
            }
          }
          debugPrint('设备配置缓存完成: 共 ${rawRows.length} 条');
        } else {
          debugPrint('设备配置请求错误: ${json['msg']}');
          // 从缓存加载
          await _loadDeviceConfigFromCache(deviceId);
        }
      }
    } catch (e) {
      debugPrint('加载设备配置失败: $e，尝试从缓存加载');
      // 从缓存加载
      await _loadDeviceConfigFromCache(deviceId);
    }
  }

  /// 下拉刷新：通过getDeviceConfigById获取指定设备的配置信息并缓存
  Future<void> _refreshDeviceConfig() async {
    final deviceId = widget.device['deviceId']?.toString() ?? '';
    if (deviceId.isEmpty) return;

    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getDeviceConfigById',
          'info': {'deviceId': deviceId, 'wechatid': globalWechatId},
        }),
      );

      debugPrint('[下拉刷新] 设备配置响应: ${resp.statusCode}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          // getDeviceConfigById返回单条Map，getDeviceConfigAll返回List
          final rawData = json['data'];
          final List<dynamic> rawRows;
          if (rawData is Map<String, dynamic>) {
            rawRows = [rawData];
          } else if (rawData is List) {
            rawRows = rawData;
          } else {
            rawRows = [];
          }
          for (final rawRow in rawRows) {
            final row = rawRow as Map<String, dynamic>;
            final parsed = <String, dynamic>{};

            final pkList = row['primaryKey'] as List<dynamic>? ?? [];
            for (final pk in pkList) {
              final pkMap = pk as Map<String, dynamic>;
              parsed[pkMap['name'] as String] = pkMap['value'];
            }

            final attrList = row['attributes'] as List<dynamic>? ?? [];
            for (final attr in attrList) {
              final attrMap = attr as Map<String, dynamic>;
              parsed[attrMap['columnName'] as String] = attrMap['columnValue'];
            }

            final configDeviceId = parsed['deviceId']?.toString() ?? '';
            if (configDeviceId.isNotEmpty) {
              // 缓存到所有设备的配置信息中
              await DBHelper().saveDeviceConfig(configDeviceId, parsed);
            }

            // 更新当前设备显示
            if (configDeviceId == deviceId) {
              setState(() {
                _configAttributes = Map<String, dynamic>.from(parsed);
                _isFromCache = false; // 网络成功，恢复标题
              });
              final lorastr = parsed['lorastr']?.toString() ?? '';
              _parseConfigLorastr(lorastr);
              debugPrint('[下拉刷新] 配置已更新: deviceId=$deviceId, lorastr=$lorastr');
            }
          }
          debugPrint('[下拉刷新] 配置缓存完成: 共 ${rawRows.length} 条');
        } else {
          debugPrint('[下拉刷新] 配置请求错误: ${json['msg']}');
        }
      }
    } catch (e) {
      debugPrint('[下拉刷新] 刷新配置失败: $e');
    }
  }

  /// 从缓存加载设备配置（离线回退）
  /// 断网时同时检查蓝牙缓存(type=6)和device_config表缓存
  /// 比较cached_at时间，取更新的数据
  Future<void> _loadDeviceConfigFromCache(String deviceId) async {
    String? bluetoothLorastr;
    String? bluetoothCachedAt;
    String? configLorastr;
    String? configCachedAt;

    // 第一步：从蓝牙缓存中找type=6的记录
    try {
      final allBluetoothData = await DBHelper().getBluetoothData();
      for (final item in allBluetoothData) {
        final dataStr = item['data'] as String?;
        if (dataStr == null || dataStr.isEmpty) continue;

        try {
          final jsonData = jsonDecode(dataStr) as Map<String, dynamic>;
          final info = jsonData['info'] as String? ?? '';
          if (!info.contains('|')) continue;

          final parts = info.split('|');
          if (parts.length < 3) continue;

          final type = parts[0];
          final deviceMarker = parts[1];

          if (type == '6' && deviceMarker == deviceId) {
            bluetoothLorastr = info;
            bluetoothCachedAt = item['cached_at'] as String? ?? '';
            break; // 列表已按cached_at DESC，第一条就是最新
          }
        } catch (_) {
          continue;
        }
      }
    } catch (e) {
      debugPrint('[离线配置] 蓝牙缓存加载失败: $e');
    }

    // 第二步：从device_config表缓存中找
    Map<String, dynamic>? cachedConfigFull;
    try {
      final cachedConfig = await DBHelper().getDeviceConfig(deviceId);
      if (cachedConfig != null) {
        cachedConfigFull = cachedConfig;
        configLorastr = cachedConfig['lorastr']?.toString() ?? '';
        configCachedAt = await DBHelper().getDeviceConfigCachedAt(deviceId);
      }
    } catch (e) {
      debugPrint('[离线配置] 表缓存加载失败: $e');
    }

    // 第三步：比较时间，取更新的
    if (bluetoothLorastr != null && configLorastr != null) {
      // 两个都有，比较cached_at
      final btTime = bluetoothCachedAt ?? '';
      final cfgTime = configCachedAt ?? '';
      if (btTime.compareTo(cfgTime) > 0) {
        _parseConfigLorastr(bluetoothLorastr);
        debugPrint('[离线配置] 蓝牙更新(bt=$btTime > cfg=$cfgTime)，使用蓝牙数据');
      } else {
        _parseConfigLorastr(configLorastr);
        if (cachedConfigFull != null) {
          setState(() => _configAttributes = Map<String, dynamic>.from(cachedConfigFull!));
        }
        debugPrint('[离线配置] 表缓存更新(cfg=$cfgTime >= bt=$btTime)，使用表缓存数据');
      }
    } else if (bluetoothLorastr != null) {
      _parseConfigLorastr(bluetoothLorastr);
      debugPrint('[离线配置] 仅有蓝牙数据，使用蓝牙数据');
    } else if (configLorastr != null) {
      _parseConfigLorastr(configLorastr);
      if (cachedConfigFull != null) {
        setState(() => _configAttributes = Map<String, dynamic>.from(cachedConfigFull!));
      }
      debugPrint('[离线配置] 仅有表缓存数据，使用表缓存数据');
    } else {
      debugPrint('[离线配置] 两个缓存都没有该设备的配置数据');
    }
  }

  /// TIME_DICT for base62 encoding (same as C++ TIME_DICT)
  static const String _timeDict = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';

  /// 将2字符转换为索引值（对应C++ twoCharToIndex）
  int _twoCharToIndex(String str) {
    if (str.length != 2) return -1;
    final h = _timeDict.indexOf(str[0]);
    final l = _timeDict.indexOf(str[1]);
    if (h == -1 || l == -1) return -1;
    return h * 62 + l;
  }

  /// 将索引转换为时间窗口（对应C++ indexToTimeWindow）
  /// 返回 [startHour, endHour] 或 null
  List<int>? _indexToTimeWindow(int idx) {
    if (idx < 0) return null;
    int sum = 0;
    for (int s = 0; s <= 23; s++) {
      // 最小间隔1小时
      int valid = 23 - (s + 1) + 1;
      if (valid <= 0) continue;
      if (idx < sum + valid) {
        int off = idx - sum;
        int endHour = s + 1 + off;
        return [s, endHour];
      }
      sum += valid;
    }
    return null;
  }

  /// 解析设备配置lorastr
  /// 格式: type|deviceId|30,0M,3t,6|...
  /// 第三段用逗号分隔: 上报周期(分钟),开机时间(2字符编码),GPS时间(2字符编码),其它字段
  void _parseConfigLorastr(String lorastr) {
    if (lorastr.isEmpty) return;
    // 始终保存原始lorastr用于显示，即使解析失败
    setState(() {
      _rawLorastr = lorastr;
    });
    try {
      final parts = lorastr.split('|');
      if (parts.length < 3) return;
      final configStr = parts[2]; // "30,0M,3t,6"
      final configs = configStr.split(',');
      if (configs.isEmpty) return;

      // 上报周期（第一个字段）+ 大周期（第四个字段）
      final interval = configs[0]; // "30"
      String intervalDisplay = '$interval分钟';
      if (configs.length >= 4) {
        final bigCycle = int.tryParse(configs[3]); // "6"
        if (bigCycle != null) {
          final bigCycleMinutes = bigCycle * 10; // 6 * 10 = 60
          intervalDisplay = '$interval分钟（$bigCycleMinutes分钟）';
        }
      }

      // 开机时间（第二个字段，2字符编码）
      String bootDisplay = '—';
      if (configs.length >= 2) {
        final bootCode = configs[1]; // "0M"
        final bootIndex = _twoCharToIndex(bootCode);
        final timeWindow = _indexToTimeWindow(bootIndex);
        if (timeWindow != null) {
          final startHour = timeWindow[0];
          final endHour = timeWindow[1];
          bootDisplay = '${startHour.toString().padLeft(2, '0')}:00-${endHour.toString().padLeft(2, '0')}:00';
        }
      }

      // GPS时间（第三个字段，2字符编码）
      String gpsDisplay = '—';
      if (configs.length >= 3) {
        final gpsCode = configs[2]; // "3t"
        final gpsIndex = _twoCharToIndex(gpsCode);
        final gpsWindow = _indexToTimeWindow(gpsIndex);
        if (gpsWindow != null) {
          final startHour = gpsWindow[0];
          final endHour = gpsWindow[1];
          gpsDisplay = '${startHour.toString().padLeft(2, '0')}:00-${endHour.toString().padLeft(2, '0')}:00';
        }
      }

      setState(() {
        _reportInterval = intervalDisplay;
        _bootTime = bootDisplay;
        _locationTime = gpsDisplay;
        _rawConfigValue = configStr; // 保存原始配置值
      });
      debugPrint('解析配置: 上报周期=$intervalDisplay, 开机时间=$bootDisplay, GPS时间=$gpsDisplay, 原始配置=$configStr');
    } catch (e) {
      debugPrint('解析设备配置失败: $e, lorastr=$lorastr');
    }
  }

  /// 加载设备日志记录
  /// [reset] true=刷新（从头加载），false=加载更多（追加）
  Future<void> _loadLogs({bool reset = false}) async {
    if (reset) {
      if (_isLoadingLogs) return;
      setState(() {
        _isLoadingLogs = true;
        _logOffset = 0;
        _hasMore = true;
      });
    } else {
      if (_isLoadingMore || !_hasMore) return;
      setState(() {
        _isLoadingMore = true;
      });
    }

    final deviceId = widget.device['deviceId']?.toString() ?? '';

    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getDeviceLogbyId',
          'info': {
            'deviceId': deviceId,
            'limit': _logLimit,
            'offset': _logOffset,
          },
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
              _logs = parsedLogs;
            } else {
              _logs.addAll(parsedLogs);
            }
            _logOffset = _logs.length;
            _hasMore = parsedLogs.length >= _logLimit;
            _isLoadingLogs = false;
            _isLoadingMore = false;
            _isFromCache = false; // 网络成功，恢复标题
          });
          
          debugPrint('加载日志: ${parsedLogs.length} 条，总计 ${_logs.length} 条');
        } else {
          debugPrint('日志请求错误: ${json['msg']}');
          setState(() {
            _isLoadingLogs = false;
            _isLoadingMore = false;
          });
        }
      } else {
        debugPrint('HTTP错误: ${resp.statusCode}');
        setState(() {
          _isLoadingLogs = false;
          _isLoadingMore = false;
        });
      }
    } catch (e) {
      debugPrint('加载日志失败(网络): $e，尝试从蓝牙缓存加载');
      // 网络失败，标记断网
      setState(() {
        _isFromCache = true;
      });
      // 从蓝牙缓存加载
      await _loadLogsFromBluetoothCache(deviceId, reset: reset);
    }
  }

  /// 从蓝牙缓存加载设备日志（离线回退）
  Future<void> _loadLogsFromBluetoothCache(String deviceId, {bool reset = false}) async {
    try {
      final allBluetoothData = await DBHelper().getBluetoothData();
      debugPrint('[离线日志] 蓝牙缓存共 ${allBluetoothData.length} 条');

      // 筛选当前设备的记录：info的parts[3] == deviceId
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

          final deviceMarker = parts[1]; // 设备标记在parts[1]
          if (deviceMarker == deviceId) {
            // 解析为日志卡片所需格式
            final log = <String, dynamic>{
              'time': jsonData['time'] ?? '',
              'deviceId': deviceId,
              'lorastr': info,
              'upDateDevice': jsonData['upDateDevice'] ?? '',
              'type': parts[0], // type在parts[0]
              'rssi': jsonData['rssi'],
              'snr': jsonData['snr'],
              '_fromCache': true, // 标记来源
            };
            matchedLogs.add(log);
          }
        } catch (_) {
          continue;
        }
      }

      debugPrint('[离线日志] 匹配到 ${matchedLogs.length} 条记录(设备标记=$deviceId)');

      // 同时从LOT缓存表取一条定位记录
      try {
        final lotData = await DBHelper().getDeviceLotByDeviceId(deviceId);
        if (lotData != null) {
          final lotLog = <String, dynamic>{
            'time': lotData['time'] ?? '',
            'deviceId': deviceId,
            'lorastr': lotData['lorastr'] ?? '',
            'upDateDevice': lotData['upDateDevice'] ?? '',
            'type': '1', // LOT记录视为定位类型
            'rssi': null,
            'snr': null,
            '_fromCache': true,
          };
          // 插入到列表开头（最新位置）
          matchedLogs.insert(0, lotLog);
          debugPrint('[离线日志] 从LOT缓存补充1条定位记录');
        }
      } catch (e) {
        debugPrint('[离线日志] LOT缓存加载失败: $e');
      }

      // 按时间降序排序（最新在最上面）
      // 先打印时间用于调试
      for (final log in matchedLogs) {
        final t = log['time']?.toString() ?? '';
        final parsed = _parseCacheTime(t);
        debugPrint('[离线排序] time="$t" => parsed=$parsed');
      }
      matchedLogs.sort((a, b) {
        final ta = _parseCacheTime(a['time']?.toString() ?? '');
        final tb = _parseCacheTime(b['time']?.toString() ?? '');
        return tb.compareTo(ta); // 降序
      });

      setState(() {
        if (reset) {
          _logs = matchedLogs;
        } else {
          _logs.addAll(matchedLogs);
        }
        _hasMore = false; // 缓存数据不分页
        _isLoadingLogs = false;
        _isLoadingMore = false;
      });
    } catch (e) {
      debugPrint('[离线日志] 从蓝牙缓存加载失败: $e');
      setState(() {
        _isLoadingLogs = false;
        _isLoadingMore = false;
      });
    }
  }

  /// 解析缓存时间字符串（格式如 "2026/6/12 13:12:44"）
  DateTime _parseCacheTime(String timeStr) {
    try {
      // 格式: "2026/6/12 13:12:44"，月/日可能是一位数
      final datePart = timeStr.split(' ')[0]; // "2026/6/12"
      final timePart = timeStr.split(' ').length > 1 ? timeStr.split(' ')[1] : '00:00:00'; // "13:12:44"
      final dp = datePart.split('/');
      final tp = timePart.split(':');
      return DateTime(
        int.parse(dp[0]), // year
        dp.length > 1 ? int.parse(dp[1]) : 1, // month
        dp.length > 2 ? int.parse(dp[2]) : 1, // day
        tp.length > 0 ? int.parse(tp[0]) : 0, // hour
        tp.length > 1 ? int.parse(tp[1]) : 0, // minute
        tp.length > 2 ? int.parse(tp[2]) : 0, // second
      );
    } catch (_) {
      return DateTime.fromMillisecondsSinceEpoch(0); // 解析失败放最后
    }
  }

  /// 获取设备ID显示颜色
  Color _getDeviceIdColor(String deviceId) {
    final hash = deviceId.hashCode.abs();
    final colors = [
      const Color(0xFF00BCD4),
      const Color(0xFFFF9800),
      const Color(0xFFE91E63),
      const Color(0xFF4CAF50),
      const Color(0xFF9C27B0),
      const Color(0xFF2196F3),
    ];
    return colors[hash % colors.length];
  }

  String _str(dynamic value) {
    if (value == null || value.toString().isEmpty) return '—';
    return value.toString();
  }

  @override
  Widget build(BuildContext context) {
    final deviceId = _str(widget.device['deviceId']);
    final rename = _str(widget.device['rename']);
    final picurl = widget.device['picurl']?.toString() ?? '';
    
    // LOT数据
    final timeRaw = widget.deviceLot != null ? _str(widget.deviceLot!['time']) : '—';
    // ignore: unused_local_variable

    // 构建显示名称
    String displayName = deviceId;
    if (rename != '—') displayName += '($rename)';

    return Scaffold(
      backgroundColor: const Color(0xFFF5F5F5),
      appBar: AppBar(
        title: Text(_isFromCache ? '设备详情(断网)' : '设备详情'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: RefreshIndicator(
        onRefresh: _refreshDeviceConfig,
        child: SingleChildScrollView(
          physics: const AlwaysScrollableScrollPhysics(),
          child: Column(
            children: [
              // 蓝色头部信息卡片
              _buildHeaderCard(displayName, picurl),
              const SizedBox(height: 16),
              // 功能按钮网格
              _buildFunctionGrid(),
            ],
          ),
        ),
      ),
    );
  }

  /// 打开今日轨迹页面
  void _openTrajectory() {
    final deviceId = _str(widget.device['deviceId']);
    final deviceName = _str(widget.device['rename']);
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => DeviceTrajectoryPage(
          deviceId: deviceId,
          deviceName: deviceName,
        ),
      ),
    );
  }

  /// 显示发送指令对话框
  void _showSendCommandDialog() {
    final deviceId = _str(widget.device['deviceId']);
    final rename = _str(widget.device['rename']);
    String displayName = deviceId;
    if (rename != '—') displayName += '($rename)';

    final commandController = TextEditingController();

    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('发送指令'),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // 目标设备
              const Text(
                '目标设备',
                style: TextStyle(fontSize: 14, color: Colors.grey),
              ),
              const SizedBox(height: 8),
              Container(
                width: double.infinity,
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: Colors.grey[100],
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Text(
                  displayName,
                  style: const TextStyle(fontSize: 15),
                ),
              ),
              const SizedBox(height: 16),
              // 指令内容
              const Text(
                '指令内容',
                style: TextStyle(fontSize: 14, color: Colors.grey),
              ),
              const SizedBox(height: 8),
              TextField(
                controller: commandController,
                decoration: const InputDecoration(
                  hintText: '输入指令...',
                  border: OutlineInputBorder(),
                ),
                maxLines: 3,
              ),
              const SizedBox(height: 16),
              // 快捷指令
              const Text(
                '快捷指令',
                style: TextStyle(fontSize: 14, color: Colors.grey),
              ),
              const SizedBox(height: 8),
              // 第一行：持续跟踪、配置下发
              Row(
                children: [
                  Expanded(
                    child: _buildQuickCommandButton(
                      icon: Icons.satellite,
                      label: '持续跟踪',
                      onTap: () {
                        commandController.text = '{"cmd":"follow","value":"30,5"}';
                      },
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: _buildQuickCommandButton(
                      icon: Icons.settings,
                      label: '配置下发',
                      onTap: () {
                        final configValue = _rawConfigValue.isNotEmpty
                            ? _rawConfigValue
                            : '10,0-24,12-6';
                        commandController.text = '{"cmd":"config","value":"$configValue"}';
                      },
                    ),
                  ),
                ],
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
              var command = commandController.text.trim();
              if (command.isEmpty) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('请输入指令内容')),
                );
                return;
              }
              // 如果是JSON格式，自动添加deviceId
              if (command.startsWith('{') && command.endsWith('}')) {
                try {
                  final json = jsonDecode(command) as Map<String, dynamic>;
                  json['deviceId'] = deviceId;
                  command = jsonEncode(json);
                } catch (_) {}
              }
              _sendCommand(deviceId, command);
              Navigator.pop(context);
            },
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF1976D2),
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
            ),
            child: const Text('发送指令'),
          ),
        ],
      ),
    );
  }

  /// 快捷指令按钮
  Widget _buildQuickCommandButton({
    required IconData icon,
    required String label,
    required VoidCallback onTap,
  }) {
    return InkWell(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
        decoration: BoxDecoration(
          color: Colors.grey[100],
          borderRadius: BorderRadius.circular(8),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(icon, size: 16, color: Colors.blue),
            const SizedBox(width: 4),
            Text(
              label,
              style: const TextStyle(fontSize: 12),
            ),
          ],
        ),
      ),
    );
  }

  /// 发送指令（通过蓝牙发送）
  Future<void> _sendCommand(String deviceId, String command) async {
    try {
      final success = await BluetoothPage.sendBluetoothData(command);
      
      if (success) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('指令发送成功')),
          );
        }
      } else {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('发送失败，请检查蓝牙连接')),
          );
        }
      }
    } catch (e) {
      debugPrint('发送指令失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('发送指令失败')),
        );
      }
    }
  }

  /// 构建日志列表
  Widget _buildLogList() {
    if (_isLoadingLogs && _logs.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }
    
    if (_logs.isEmpty) {
      return const Center(child: Text('暂无数据记录'));
    }

    return NotificationListener<ScrollNotification>(
      onNotification: (scrollInfo) {
        // 滚动到底部时触发加载更多
        if (scrollInfo.metrics.pixels >= scrollInfo.metrics.maxScrollExtent - 100) {
          if (_hasMore && !_isLoadingMore && !_isLoadingLogs) {
            _loadLogs(reset: false);
          }
        }
        return false;
      },
      child: ListView.builder(
        itemCount: _logs.length + (_hasMore ? 1 : 0),
        itemBuilder: (context, index) {
          // 加载更多指示器
          if (index >= _logs.length) {
            return _buildLoadingMoreIndicator();
          }
          
          final log = _logs[index];
          final isEven = index % 2 == 0;
          return _buildLogCard(log, isEven);
        },
      ),
    );
  }

  /// 加载更多指示器
  Widget _buildLoadingMoreIndicator() {
    return Container(
      padding: const EdgeInsets.all(16),
      child: Center(
        child: _isLoadingMore
            ? const SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            : const Text('没有更多数据', style: TextStyle(color: Colors.grey, fontSize: 13)),
      ),
    );
  }

  /// 蓝色头部信息卡片
  Widget _buildHeaderCard(String displayName, String picurl) {
    return Container(
      margin: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: const Color(0xFF1976D2),
        borderRadius: BorderRadius.circular(16),
      ),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 左侧设备图像
            ClipRRect(
              borderRadius: BorderRadius.circular(8),
              child: Container(
                width: 72,
                height: 72,
                decoration: BoxDecoration(
                  border: Border.all(
                    color: Colors.white.withValues(alpha: 0.4),
                    width: 1.5,
                  ),
                ),
                child: picurl.isNotEmpty
                    ? Image.network(
                        picurl,
                        width: 72,
                        height: 72,
                        fit: BoxFit.cover,
                        errorBuilder: (_, __, ___) => _buildDeviceImagePlaceholder(),
                      )
                    : _buildDeviceImagePlaceholder(),
              ),
            ),
            const SizedBox(width: 12),
            // 右侧信息
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  // 设备名称
                  Text(
                    displayName,
                    style: const TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.bold,
                      color: Colors.white,
                    ),
                  ),
                  const SizedBox(height: 8),
                  _buildInfoRowWhite('上报周期', _reportInterval),
                  _buildInfoRowWhite('开机时间', _bootTime),
                  _buildInfoRowWhite('GPS时间', _locationTime),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// 设备图像占位符
  Widget _buildDeviceImagePlaceholder() {
    return Container(
      color: Colors.white.withValues(alpha: 0.15),
      child: const Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(Icons.gps_fixed, size: 28, color: Colors.white),
          SizedBox(height: 2),
          Text('设备图片', style: TextStyle(fontSize: 9, color: Colors.white)),
        ],
      ),
    );
  }

  /// 功能按钮网格
  Widget _buildFunctionGrid() {
    final buttons = <_FunctionButton>[
      _FunctionButton(icon: Icons.my_location, label: '实时定位', color: const Color(0xFFEF5350), onTap: _onRealtimeLocation),
      _FunctionButton(icon: Icons.list_alt, label: '数据列表', color: const Color(0xFF26A69A), onTap: _onDataList),
      _FunctionButton(icon: Icons.route, label: '轨迹地图', color: const Color(0xFFFFA726), onTap: _openTrajectory),
      _FunctionButton(icon: Icons.settings, label: '设备设置', color: const Color(0xFF66BB6A), onTap: _onDeviceSettings),
      _FunctionButton(icon: Icons.notifications_active, label: '报警信息', color: const Color(0xFF42A5F5), onTap: _onAlarmInfo),
      _FunctionButton(icon: Icons.lock_reset, label: '修改密码', color: const Color(0xFF7E57C2), onTap: _onChangePassword),
      _FunctionButton(icon: Icons.fence, label: '电子栅栏', color: const Color(0xFF42A5F5), onTap: _onGeofence),
    ];

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16),
      child: Wrap(
        spacing: 16,
        runSpacing: 20,
        alignment: WrapAlignment.start,
        children: buttons.map((btn) {
          return SizedBox(
            width: 80,
            child: Column(
              children: [
                InkWell(
                  onTap: btn.onTap,
                  borderRadius: BorderRadius.circular(32),
                  child: Container(
                    width: 64,
                    height: 64,
                    decoration: BoxDecoration(
                      color: btn.color,
                      shape: BoxShape.circle,
                    ),
                    child: Icon(btn.icon, size: 30, color: Colors.white),
                  ),
                ),
                const SizedBox(height: 8),
                Text(
                  btn.label,
                  style: const TextStyle(fontSize: 13, color: Colors.black87),
                  textAlign: TextAlign.center,
                ),
              ],
            ),
          );
        }).toList(),
      ),
    );
  }

  // --- 功能按钮点击事件 ---

  /// 设备配置信息区域（getDeviceConfigAll返回的全部属性）
  Widget _buildConfigSection() {
    // 过滤掉已在头部显示的字段
    final skipKeys = {'deviceId'};
    final entries = _configAttributes.entries
        .where((e) => !skipKeys.contains(e.key))
        .toList();

    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 标题
          Container(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
            child: Row(
              children: [
                Icon(Icons.settings_suggest, size: 18, color: Colors.blue[700]),
                const SizedBox(width: 6),
                const Text(
                  '设备配置信息',
                  style: TextStyle(fontSize: 15, fontWeight: FontWeight.bold, color: Colors.black87),
                ),
              ],
            ),
          ),
          const Divider(height: 1, indent: 16, endIndent: 16),
          // 属性列表
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            child: Column(
              children: entries.map((e) {
                final key = e.key;
                final value = e.value?.toString() ?? '—';
                return Padding(
                  padding: const EdgeInsets.only(bottom: 6),
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      SizedBox(
                        width: 110,
                        child: Text(
                          key,
                          style: TextStyle(fontSize: 13, color: Colors.grey[600]),
                        ),
                      ),
                      Expanded(
                        child: Text(
                          value,
                          style: const TextStyle(fontSize: 13, color: Colors.black87),
                        ),
                      ),
                    ],
                  ),
                );
              }).toList(),
            ),
          ),
          const SizedBox(height: 8),
        ],
      ),
    );
  }

  /// 实时定位：通过getDeviceLotById获取最新定位坐标并跳转到定位详情
  void _onRealtimeLocation() async {
    final deviceId = _str(widget.device['deviceId']);
    final rename = _str(widget.device['rename']);
    if (deviceId.isEmpty || deviceId == '—') return;

    // 显示加载指示
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (_) => const Center(
        child: CircularProgressIndicator(),
      ),
    );

    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getDeviceLotById',
          'info': {'deviceId': deviceId, 'wechatid': globalWechatId},
        }),
      );

      debugPrint('[实时定位] getDeviceLotById响应: ${resp.statusCode}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          // getDeviceLotById返回单条Map
          final rawData = json['data'];
          Map<String, dynamic>? parsed;
          if (rawData is Map<String, dynamic>) {
            parsed = _parseOtsRow(rawData);
          } else if (rawData is List && rawData.isNotEmpty) {
            parsed = _parseOtsRow(rawData.first);
          }

          if (parsed != null) {
            final lorastr = parsed['lorastr']?.toString() ?? '';
            final time = parsed['time']?.toString() ?? '';
            debugPrint('[实时定位] lorastr=$lorastr, time=$time');

            // 从lorastr解析GPS坐标：格式 "type|deviceMarker|lat,lng|value"
            final parts = lorastr.split('|');
            if (parts.length >= 3) {
              final gpsStr = parts[2]; // "lat,lng"
              final gpsParts = gpsStr.split(',');
              if (gpsParts.length >= 2) {
                final lat = double.tryParse(gpsParts[0].trim());
                final lng = double.tryParse(gpsParts[1].trim());
                if (lat != null && lng != null && (lat != 0 || lng != 0)) {
                  // 关闭加载
                  if (mounted) Navigator.pop(context);
                  // 跳转到定位详情页
                  Navigator.push(
                    context,
                    MaterialPageRoute(
                      builder: (_) => DeviceLogMapPage(
                        latitude: lat,
                        longitude: lng,
                        time: time,
                        deviceId: deviceId,
                        type: parts[0], // type在parts[0]
                        deviceName: rename != '—' ? '$deviceId ($rename)' : deviceId,
                      ),
                    ),
                  );
                  return;
                }
              }
            }
            // 坐标解析失败
            if (mounted) Navigator.pop(context);
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('定位坐标解析失败，该设备可能尚未上报GPS数据')),
            );
          } else {
            if (mounted) Navigator.pop(context);
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('未获取到该设备的定位数据')),
            );
          }
        } else {
          debugPrint('[实时定位] 请求错误: ${json['msg']}');
          if (mounted) Navigator.pop(context);
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('获取定位失败: ${json['msg']}')),
          );
        }
      } else {
        if (mounted) Navigator.pop(context);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('网络请求失败: HTTP ${resp.statusCode}')),
        );
      }
    } catch (e) {
      debugPrint('[实时定位] 请求异常: $e');
      if (mounted) Navigator.pop(context);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('获取实时定位失败: $e')),
      );
    }
  }

  /// 解析OTS返回的原始行数据
  Map<String, dynamic> _parseOtsRow(dynamic rawRow) {
    final row = rawRow as Map<String, dynamic>;
    final parsed = <String, dynamic>{};
    final pkList = row['primaryKey'] as List<dynamic>? ?? [];
    for (final pk in pkList) {
      final pkMap = pk as Map<String, dynamic>;
      parsed[pkMap['name'] as String] = pkMap['value'];
    }
    final attrList = row['attributes'] as List<dynamic>? ?? [];
    for (final attr in attrList) {
      final attrMap = attr as Map<String, dynamic>;
      parsed[attrMap['columnName'] as String] = attrMap['columnValue'];
    }
    return parsed;
  }

  void _onDataList() {
    // 打开设备记录页面
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => DeviceRecordPage(
          device: widget.device,
          deviceLot: widget.deviceLot,
        ),
      ),
    );
  }

  void _onDeviceSettings() {
    final deviceId = _str(widget.device['deviceId']);
    final deviceName = _str(widget.device['rename']);
    final deviceKey = _str(widget.device['device_key']);
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => DeviceDtuCommandPage(
          deviceId: deviceId,
          deviceName: deviceName,
          deviceKey: deviceKey,
        ),
      ),
    );
  }

  void _onAlarmInfo() {
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('报警信息功能开发中...')),
    );
  }

  void _onChangePassword() {
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('修改密码功能开发中...')),
    );
  }

  void _onGeofence() {
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('电子栅栏功能开发中...')),
    );
  }

  /// 复制配置数据到剪贴板
  void _copyConfigToClipboard() {
    if (_rawLorastr.isEmpty) return;
    Clipboard.setData(ClipboardData(text: _rawLorastr));
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text('配置数据已复制: $_rawLorastr'),
        duration: const Duration(seconds: 2),
      ),
    );
  }

  /// 白色文字信息行
  Widget _buildInfoRowWhite(String label, String value, {Color? valueColor}) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 4),
      child: Row(
        children: [
          Text(
            '$label  ',
            style: TextStyle(
              fontSize: 14,
              color: Colors.white.withValues(alpha: 0.8),
            ),
          ),
          Text(
            value,
            style: TextStyle(
              fontSize: 14,
              color: valueColor ?? Colors.white,
              fontWeight: FontWeight.w500,
            ),
          ),
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
        return (label: '对时', color: const Color(0xFF4CAF50));
      case 5:
        return (label: '跟踪', color: const Color(0xFFFF9800));
      case 6:
        return (label: '配置', color: const Color(0xFF9C27B0));
      default:
        return (label: type.toString(), color: Colors.grey);
    }
  }

  /// 日志卡片
  Widget _buildLogCard(Map<String, dynamic> log, bool isEven) {
    final time = _str(log['time']);
    final logDeviceId = _str(log['deviceId']);
    final lorastr = _str(log['lorastr']);
    final upDateDevice = _str(log['upDateDevice']);
    final typeInfo = _getTypeInfo(log['type']);
    final typeStr = log['type']?.toString() ?? '';
    
    // 直接取 rssi 和 snr 字段
    final rssiVal = log['rssi'];
    final snrVal = log['snr'];
    final rssi = rssiVal != null ? '${rssiVal}dBm' : '—';
    final snr = snrVal != null ? snrVal.toString() : '—';

    // type 1(定位) 或 5(跟踪) 有GPS坐标，可点击查看地图
    final bool hasGps = typeStr == '1' || typeStr == '5';

    Widget card = Container(
      color: isEven ? const Color(0xFFE8F5E9) : const Color(0xFFE3F2FD),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 第一行：时间 | upDateDevice
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
          // 第二行：类型标签 + lorastr + 箭头
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
              const SizedBox(width: 8),
              const Icon(
                Icons.arrow_forward_ios,
                size: 14,
                color: Colors.grey,
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
        child: Stack(
          children: [
            card,
            // 右上角地图图标提示

          ],
        ),
      );
    }
    return card;
  }

  /// 打开日志定位地图
  void _openLogMap(Map<String, dynamic> log, String lorastr, String deviceId, String type) {
    // 从 lorastr 解析 GPS 坐标：格式 "type|deviceMarker|lat,lng|value"
    final parts = lorastr.split('|');
    if (parts.length < 3) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('该记录无GPS坐标信息')),
      );
      return;
    }
    final gpsStr = parts[2]; // "lat,lng"
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
        const SnackBar(content: Text('GPS坐标无效（为0或格式错误）')),
      );
      return;
    }
    final time = _str(log['time']);
    final rename = _str(widget.device['rename']);

    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => DeviceLogMapPage(
          latitude: lat,
          longitude: lng,
          time: time,
          deviceId: deviceId,
          type: type,
          deviceName: rename != '—' ? '$deviceId ($rename)' : deviceId,
        ),
      ),
    );
  }
}

/// 设备详情 TAB 页（作为底部导航第一个TAB）
/// 监听全局选中设备变化，自动刷新显示
class DeviceDetailTabPage extends StatefulWidget {
  final VoidCallback? onSwitchTab;
  const DeviceDetailTabPage({super.key, this.onSwitchTab});

  @override
  State<DeviceDetailTabPage> createState() => _DeviceDetailTabPageState();
}

class _DeviceDetailTabPageState extends State<DeviceDetailTabPage> {
  @override
  void initState() {
    super.initState();
    // 监听设备选择变化
    deviceSelectedNotifier.addListener(_onDeviceChanged);
  }

  @override
  void dispose() {
    deviceSelectedNotifier.removeListener(_onDeviceChanged);
    super.dispose();
  }

  void _onDeviceChanged() {
    if (mounted) {
      setState(() {});
    }
  }

  @override
  Widget build(BuildContext context) {
    final device = globalSelectedDevice;
    final deviceLot = globalSelectedDeviceLot;

    // 没有选中设备时显示占位
    if (device == null || device.isEmpty) {
      return Scaffold(
        backgroundColor: const Color(0xFFF5F5F5),
        appBar: AppBar(
          title: const Text('设备详情'),
          backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        ),
        body: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.devices_other, size: 80, color: Colors.grey[300]),
              const SizedBox(height: 16),
              Text(
                '请先在设备管理中选择一个设备',
                style: TextStyle(fontSize: 16, color: Colors.grey[500]),
              ),
              const SizedBox(height: 24),
              ElevatedButton.icon(
                onPressed: widget.onSwitchTab,
                icon: const Icon(Icons.devices),
                label: const Text('去设备管理'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.blue,
                  foregroundColor: Colors.white,
                  padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
                ),
              ),
            ],
          ),
        ),
      );
    }

    // 有选中设备时显示详情
    return DeviceDetailPage(
      device: device,
      deviceLot: deviceLot,
    );
  }
}