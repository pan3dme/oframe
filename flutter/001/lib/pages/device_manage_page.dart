import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';
import 'device_detail_page.dart'; // 导入设备详情页面

/// FC 地址常量
const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

class DeviceManagePage extends StatefulWidget {
  const DeviceManagePage({super.key});

  @override
  State<DeviceManagePage> createState() => _DeviceManagePageState();
}

class _DeviceManagePageState extends State<DeviceManagePage> {
  List<Map<String, dynamic>> _data = [];
  Map<String, Map<String, dynamic>> _deviceLotMap = {}; // 设备LOT数据映射
  Map<String, Map<String, dynamic>> _deviceSyncMap = {}; // 设备对时表数据映射
  Map<String, String> _bluetoothTimeMap = {}; // 蓝牙缓存中每个设备的最新时间
  Map<String, String> _bluetoothTypeMap = {}; // 蓝牙缓存中每个设备最新记录的类型
  Map<String, Map<String, dynamic>> _deviceConfigMap = {}; // 设备配置数据映射(getDeviceConfigAll)
  Map<String, List<int>> _deviceWorkHoursMap = {}; // 设备开机时间映射: deviceId -> [startHour, duration]
  Map<String, int> _deviceIntervalMap = {}; // 设备上报周期映射(分钟): deviceId -> intervalMinutes
  String _loadStatus = '';
  bool _isLoading = true;
  bool _isFromCache = false; // 标记是否使用缓存数据

  
  // 编辑表单控制器
  final TextEditingController _deviceKeyController = TextEditingController();
  final TextEditingController _renameController = TextEditingController();
  


  @override
  void initState() {
    super.initState();
    _loadData();
  }
  



  Future<void> _loadData() async {
    // 先尝试加载缓存数据
    await _loadFromCache();
    
    // 然后尝试从网络获取最新数据
    await _loadFromNetwork();
    
    // 加载设备LOT数据
    await _loadDeviceLotData();
    
    // 加载设备对时表数据
    await _loadDeviceSyncData();
    
    // 加载蓝牙缓存中的设备时间（离线模式补充）
    await _loadBluetoothTimeData();
    
    // 加载设备配置数据（获取开机时间）
    await _loadDeviceConfigData();
  }

  /// 从缓存加载数据
  Future<void> _loadFromCache() async {
    try {
      final cachedData = await DBHelper().getDevices();
      if (cachedData.isNotEmpty) {
        setState(() {
          _data = cachedData;
          _filterVisibleDevices(); // 过滤visible=true
          _sortDevices(); // 排序
          _isLoading = false;
          // 不设置 _isFromCache 和 _loadStatus，等网络请求结果再决定
        });
        debugPrint('从缓存加载设备数据: ${cachedData.length} 条');
      }
    } catch (e) {
      debugPrint('加载缓存失败: $e');
    }
  }

  /// 加载设备LOT数据
  Future<void> _loadDeviceLotData() async {
    try {
      // 从缓存加载LOT数据
      final cachedLotData = await DBHelper().getDeviceLot();
      if (cachedLotData.isNotEmpty) {
        final lotMap = <String, Map<String, dynamic>>{};
        for (final lot in cachedLotData) {
          final deviceId = lot['deviceId'] as String;
          lotMap[deviceId] = lot;
        }
        setState(() {
          _deviceLotMap = lotMap;
        });
        debugPrint('从缓存加载设备LOT数据: ${cachedLotData.length} 条');
      }
      
      // 从网络加载LOT数据
      await _loadDeviceLotFromNetwork();
    } catch (e) {
      debugPrint('加载设备LOT数据失败: $e');
    }
  }

