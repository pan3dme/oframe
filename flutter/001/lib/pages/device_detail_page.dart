import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';
import 'device_log_map_page.dart';
import 'device_trajectory_page.dart';
import 'bluetooth_page.dart';

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
  bool _isBluetoothConnected = false; // 蓝牙连接状态
  bool _isFromCache = false; // 标记是否使用缓存数据（断网）

  // FC地址
  static const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

  @override
  void initState() {
    super.initState();
    _checkBluetoothConnection();
    _loadLogs(reset: true);
    _loadDeviceConfig();
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
          'info': {'limit': 99},
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

            // 找到当前设备，解析显示
            if (configDeviceId == deviceId) {
              final lorastr = parsed['lorastr']?.toString() ?? '';
              _parseConfigLorastr(lorastr);
              debugPrint('设备配置匹配: deviceId=$deviceId, lorastr=$lorastr');
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
    try {
      final cachedConfig = await DBHelper().getDeviceConfig(deviceId);
      if (cachedConfig != null) {
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
        debugPrint('[离线配置] 表缓存更新(cfg=$cfgTime >= bt=$btTime)，使用表缓存数据');
      }
    } else if (bluetoothLorastr != null) {
      _parseConfigLorastr(bluetoothLorastr);
      debugPrint('[离线配置] 仅有蓝牙数据，使用蓝牙数据');
    } else if (configLorastr != null) {
      _parseConfigLorastr(configLorastr);
      debugPrint('[离线配置] 仅有表缓存数据，使用表缓存数据');
    } else {
      debugPrint('[离线配置] 两个缓存都没有该设备的配置数据');
    }
  }

  /// 解析设备配置lorastr
  /// 格式: type|deviceId|30,12-6,12-4|...
  /// 第三段用逗号分隔: 上报周期(分钟),开机时间(起始-持续),定位时间(起始-持续)
  void _parseConfigLorastr(String lorastr) {
    if (lorastr.isEmpty) return;
    try {
      final parts = lorastr.split('|');
      if (parts.length < 3) return;
      final configStr = parts[2]; // "30,12-6,12-4"
      final configs = configStr.split(',');
      if (configs.length < 3) return;

      // 上报周期
      final interval = configs[0]; // "30"
      final intervalDisplay = '$interval分钟';

      // 开机时间: "12-6" → 12:00-18:00
      final bootParts = configs[1].split('-'); // ["12", "6"]
      final bootStart = int.tryParse(bootParts[0]) ?? 0;
      final bootDuration = int.tryParse(bootParts[1]) ?? 0;
      final bootEnd = bootStart + bootDuration;
      final bootDisplay = '${bootStart.toString().padLeft(2, '0')}:00-${bootEnd.toString().padLeft(2, '0')}:00';

      // 定位时间: "12-4" → 12:00-16:00
      final locParts = configs[2].split('-'); // ["12", "4"]
      final locStart = int.tryParse(locParts[0]) ?? 0;
      final locDuration = int.tryParse(locParts[1]) ?? 0;
      final locEnd = locStart + locDuration;
      final locDisplay = '${locStart.toString().padLeft(2, '0')}:00-${locEnd.toString().padLeft(2, '0')}:00';

      setState(() {
        _reportInterval = intervalDisplay;
        _bootTime = bootDisplay;
        _locationTime = locDisplay;
      });
      debugPrint('解析配置: 上报周期=$intervalDisplay, 开机时间=$bootDisplay, 定位时间=$locDisplay');
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
    final deviceKey = _str(widget.device['device_key']);
    final linkCowSheepId = _str(widget.device['link_cowsheep_id']);
    final productKey = _str(widget.device['ProductKey']);
    final picurl = widget.device['picurl']?.toString() ?? '';
    
    // LOT数据
    final timeRaw = widget.deviceLot != null ? _str(widget.deviceLot!['time']) : '—';

    // 构建显示名称
    String displayName = deviceId;
    if (rename != '—') displayName += '($rename)';

    return Scaffold(
      backgroundColor: const Color(0xFFF5F5F5),
      appBar: AppBar(
        title: Text(_isFromCache ? '设备详情(断网)' : '设备详情'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: Column(
        children: [
          // 固定头部区域
          Container(
            margin: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: const Color(0xFF1976D2),
              borderRadius: BorderRadius.circular(16),
            ),
            child: Stack(
              children: [
                Padding(
                  padding: const EdgeInsets.all(16),
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      // 左侧设备图像区域
                      ClipRRect(
                        borderRadius: BorderRadius.circular(12),
                        child: Container(
                          width: 80,
                          height: 80,
                          decoration: BoxDecoration(
                            border: Border.all(
                              color: Colors.white.withOpacity(0.4),
                              width: 2,
                            ),
                          ),
                          child: picurl.isNotEmpty
                              ? Image.network(
                                  picurl,
                                  width: 80,
                                  height: 80,
                                  fit: BoxFit.cover,
                                  errorBuilder: (_, __, ___) => Container(
                                    color: Colors.white.withValues(alpha: 0.2),
                                    child: const Icon(
                                      Icons.gps_fixed,
                                      size: 40,
                                      color: Colors.white,
                                    ),
                                  ),
                                )
                              : Container(
                                  color: Colors.white.withValues(alpha: 0.2),
                                  child: const Icon(
                                    Icons.gps_fixed,
                                    size: 40,
                                    color: Colors.white,
                                  ),
                                ),
                        ),
                      ),
                      const SizedBox(width: 16),
                      // 右侧信息区域
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              'id: $displayName',
                              style: const TextStyle(
                                fontSize: 18,
                                fontWeight: FontWeight.bold,
                                color: Colors.white,
                              ),
                            ),
                            const SizedBox(height: 8),

                            _buildInfoRowWhite(
                              '绑定牛羊',
                              linkCowSheepId == '—' ? '未绑定' : linkCowSheepId,
                              valueColor: linkCowSheepId == '—' 
                                  ? const Color(0xFF66BB6A) 
                                  : Colors.white,
                            ),
                            _buildInfoRowWhite('上报周期', _reportInterval),
                            _buildInfoRowWhite('开机时间', _bootTime),
                            _buildInfoRowWhite('定位时间', _locationTime),
                            _buildInfoRowWhite('上次换电', '—'),
                            const SizedBox(height: 8),

                          ],
                        ),
                      ),
                    ],
                  ),
                ),
                // 右上角操作按钮
                Positioned(
                  right: 16,
                  bottom: 16,
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      // 查看轨迹按钮
                      InkWell(
                        onTap: () => _openTrajectory(),
                        child: Container(
                          width: 36,
                          height: 36,
                          decoration: BoxDecoration(
                            color: const Color(0xFF2196F3),
                            shape: BoxShape.circle,
                          ),
                          child: const Icon(
                            Icons.route,
                            size: 20,
                            color: Colors.white,
                          ),
                        ),
                      ),
                      const SizedBox(width: 8),
                      // 指令按钮
                      InkWell(
                        onTap: () {
                          if (_isBluetoothConnected) {
                            _showSendCommandDialog();
                          } else {
                            ScaffoldMessenger.of(context).showSnackBar(
                              const SnackBar(content: Text('请连接蓝牙')),
                            );
                          }
                        },
                        child: Container(
                          width: 36,
                          height: 36,
                          decoration: BoxDecoration(
                            color: _isBluetoothConnected
                                ? const Color(0xFF4CAF50)
                                : Colors.grey.withOpacity(0.5),
                            shape: BoxShape.circle,
                          ),
                          child: Icon(
                            Icons.send,
                            size: 20,
                            color: Colors.white,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),

              ],
            ),
          ),

          // 数据记录区域（可滚动 + 下拉刷新 + 上拉加载）
          Expanded(
            child: Container(
              margin: const EdgeInsets.symmetric(horizontal: 16),
              decoration: BoxDecoration(
                color: Colors.white,
                borderRadius: BorderRadius.circular(12),
              ),
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
                        commandController.text = '{"cmd":"config","value":"10,0-24,12-6"}';
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
              color: Colors.white.withOpacity(0.8),
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

  /// 操作按钮
  Widget _buildActionButton(IconData icon, Color color, VoidCallback onTap) {
    return InkWell(
      onTap: onTap,
      child: Container(
        width: 36,
        height: 36,
        decoration: BoxDecoration(
          color: color.withOpacity(0.2),
          shape: BoxShape.circle,
        ),
        child: Icon(icon, size: 20, color: color),
      ),
    );
  }

  /// 底部小图标
  Widget _buildSmallIcon(IconData icon, VoidCallback onTap) {
    return InkWell(
      onTap: onTap,
      child: Container(
        width: 32,
        height: 32,
        decoration: BoxDecoration(
          color: Colors.white.withOpacity(0.2),
          shape: BoxShape.circle,
        ),
        child: Icon(icon, size: 18, color: Colors.white),
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

    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => DeviceLogMapPage(
          latitude: lat,
          longitude: lng,
          time: time,
          deviceId: deviceId,
          type: type,
        ),
      ),
    );
  }
}