  /// 从网络加载设备LOT数据
  Future<void> _loadDeviceLotFromNetwork() async {
    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getDeviceLotRefreshAll',
          'info': {
            'limit': 99,
          }}),
      );

      debugPrint('设备LOT FC 响应状态: ${resp.statusCode}');
      debugPrint('设备LOT FC 响应体: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>;
          final parsedData = rawRows.map(_parseOtsRow).toList();
          
          // 保存到缓存
          await DBHelper().saveDeviceLot(parsedData);
          
          // 构建LOT数据映射
          final lotMap = <String, Map<String, dynamic>>{};
          for (final lot in parsedData) {
            final deviceId = lot['deviceId'] as String;
            lotMap[deviceId] = lot;
          }
          
          setState(() {
            _deviceLotMap = lotMap;
          });
          
          debugPrint('从网络加载设备LOT数据: ${parsedData.length} 条，已缓存');
        } else {
          debugPrint('设备LOT请求错误: ${json['msg']} ${json['error'] ?? ''}');
        }
      } else {
        debugPrint('设备LOT HTTP ${resp.statusCode}');
      }
    } catch (e) {
      debugPrint('设备LOT表请求失败: $e');
    }
  }

  /// 加载设备对时表数据
  Future<void> _loadDeviceSyncData() async {
    try {
      // 从缓存加载对时表数据
      final cachedSyncData = await DBHelper().getDeviceSync();
      if (cachedSyncData.isNotEmpty) {
        final syncMap = <String, Map<String, dynamic>>{};
        for (final sync in cachedSyncData) {
          final deviceId = sync['deviceId'] as String;
          syncMap[deviceId] = sync;
        }
        setState(() {
          _deviceSyncMap = syncMap;
        });
        debugPrint('从缓存加载设备对时表数据: ${cachedSyncData.length} 条');
      }
      
      // 从网络加载对时表数据
      await _loadDeviceSyncFromNetwork();
    } catch (e) {
      debugPrint('加载设备对时表数据失败: $e');
    }
  }

  /// 从网络加载设备对时表数据
  Future<void> _loadDeviceSyncFromNetwork() async {
    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getDevicesyncAll',
          'info': {
            'limit': 99,
          }}),
      );

      debugPrint('设备对时表 FC 响应状态: ${resp.statusCode}');
      debugPrint('设备对时表 FC 响应体: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>;
          final parsedData = rawRows.map(_parseOtsRow).toList();
          
          // 保存到缓存
          await DBHelper().saveDeviceSync(parsedData);
          
          // 构建对时表数据映射
          final syncMap = <String, Map<String, dynamic>>{};
          for (final sync in parsedData) {
            final deviceId = sync['deviceId'] as String;
            syncMap[deviceId] = sync;
          }
          
          setState(() {
            _deviceSyncMap = syncMap;
          });
          
          debugPrint('从网络加载设备对时表数据: ${parsedData.length} 条，已缓存');
        } else {
          debugPrint('设备对时表请求错误: ${json['msg']} ${json['error'] ?? ''}');
        }
      } else {
        debugPrint('设备对时表 HTTP ${resp.statusCode}');
      }
    } catch (e) {
      debugPrint('设备对时表请求失败: $e');
    }
  }

  /// 从蓝牙缓存加载每个设备的最新时间（筛选type 1/2/5）
  Future<void> _loadBluetoothTimeData() async {
    try {
      final allBluetoothData = await DBHelper().getBluetoothData();
      if (allBluetoothData.isEmpty) return;

      // 按设备标记分组，找每个设备的最新时间和类型
      final deviceTimeMap = <String, DateTime>{};
      final deviceTimeRawMap = <String, String>{};
      final deviceTypeMap = <String, String>{};

      for (final item in allBluetoothData) {
        final dataStr = item['data'] as String?;
        if (dataStr == null || dataStr.isEmpty) continue;

        try {
          final jsonData = jsonDecode(dataStr) as Map<String, dynamic>;
          final info = jsonData['info'] as String? ?? '';
          if (!info.contains('|')) continue;

          final parts = info.split('|');
          if (parts.length < 2) continue;

          final type = parts[0]; // 类型: 1=GPS, 2=对时, 5=跟踪
          final deviceMarker = parts[1]; // 设备标记

          // 只处理类型1、2、5
          if (type != '1' && type != '2' && type != '5') continue;

          final timeStr = jsonData['time'] as String? ?? '';
          if (timeStr.isEmpty) continue;

          final dt = _parseTimeToDateTime(timeStr);
          if (dt == null) continue;

          // 保留最新时间
          if (!deviceTimeMap.containsKey(deviceMarker) || dt.isAfter(deviceTimeMap[deviceMarker]!)) {
            deviceTimeMap[deviceMarker] = dt;
            deviceTimeRawMap[deviceMarker] = timeStr;
            deviceTypeMap[deviceMarker] = type; // 记录类型
          }
        } catch (_) {
          continue;
        }
      }

      debugPrint('[蓝牙时间] 从缓存中提取到 ${deviceTimeRawMap.length} 个设备的时间');

      setState(() {
        _bluetoothTimeMap = deviceTimeRawMap;
        _bluetoothTypeMap = deviceTypeMap;
      });
    } catch (e) {
      debugPrint('[蓝牙时间] 加载失败: $e');
    }
  }

  /// 加载设备配置数据（获取开机时间）
  Future<void> _loadDeviceConfigData() async {
    try {
      // 从缓存加载配置数据
      final cachedConfigs = await DBHelper().getAllDeviceConfig();
      if (cachedConfigs.isNotEmpty) {
        final configMap = <String, Map<String, dynamic>>{};
        final workHoursMap = <String, List<int>>{};
        final intervalMap = <String, int>{};
        for (final config in cachedConfigs) {
          final deviceId = config['deviceId']?.toString() ?? '';
          if (deviceId.isNotEmpty) {
            configMap[deviceId] = config;
            final lorastr = config['lorastr']?.toString() ?? '';
            // 解析开机时间
            final workHours = _parseWorkHours(lorastr);
            if (workHours != null) {
              workHoursMap[deviceId] = workHours;
            }
            // 解析上报周期
            final interval = _parseReportInterval(lorastr);
            if (interval != null) {
              intervalMap[deviceId] = interval;
            }
          }
        }
        setState(() {
          _deviceConfigMap = configMap;
          _deviceWorkHoursMap = workHoursMap;
          _deviceIntervalMap = intervalMap;
        });
        debugPrint('从缓存加载设备配置数据: ${cachedConfigs.length} 条');
      }
      
      // 从网络加载配置数据
      await _loadDeviceConfigFromNetwork();
    } catch (e) {
      debugPrint('加载设备配置数据失败: $e');
    }
  }

  /// 从网络加载设备配置数据
  Future<void> _loadDeviceConfigFromNetwork() async {
    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getDeviceConfigAll',
          'info': {'limit': 99},
        }),
      );

      debugPrint('设备配置FC响应状态: ${resp.statusCode}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>? ?? [];
          final configMap = <String, Map<String, dynamic>>{};
          final workHoursMap = <String, List<int>>{};
          final intervalMap = <String, int>{};
          
          for (final rawRow in rawRows) {
            final parsed = _parseOtsRow(rawRow);
            final deviceId = parsed['deviceId']?.toString() ?? '';
            if (deviceId.isNotEmpty) {
              await DBHelper().saveDeviceConfig(deviceId, parsed);
              configMap[deviceId] = parsed;
              final lorastr = parsed['lorastr']?.toString() ?? '';
              // 解析开机时间
              final workHours = _parseWorkHours(lorastr);
              if (workHours != null) {
                workHoursMap[deviceId] = workHours;
              }
              // 解析上报周期
              final interval = _parseReportInterval(lorastr);
              if (interval != null) {
                intervalMap[deviceId] = interval;
              }
            }
          }
          
          setState(() {
            _deviceConfigMap = configMap;
            _deviceWorkHoursMap = workHoursMap;
            _deviceIntervalMap = intervalMap;
          });
          
          debugPrint('从网络加载设备配置数据: ${rawRows.length} 条，已缓存');
        } else {
          debugPrint('设备配置请求错误: ${json['msg']}');
        }
      }
    } catch (e) {
      debugPrint('设备配置请求失败: $e');
    }
  }

  /// 解析lorastr中的开机时间
  /// 格式: type|deviceId|30,12-6,12-4|...
  /// 第三段按逗号分割: 上报周期,开机时间(起始-持续),定位时间(起始-持续)
  /// 返回 [startHour, duration] 或 null
  List<int>? _parseWorkHours(String lorastr) {
    if (lorastr.isEmpty) return null;
    try {
      final parts = lorastr.split('|');
      if (parts.length < 3) return null;
      final configStr = parts[2]; // "30,12-6,12-4"
      final configs = configStr.split(',');
      if (configs.length < 3) return null;
      
      // 开机时间: "12-6" -> startHour=12, duration=6
      final bootParts = configs[1].split('-');
      if (bootParts.length < 2) return null;
      final startHour = int.tryParse(bootParts[0]);
      final duration = int.tryParse(bootParts[1]);
      if (startHour == null || duration == null) return null;
      
      return [startHour, duration];
    } catch (e) {
      debugPrint('解析开机时间失败: $e, lorastr=$lorastr');
    }
    return null;
  }

  /// 解析lorastr中的上报周期（分钟）
  /// 格式: type|deviceId|30,12-6,12-4|...
  /// 第三段第一个值即上报周期，如"30"表示30分钟
  int? _parseReportInterval(String lorastr) {
    if (lorastr.isEmpty) return null;
    try {
      final parts = lorastr.split('|');
      if (parts.length < 3) return null;
      final configStr = parts[2]; // "30,12-6,12-4"
      final configs = configStr.split(',');
      if (configs.isEmpty) return null;
      return int.tryParse(configs[0]);
    } catch (e) {
      debugPrint('解析上报周期失败: $e, lorastr=$lorastr');
    }
    return null;
  }

  /// 根据上报周期计算时间颜色级别
  /// 返回: 0=绿色(≤1周期), 1=红色(1~2周期), 2=灰色(>2周期)
  int _getTimeColorLevel(String timeRaw, String deviceId) {
    if (timeRaw == '—' || timeRaw.isEmpty) return 2;
    final intervalMinutes = _deviceIntervalMap[deviceId];
    if (intervalMinutes == null || intervalMinutes <= 0) return 0; // 无周期数据，默认绿色
    
    try {
      if (timeRaw.contains(' ')) {
        final parts = timeRaw.split(' ');
        if (parts.length >= 2) {
          final dateParts = parts[0].replaceAll('/', '-').split('-');
          final timeParts = parts[1].split(':');
          if (dateParts.length >= 3 && timeParts.length >= 2) {
            final deviceTime = DateTime(
              int.parse(dateParts[0]),
              int.parse(dateParts[1]),
              int.parse(dateParts[2]),
              int.parse(timeParts[0]),
              int.parse(timeParts[1]),
              timeParts.length >= 3 ? int.parse(timeParts[2]) : 0,
            );
            final diffMinutes = DateTime.now().difference(deviceTime).inMinutes;
            if (diffMinutes <= intervalMinutes) return 0; // ≤1周期：绿色
            if (diffMinutes <= intervalMinutes * 2) return 1; // 1~2周期：红色
            return 2; // >2周期：灰色
          }
        }
      }
    } catch (e) {
      debugPrint('计算时间颜色失败: $e');
    }
    return 2; // 默认灰色
  }

  /// 判断设备是否在工作时间内
  bool _isDeviceWorking(String deviceId) {
    final workHours = _deviceWorkHoursMap[deviceId];
    if (workHours == null) return true; // 没有配置数据，默认显示为工作中
    
    final startHour = workHours[0];
    final duration = workHours[1];
    final now = DateTime.now();
    final currentHour = now.hour;
    
    // 计算工作结束时间
    final endHour = (startHour + duration) % 24;
    
    // 判断当前时间是否在工作时间内
    if (startHour < endHour) {
      // 不跨天，例如 12-18
      return currentHour >= startHour && currentHour < endHour;
    } else {
      // 跨天，例如 20-6
      return currentHour >= startHour || currentHour < endHour;
    }
  }

  /// 从网络加载数据
  Future<void> _loadFromNetwork() async {
    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getDeviceTaleAll',
          'info': {
            'limit': 99,
          },}),
      );

      debugPrint('FC getDeviceTaleAll响应状态: ${resp.statusCode}');
      debugPrint('FC getDeviceTaleAll响应体: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>;
          final parsedData = rawRows.map(_parseOtsRow).toList();
          
          // 保存到缓存
          await DBHelper().saveDevices(parsedData);
          
          setState(() {
            _data = parsedData;
            _filterVisibleDevices(); // 过滤visible=true
            _sortDevices(); // 排序
            _isLoading = false;
          });
          
          debugPrint('从网络加载设备数据: ${parsedData.length} 条，已缓存');
        } else {
          // 网络请求返回错误
          setState(() {
            _loadStatus = '请求错误: ${json['msg']} ${json['error'] ?? ''}';
            _isLoading = false;
            // 保持_isFromCache状态不变
          });
        }
      } else {
        setState(() {
          _loadStatus = 'HTTP ${resp.statusCode}';
          _isLoading = false;
        });
      }
    } catch (e) {
      debugPrint('设备表请求失败: $e');
      // 网络请求失败，如果有缓存数据则不显示错误
      if (_data.isEmpty) {
        setState(() {
          _loadStatus = '连接失败: $e';
          _isLoading = false;
        });
      } else {
        // 有缓存数据，网络失败，显示缓存模式
        setState(() {
          _isFromCache = true;
          _loadStatus = '使用缓存数据（离线模式）';
          _isLoading = false;
        });
      }
    }
  }

  /// 解析 TableStore Node.js SDK 返回的行数据
  Map<String, dynamic> _parseOtsRow(dynamic rawRow) {
    final row = rawRow as Map<String, dynamic>;
    final result = <String, dynamic>{};

    final pkList = row['primaryKey'] as List<dynamic>? ?? [];
    for (final pk in pkList) {
      final pkMap = pk as Map<String, dynamic>;
      result[pkMap['name'] as String] = pkMap['value'];
    }

    final attrList = row['attributes'] as List<dynamic>? ?? [];
    for (final attr in attrList) {
      final attrMap = attr as Map<String, dynamic>;
      result[attrMap['columnName'] as String] = attrMap['columnValue'];
    }

    return result;
  }

  /// 从记录中安全取值，空值显示占位文字
  String _str(Map<String, dynamic> item, String key) {
    final v = item[key];
    if (v == null || v.toString().isEmpty) return '—';
    return v.toString();
  }

  /// 从deviceId中提取-后面的数字，用于排序
  int _extractDeviceNumber(String deviceId) {
    final match = RegExp(r'-(\d+)').firstMatch(deviceId);
    if (match != null) {
      return int.tryParse(match.group(1) ?? '0') ?? 0;
    }
    return 0;
  }

  /// 设备列表排序：按deviceId中-后的数字升序，ProductKey有内容的排最后
  void _sortDevices() {
    _data.sort((a, b) {
      final aHasProductKey = a['ProductKey'] != null && a['ProductKey'].toString().isNotEmpty;
      final bHasProductKey = b['ProductKey'] != null && b['ProductKey'].toString().isNotEmpty;
      
      // productkey有内容的排最后
      if (aHasProductKey && !bHasProductKey) return 1;
      if (!aHasProductKey && bHasProductKey) return -1;
      
      // 按-后面的数字排序
      final aNum = _extractDeviceNumber(a['deviceId']?.toString() ?? '');
      final bNum = _extractDeviceNumber(b['deviceId']?.toString() ?? '');
      return aNum.compareTo(bNum);
    });
  }

  /// 过滤设备：只显示visible=true的设备
  void _filterVisibleDevices() {
    _data = _data.where((item) {
      final v = item['visible'];
      return v == true || v == 'true' || v == 1;
    }).toList();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFFF5F5F5),
      appBar: AppBar(
        title: const Text('设备管理'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : Column(
              children: [
                // 状态提示条
                if (_loadStatus.isNotEmpty)
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
                    color: _isFromCache ? Colors.blue.shade100 : Colors.red.shade100,
                    child: Row(
                      children: [
                        Icon(
                          _isFromCache ? Icons.cloud_off : Icons.error_outline,
                          color: _isFromCache ? Colors.blue : Colors.red,
                          size: 20,
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            _loadStatus,
                            style: const TextStyle(fontSize: 13),
                            maxLines: 2,
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                        if (_isFromCache)
                          IconButton(
                            icon: const Icon(Icons.refresh, size: 20),
                            onPressed: _loadData,
                            tooltip: '刷新',
                          ),
                      ],
                    ),
                  ),
                // 顶部信息栏：设备计数
                Padding(
                  padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
                  child: Text(
                    '共 ${_data.length} 台设备',
                    style: TextStyle(fontSize: 14, color: Colors.grey[600]),
                  ),
                ),
                Expanded(
                  child: _data.isEmpty
                      ? const Center(
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Icon(Icons.devices, size: 64, color: Colors.grey),
                              SizedBox(height: 16),
                              Text('设备管理', style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                              SizedBox(height: 8),
                              Text('暂无数据', style: TextStyle(color: Colors.grey)),
                            ],
                          ),
                        )
                      : Padding(
                          padding: const EdgeInsets.fromLTRB(12, 0, 12, 12),
                          child: Container(
                            decoration: BoxDecoration(
                              color: Colors.white,
                              borderRadius: BorderRadius.circular(12),
                            ),
                            child: RefreshIndicator(
                              onRefresh: _handleRefresh,
                              child: ListView.builder(
                                padding: EdgeInsets.zero,
                                itemCount: _data.length,
                                itemBuilder: (context, index) {
                                  final item = _data[index];
                                  
                                  final deviceId = _str(item, 'deviceId');
                                  final rename = _str(item, 'rename');
                                  
                                  // 获取设备LOT数据
                                  final deviceLot = _deviceLotMap[deviceId];
                                  final lotTimeRaw = deviceLot != null ? _str(deviceLot, 'time') : '—';
                                  
                                  // 获取设备对时表数据
                                  final deviceSync = _deviceSyncMap[deviceId];
                                  final syncTimeRaw = deviceSync != null ? _str(deviceSync, 'time') : '—';
                                  
                                  // 从对时表lorastr中提取电量（第4个字段）
                                  String batteryLevel = '';
                                  if (deviceSync != null) {
                                    final lorastr = deviceSync['lorastr']?.toString() ?? '';
                                    if (lorastr.isNotEmpty) {
                                      final loraParts = lorastr.split('|');
                                      if (loraParts.length >= 4) {
                                        batteryLevel = loraParts[3];
                                      }
                                    }
                                  }
                                  
                                  // 获取蓝牙缓存时间
                                  final bluetoothTimeRaw = _bluetoothTimeMap[deviceId] ?? '—';
                                  
                                  // 对比LOT表、对时表、蓝牙缓存的时间，取最晚的
                                  String finalTimeRaw;
                                  bool timeFromSync; // true=来自对时表(时钟图标), false=来自LOT表(绿点)
                                  final lotDt = _parseTimeToDateTime(lotTimeRaw);
                                  final syncDt = _parseTimeToDateTime(syncTimeRaw);
                                  final btDt = _parseTimeToDateTime(bluetoothTimeRaw);
                                  
                                  // 找出三个时间中最晚的
                                  DateTime? latestDt;
                                  String latestSource = 'lot'; // 'lot', 'sync', 'bluetooth'
                                  
                                  if (lotDt != null) {
                                    latestDt = lotDt;
                                    latestSource = 'lot';
                                  }
                                  if (syncDt != null && (latestDt == null || syncDt.isAfter(latestDt))) {
                                    latestDt = syncDt;
                                    latestSource = 'sync';
                                  }
                                  if (btDt != null && (latestDt == null || btDt.isAfter(latestDt))) {
                                    latestDt = btDt;
                                    latestSource = 'bluetooth';
                                  }
                                  
                                  if (latestSource == 'sync') {
                                    finalTimeRaw = syncTimeRaw;
                                    timeFromSync = true;
                                  } else if (latestSource == 'bluetooth') {
                                    finalTimeRaw = bluetoothTimeRaw;
                                    // 蓝牙类型1/5=定位→绿点，类型2=对时→时钟
                                    final btType = _bluetoothTypeMap[deviceId] ?? '';
                                    timeFromSync = (btType == '2');
                                  } else {
                                    finalTimeRaw = lotTimeRaw;
                                    timeFromSync = false;
                                  }
                                  
                                  // 计算相对时间
                                  String timeAgo = _calcTimeAgo(finalTimeRaw);
                                  // 根据上报周期计算颜色级别: 0=绿, 1=红, 2=灰
                                  int timeColorLevel = _getTimeColorLevel(finalTimeRaw, deviceId);
                                  
                                  // 图标：LOT表→绿点，对时表→时钟
                                  bool showGreenDot = !timeFromSync;
                                  
                                  // 判断设备是否在工作时间内
                                  bool isWorking = _isDeviceWorking(deviceId);
                                  
                                  // 构建显示名称：deviceId (rename)
                                  String displayName = deviceId;
                                  if (rename != '—') displayName += ' ($rename)';

                            // 单行显示：序号 + 设备名 + 状态图标 + 时间标签
                            return Column(
                              children: [
                                if (index > 0)
                                  Padding(
                                    padding: const EdgeInsets.only(left: 56),
                                    child: Divider(height: 1, color: Colors.grey[200]),
                                  ),
                                InkWell(
                                  onTap: () {
                                    Navigator.push(
                                      context,
                                      MaterialPageRoute(
                                        builder: (context) => DeviceDetailPage(
                                          device: item,
                                          deviceLot: deviceLot,
                                        ),
                                      ),
                                    );
                                  },
                                  child: Padding(
                                    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                                    child: Row(
                                      children: [
                                        // 序号
                                        SizedBox(
                                          width: 28,
                                          child: Text(
                                            '${index + 1}',
                                            style: TextStyle(
                                              fontSize: 16,
                                              fontWeight: FontWeight.w600,
                                              color: Colors.grey[500],
                                            ),
                                          ),
                                        ),
                                        const SizedBox(width: 4),
                                        // 设备名称
                                        Expanded(
                                          child: Text(
                                            displayName,
                                            style: TextStyle(
                                              fontSize: 14,
                                              fontWeight: FontWeight.w700,
                                              color: isWorking ? const Color(0xFF333333) : Colors.grey[400],
                                            ),
                                            maxLines: 1,
                                            overflow: TextOverflow.ellipsis,
                                          ),
                                        ),
                                        // 电量
                                        if (batteryLevel.isNotEmpty) ...[
                                         
                                          Text(
                                            '${batteryLevel}',
                                            style: TextStyle(
                                              fontSize: 13,
                                              fontWeight: FontWeight.w600,
                                              color: Colors.grey[600],
                                            ),
                                          ),
                                        ],
                                        const SizedBox(width: 8),
                                        // 状态图标：颜色跟随时间
                                        showGreenDot
                                            ? Icon(Icons.circle, size: 10, color: timeColorLevel == 0 ? const Color(0xFF2ECC71) : timeColorLevel == 1 ? Colors.red : Colors.grey[500])
                                            : Icon(Icons.access_time, size: 18, color: timeColorLevel == 0 ? const Color(0xFF2ECC71) : timeColorLevel == 1 ? Colors.red : Colors.grey[500]),
                                        const SizedBox(width: 8),
                                        // 时间标签
                                        Container(
                                          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                                          decoration: BoxDecoration(
                                            color: timeColorLevel == 0
                                                ? const Color(0xFFE8F8F0)
                                                : timeColorLevel == 1
                                                    ? const Color(0xFFFDEDEC)
                                                    : Colors.grey[100]!,
                                            borderRadius: BorderRadius.circular(4),
                                          ),
                                          child: Text(
                                            timeAgo.isNotEmpty ? timeAgo : '—',
                                            style: TextStyle(
                                              fontSize: 12,
                                              fontWeight: FontWeight.w600,
                                              color: timeColorLevel == 0
                                                  ? const Color(0xFF2ECC71)
                                                  : timeColorLevel == 1
                                                      ? Colors.red
                                                      : Colors.grey[500],
                                            ),
                                          ),
                                        ),
                                      ],
                                    ),
                                  ),
                                ),
                              ],
                            );
                          },
                        ),
                      ),
                    ),
                  ),
                ),
              ],
            ),
    );
  }

  /// 处理下拉刷新
  Future<void> _handleRefresh() async {
    await _loadData();
  }

  /// 解析时间字符串为DateTime
  DateTime? _parseTimeToDateTime(String timeRaw) {
    if (timeRaw == '—' || timeRaw.isEmpty) return null;
    try {
      if (timeRaw.contains(' ')) {
        final parts = timeRaw.split(' ');
        if (parts.length >= 2) {
          final dateParts = parts[0].replaceAll('/', '-').split('-');
          final timeParts = parts[1].split(':');
          if (dateParts.length >= 3 && timeParts.length >= 2) {
            return DateTime(
              int.parse(dateParts[0]),
              int.parse(dateParts[1]),
              int.parse(dateParts[2]),
              int.parse(timeParts[0]),
              int.parse(timeParts[1]),
              timeParts.length >= 3 ? int.parse(timeParts[2]) : 0,
            );
          }
        }
      }
    } catch (e) {
      debugPrint('解析时间失败: $e, 原始时间: $timeRaw');
    }
    return null;
  }

  /// 计算相对时间显示（不含括号）
  String _calcTimeAgo(String timeRaw) {
    if (timeRaw == '—' || timeRaw.isEmpty) return '';
    try {
      if (timeRaw.contains(' ')) {
        final parts = timeRaw.split(' ');
        if (parts.length >= 2) {
          final datePart = parts[0];
          final timePart = parts[1];
          final dateParts = datePart.replaceAll('/', '-').split('-');
          final timeParts = timePart.split(':');
          if (dateParts.length >= 3 && timeParts.length >= 2) {
            final year = int.parse(dateParts[0]);
            final month = int.parse(dateParts[1]);
            final day = int.parse(dateParts[2]);
            final hour = int.parse(timeParts[0]);
            final minute = int.parse(timeParts[1]);
            final second = timeParts.length >= 3 ? int.parse(timeParts[2]) : 0;
            final deviceTime = DateTime(year, month, day, hour, minute, second);
            final now = DateTime.now();
            final difference = now.difference(deviceTime);
            if (difference.inSeconds < 10) return '刚刚';
            if (difference.inSeconds < 60) return '${difference.inSeconds}秒前';
            if (difference.inMinutes < 60) return '${difference.inMinutes}分钟前';
            if (difference.inHours < 24) return '${difference.inHours}小时前';
            return '${difference.inDays}天前';
          }
        }
      }
    } catch (e) {
      debugPrint('解析时间失败: $e, 原始时间: $timeRaw');
    }
    return '';
  }


  /// 显示编辑对话框
  void _showEditDialog(Map<String, dynamic> item) {
    final deviceId = _str(item, 'deviceId');
    final picurl = _str(item, 'picurl');
    final hasImage = picurl != '—' && picurl.isNotEmpty;
    
    // 初始化表单数据
    _deviceKeyController.text = _str(item, 'device_key');
    _renameController.text = _str(item, 'rename');
    
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('编辑设备'),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // 图片显示
              Center(
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(10),
                  child: hasImage
                      ? Image.network(
                          picurl,
                          width: 150,
                          height: 150,
                          fit: BoxFit.cover,
                          errorBuilder: (context, error, stackTrace) {
                            return _buildImagePlaceholderLarge();
                          },
                        )
                      : _buildImagePlaceholderLarge(),
                ),
              ),
              const SizedBox(height: 16),
              
              // deviceId（不可编辑）
              TextField(
                controller: TextEditingController(text: deviceId),
                enabled: false,
                decoration: const InputDecoration(
                  labelText: '设备ID',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.devices),
                ),
              ),
              const SizedBox(height: 12),
              
              // device_key（可编辑）
              TextField(
                controller: _deviceKeyController,
                decoration: const InputDecoration(
                  labelText: '设备编号 (device_key)',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.key),
                ),
                keyboardType: TextInputType.number,
              ),
              const SizedBox(height: 12),
              
              // rename（可编辑）
              TextField(
                controller: _renameController,
                decoration: const InputDecoration(
                  labelText: '别名 (rename)',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.edit_note),
                ),
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
              _saveDevice(item);
              Navigator.pop(context);
            },
            child: const Text('保存'),
          ),
        ],
      ),
    );
  }

  /// 保存图片占位符
  Widget _buildImagePlaceholderLarge() {
    return Container(
      width: 150,
      height: 150,
      decoration: BoxDecoration(
        color: Colors.grey.shade200,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Icon(Icons.image_not_supported, size: 64, color: Colors.grey[400]),
    );
  }

  /// 保存设备信息
  Future<void> _saveDevice(Map<String, dynamic> item) async {
    final deviceId = item['deviceId'];
    final picurl = item['picurl'];
    final newDeviceKey = _deviceKeyController.text;
    final newRename = _renameController.text;
    
    debugPrint('保存设备: deviceId=$deviceId, deviceKey=$newDeviceKey, rename=$newRename, picurl=$picurl');
    
    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'updateDevice',
          'info': {
            'deviceId': deviceId,
            'device_key': newDeviceKey,
            'rename': newRename,
            'picurl': picurl,
          },
        }),
      );
      
      debugPrint('保存响应: ${resp.statusCode} - ${resp.body}');
      
      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          debugPrint('保存成功');
          // 保存成功后刷新列表
          _loadData();
        } else {
          debugPrint('保存失败: ${json['msg']}');
        }
      }
    } catch (e) {
      debugPrint('保存设备失败: $e');
    }
  }
}