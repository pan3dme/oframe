import 'dart:convert';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_map_tile_caching/flutter_map_tile_caching.dart';
import 'package:latlong2/latlong.dart';
import 'package:geolocator/geolocator.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';
import '../utils/coord_transform.dart';
import '../utils/db_helper.dart';
import '../main.dart'; // 全局 globalWechatId

class MapCenterPage extends StatefulWidget {
  const MapCenterPage({super.key});

  @override
  State<MapCenterPage> createState() => MapCenterPageState();
}

class MapCenterPageState extends State<MapCenterPage> with TickerProviderStateMixin {
  final MapController _mapController = MapController();
  LatLng? _currentPosition;
  bool _isLocating = false;
  String _mapStatus = '地图加载中...'; // 地图状态提示
  
  // 道路和地名数据
  List<Map<String, dynamic>> _allRouteData = []; // 所有道路数据（完整）
  List<Map<String, dynamic>> _allPlaceData = []; // 所有地名数据（完整）
  List<Map<String, dynamic>> _displayedRouteData = []; // 当前显示的道路数据（根据level过滤）
  List<Map<String, dynamic>> _displayedPlaceData = []; // 当前显示的地名数据（根据level过滤）
  bool _showRouteAndPlace = false; // 是否显示道路和地名
  bool _isLoadingRoutePlace = false; // 是否正在加载道路地名数据
  int _currentLevel = 1; // 当前显示的level级别（1 -> 2 -> 3 -> 0 -> 1...）
  String _levelStatus = ''; // level状态提示
  int _maxAvailableLevel = 1; // 数据中实际存在的最大level值
  
  // 设备位置数据
  List<Map<String, dynamic>> _devicePositions = []; // 设备位置列表
  bool _showDevices = false; // 是否显示设备位置
  Map<String, Map<String, dynamic>> _bluetoothGpsCache = {}; // 蓝牙缓存GPS映射: deviceId -> {lat, lng}
  
  // 黄点闪烁动画
  AnimationController? _blinkAnimationController;
  double _blinkOpacity = 1.0;
  
  // 地图缓存相关
  bool _isCacheEnabled = true; // 是否启用缓存
  int _cachedTileCount = 0; // 已缓存瓦片数量
  final String _cacheStoreName = 'map_cache'; // 缓存存储名称
  
  // 蓝牙数据监听
  int _lastBluetoothDataCount = 0; // 上次检查的蓝牙数据数量
  
  // 道路地名缓存时间管理
  String? _lastRoutePlaceFetchDate; // 上次请求道路地名数据的日期 (格式: yyyy-MM-dd)
  
  // 设备气泡显示控制
  String? _selectedDeviceId; // 当前显示气泡的设备deviceId（同时只显示一个）

  @override
  void initState() {
    super.initState();
    // 初始化瓦片缓存
    _initTileCache();
    // 页面加载后自动定位
    _getCurrentLocation();
    // 自动加载设备位置（先缓存后网络）
    _loadDevicePositionsAuto();
    // 初始化闪烁动画
    _initBlinkAnimation();
    // 启动蓝牙数据监听
    _startBluetoothDataListener();
    // 恢复道路地名上次请求日期
    _restoreLastRoutePlaceFetchDate();
  }

  /// 初始化黄点闪烁动画
  void _initBlinkAnimation() {
    _blinkAnimationController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1000), // 总周期1秒
    );
    
    _blinkAnimationController!.repeat();
    
    _blinkAnimationController!.addListener(() {
      setState(() {
        final value = _blinkAnimationController!.value;
        // 0-0.5秒：黄色
        // 0.5-1秒：红色
        if (value < 0.5) {
          _blinkOpacity = 1.0; // 黄色阶段
        } else {
          _blinkOpacity = 0.0; // 红色阶段
        }
      });
    });
  }

  /// 无感刷新：同时刷新LOT数据和对时数据，取两者最新时间判断GPS过期
  void silentRefresh() async {
    if (!mounted) return;
    debugPrint('[地图中心] 触发无感刷新 LOT + 对时...');
    try {
      // 并行请求LOT数据和对时数据
      final results = await Future.wait([
        _fetchLotRefreshAll(),
        _fetchSyncRefreshAll(),
      ]);
      final lotSuccess = results[0] as bool;
      final syncSuccess = results[1] as bool;
      debugPrint('[地图刷新] LOT=$lotSuccess, 对时=$syncSuccess');
      // 重新加载设备位置
      await _loadDevicePositions();
      if (mounted) {
        setState(() {
          _showDevices = true;
        });
      }
    } catch (e) {
      debugPrint('[地图刷新] 失败: $e');
    }
  }

  /// 请求LOT刷新数据并保存到缓存，返回是否成功
  Future<bool> _fetchLotRefreshAll() async {
    try {
      final resp = await http.post(
        Uri.parse(deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getDeviceLotRefreshAll',
          'info': {
            'limit': 99,
            'wechatid': globalWechatId,
          }}),
      );
      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>;
          final parsedData = rawRows.map((rawRow) {
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
          }).toList();
          for (final lot in parsedData) {
            final deviceId = lot['deviceId']?.toString() ?? '';
            final time = lot['time']?.toString() ?? '';
            debugPrint('[地图刷新] LOT 设备[$deviceId] TIME=$time');
          }
          await DBHelper().saveDeviceLot(parsedData);
          debugPrint('[地图刷新] LOT数据已保存: ${parsedData.length}条');
          return true;
        } else {
          debugPrint('[地图刷新] LOT请求错误: ${json['msg']}');
        }
      } else {
        debugPrint('[地图刷新] LOT HTTP ${resp.statusCode}');
      }
    } catch (e) {
      debugPrint('[地图刷新] LOT失败: $e');
    }
    return false;
  }

  /// 请求对时刷新数据并保存到缓存，返回是否成功
  Future<bool> _fetchSyncRefreshAll() async {
    try {
      final resp = await http.post(
        Uri.parse(deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getDevicesyncAll',
          'info': {
            'limit': 99,
            'wechatid': globalWechatId,
          }}),
      );
      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>;
          final parsedData = rawRows.map((rawRow) {
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
          }).toList();
          for (final sync in parsedData) {
            final deviceId = sync['deviceId']?.toString() ?? '';
            final time = sync['time']?.toString() ?? '';
            debugPrint('[地图刷新] 对时 设备[$deviceId] TIME=$time');
          }
          await DBHelper().saveDeviceSync(parsedData);
          debugPrint('[地图刷新] 对时数据已保存: ${parsedData.length}条');
          return true;
        } else {
          debugPrint('[地图刷新] 对时请求错误: ${json['msg']}');
        }
      } else {
        debugPrint('[地图刷新] 对时 HTTP ${resp.statusCode}');
      }
    } catch (e) {
      debugPrint('[地图刷新] 对时失败: $e');
    }
    return false;
  }

  @override
  void dispose() {
    _blinkAnimationController?.dispose();
    super.dispose();
  }

  /// 启动蓝牙数据监听（定期检查数量变化）
  void _startBluetoothDataListener() {
    // 每5秒检查一次蓝牙数据数量变化
    Future.delayed(const Duration(seconds: 5), () async {
      if (!mounted) return;
      
      try {
        final bluetoothData = await DBHelper().getBluetoothData();
        final currentCount = bluetoothData.length;
        
        // 如果数量发生变化，刷新设备位置
        if (currentCount != _lastBluetoothDataCount) {
          debugPrint('[蓝牙监听] 检测到数据变化: $_lastBluetoothDataCount -> $currentCount');
          debugPrint('[蓝牙监听] 自动刷新设备位置...');
          
          // 记录当前数量
          setState(() {
            _lastBluetoothDataCount = currentCount;
          });
          
          // 重新加载设备位置
          await _loadDevicePositions();
          
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text('检测到新数据，已刷新设备位置'),
                duration: const Duration(seconds: 2),
              ),
            );
          }
        }
        
        // 继续监听
        _startBluetoothDataListener();
      } catch (e) {
        debugPrint('[蓝牙监听] 检查失败: $e');
        // 出错后继续监听
        _startBluetoothDataListener();
      }
    });
  }

  /// 恢复道路地名上次请求日期
  Future<void> _restoreLastRoutePlaceFetchDate() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final savedDate = prefs.getString('last_route_place_fetch_date');
      
      if (savedDate != null) {
        setState(() {
          _lastRoutePlaceFetchDate = savedDate;
        });
        debugPrint('[道路地名] 恢夏上次请求日期: $_lastRoutePlaceFetchDate');
      } else {
        debugPrint('[道路地名] 没有保存的请求日期');
      }
    } catch (e) {
      debugPrint('[道路地名] 恢夏日期失败: $e');
    }
  }

  /// 保存道路地名请求日期
  Future<void> _saveLastRoutePlaceFetchDate(String date) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString('last_route_place_fetch_date', date);
      debugPrint('[道路地名] 已保存请求日期: $date');
    } catch (e) {
      debugPrint('[道路地名] 保存日期失败: $e');
    }
  }

  /// 初始化瓦片缓存
  Future<void> _initTileCache() async {
    try {
      debugPrint('[瓦片缓存] 开始初始化...');
      
      // 创建或打开缓存存储（如果已存在则忽略错误）
      try {
        await FMTCStore(_cacheStoreName).manage.create();
      } catch (e) {
        // 存储可能已经存在，忽略错误
        debugPrint('[瓦片缓存] 存储已存在或创建失败: $e');
      }
      
      setState(() {
        _cachedTileCount = -1; // -1表示未知数量
      });
      
      debugPrint('[瓦片缓存] 初始化完成');
    } catch (e) {
      debugPrint('[瓦片缓存] 初始化失败: $e');
    }
  }

  /// 自动加载设备位置（先缓存后网络）
  Future<void> _loadDevicePositionsAuto() async {
    debugPrint('[设备位置] 开始自动加载...');
    
    // 初始化蓝牙数据数量
    try {
      final bluetoothData = await DBHelper().getBluetoothData();
      setState(() {
        _lastBluetoothDataCount = bluetoothData.length;
      });
      debugPrint('[设备位置] 初始蓝牙数据数量: $_lastBluetoothDataCount');
    } catch (e) {
      debugPrint('[设备位置] 获取初始蓝牙数据失败: $e');
    }
    
    // 先从缓存加载
    await _loadDevicePositionsFromCache();
    
    // 再从网络更新
    await _loadDevicePositionsFromNetwork();
  }

  /// 从缓存加载设备位置
  Future<void> _loadDevicePositionsFromCache() async {
    try {
      debugPrint('[设备位置] 尝试从缓存加载...');
      
      final devices = await DBHelper().getDevices();
      final deviceLotList = await DBHelper().getDeviceLot();
      final deviceSyncList = await DBHelper().getDeviceSync();
      
      if (devices.isEmpty || deviceLotList.isEmpty) {
        debugPrint('[设备位置] 缓存中没有数据');
        return;
      }
      
      // 先加载蓝牙缓存GPS数据
      await _loadBluetoothGpsCache();
      
      // 构建设备LOT映射
      final lotMap = <String, Map<String, dynamic>>{};
      for (final lot in deviceLotList) {
        final deviceId = lot['deviceId'] as String;
        lotMap[deviceId] = lot;
      }
      
      // 构建设备对时映射
      final syncMap = <String, Map<String, dynamic>>{};
      for (final sync in deviceSyncList) {
        final deviceId = sync['deviceId'] as String;
        syncMap[deviceId] = sync;
      }
      
      // 合并设备和位置信息
      final positions = <Map<String, dynamic>>[];
      for (final device in devices) {
        final deviceId = device['deviceId'] as String;
        final rename = device['rename'];
        final productKey = device['ProductKey']?.toString() ?? '';
        
        // 跳过中继设备（有ProductKey的设备）
        if (productKey.isNotEmpty) continue;
        
        // 构造显示名称：deviceId + (rename)
        String displayName;
        if (rename != null && rename.toString().isNotEmpty) {
          displayName = '$deviceId ($rename)';
        } else {
          displayName = deviceId;
        }
        
        // 优先使用蓝牙缓存中的GPS坐标
        final bluetoothGps = _bluetoothGpsCache[deviceId];
        double? lat;
        double? lng;
        bool fromBluetooth = false;
        
        if (bluetoothGps != null) {
          // 使用蓝牙缓存的GPS
          lat = bluetoothGps['lat'] as double;
          lng = bluetoothGps['lng'] as double;
          fromBluetooth = true;
          debugPrint('[设备位置] 设备[$displayName] 使用蓝牙缓存GPS: ($lat, $lng)');
        } else {
          // 使用LOT数据中的GPS
          final lot = lotMap[deviceId];
          if (lot != null) {
            final gpsStr = lot['gps'] as String?;
            if (gpsStr != null && gpsStr.isNotEmpty && gpsStr.contains(',')) {
              try {
                final parts = gpsStr.split(',');
                if (parts.length >= 2) {
                  final parsedLat = double.tryParse(parts[0].trim());
                  final parsedLng = double.tryParse(parts[1].trim());
                  
                  if (parsedLat != null && parsedLng != null && 
                      parsedLat.abs() > 0.0001 && parsedLng.abs() > 0.0001) {
                    lat = parsedLat;
                    lng = parsedLng;
                    debugPrint('[设备位置] 设备[$displayName] 使用LOT GPS: ($lat, $lng)');
                  }
                }
              } catch (e) {
                debugPrint('[设备位置] 解析GPS失败: $e');
              }
            }
          }
        }
        
        // 如果有GPS坐标，转换并添加到列表
        if (lat != null && lng != null && lat.abs() > 0.0001 && lng.abs() > 0.0001) {
          final gcj02Coord = CoordTransform.wgs84ToGcj02(lat, lng);
          
          // 取LOT时间和对时时间中最新的，与当前时间对比判断GPS过期
          final lotTimeStr = lotMap[deviceId]?['time']?.toString() ?? '';
          final syncTimeStr = syncMap[deviceId]?['time']?.toString() ?? '';
          final lotDt = _parseGpsTime(lotTimeStr);
          final syncDt = _parseGpsTime(syncTimeStr);
          // 取两者中最新的时间
          DateTime? latestServerDt;
          String gpsTimeStr = '';
          if (lotDt != null && (syncDt == null || lotDt.isAfter(syncDt))) {
            latestServerDt = lotDt;
            gpsTimeStr = lotTimeStr;
          } else if (syncDt != null) {
            latestServerDt = syncDt;
            gpsTimeStr = syncTimeStr;
          }
          // 如果服务器时间都没有，用蓝牙时间
          if (latestServerDt == null) {
            gpsTimeStr = bluetoothGps?['time']?.toString() ?? '';
          }
          bool gpsExpired = false;
          if (gpsTimeStr.isNotEmpty) {
            final gpsTime = _parseGpsTime(gpsTimeStr);
            if (gpsTime != null) {
              gpsExpired = DateTime.now().difference(gpsTime) > const Duration(hours: 1);
              debugPrint('[GPS过期] 设备[$deviceId] lotTime=$lotTimeStr syncTime=$syncTimeStr latest=$gpsTimeStr expired=$gpsExpired');
            } else {
              debugPrint('[GPS过期] 设备[$deviceId] 时间解析失败: time=$gpsTimeStr');
            }
          }
          positions.add({
            'deviceId': deviceId,
            'name': displayName,
            'lat': gcj02Coord[0],
            'lng': gcj02Coord[1],
            'fromBluetooth': fromBluetooth,
            'gpsTime': gpsTimeStr,
            'gps_expired': gpsExpired,
          });
        }
      }
      
      if (positions.isNotEmpty) {
        setState(() {
          _devicePositions = positions;
          _showDevices = true; // 自动显示设备
        });
        debugPrint('[设备位置] 从缓存加载成功: ${positions.length}个设备');
      }
    } catch (e) {
      debugPrint('[设备位置] 从缓存加载失败: $e');
    }
  }

  /// 从网络更新设备位置
  Future<void> _loadDevicePositionsFromNetwork() async {
    try {
      debugPrint('[设备位置] 从网络更新...');
      
      // 这里可以添加从网络刷新设备数据的逻辑
      // 目前先使用缓存数据，如果需要实时刷新可以调用设备管理页面的接口
      
      // 重新加载以确保数据最新
      await _loadDevicePositions();
      
      if (_devicePositions.isNotEmpty) {
        setState(() {
          _showDevices = true; // 确保显示设备
        });
      }
    } catch (e) {
      debugPrint('[设备位置] 从网络更新失败: $e');
      // 网络失败不影响已显示的缓存数据
    }
  }

  /// 加载蓝牙缓存GPS数据
  Future<void> _loadBluetoothGpsCache() async {
    try {
      debugPrint('[蓝牙GPS缓存] 开始加载...');
      
      final bluetoothData = await DBHelper().getBluetoothData();
      debugPrint('[蓝牙GPS缓存] 共${bluetoothData.length}条数据');
      
      final gpsMap = <String, Map<String, dynamic>>{};
      int invalidCount = 0; // 无效GPS计数
      
      for (final item in bluetoothData) {
        try {
          final dataStr = item['data'] as String?;
          if (dataStr == null || dataStr.isEmpty) continue;
          
          // 解析JSON: {"info":"1|v4-3|26.52956,109.39073|368","upDateDevice":"v4-1","time":"2026/6/12 13:12:44"}
          final jsonData = jsonDecode(dataStr) as Map<String, dynamic>;
          final lorastr = jsonData['info'] as String?;
          
          if (lorastr == null || !lorastr.contains('|')) continue;
          
          // 解析LORA字符串: "1|v4-3|26.52956,109.39073|368"
          final parts = lorastr.split('|');
          if (parts.length >= 4) {
            final deviceId = parts[1]; // v4-3
            final gpsPart = parts[2]; // 26.52956,109.39073
            
            if (gpsPart.contains(',')) {
              final gpsCoords = gpsPart.split(',');
              if (gpsCoords.length >= 2) {
                final lat = double.tryParse(gpsCoords[0].trim());
                final lng = double.tryParse(gpsCoords[1].trim());
                
                // 严格验证GPS有效性：排除0.00000或接近0的值
                if (lat != null && lng != null && 
                    lat.abs() > 0.0001 && lng.abs() > 0.0001) {
                  // 只保留最新的GPS数据（后面的会覆盖前面的）
                  final timeStr = jsonData['time']?.toString() ?? '';
                  gpsMap[deviceId] = {'lat': lat, 'lng': lng, 'time': timeStr};
                  debugPrint('[蓝牙GPS缓存] ✓ 设备[$deviceId]: ($lat, $lng) time=$timeStr');
                } else {
                  invalidCount++;
                  debugPrint('[蓝牙GPS缓存] ✗ 设备[$deviceId] GPS无效: ($lat, $lng)');
                }
              }
            }
          }
        } catch (e) {
          debugPrint('[蓝牙GPS缓存] 解析单条数据失败: $e');
        }
      }
      
      setState(() {
        _bluetoothGpsCache = gpsMap;
      });
      
      debugPrint('[蓝牙GPS缓存] 加载完成: ${gpsMap.length}个设备的GPS, $invalidCount个无效GPS已过滤');
    } catch (e) {
      debugPrint('[蓝牙GPS缓存] 加载失败: $e');
    }
  }

  /// 加载设备位置数据（用于手动刷新）
  Future<void> _loadDevicePositions() async {
    try {
      debugPrint('[设备位置] 开始加载...');
      
      // 从缓存加载设备数据
      final devices = await DBHelper().getDevices();
      debugPrint('[设备位置] 设备数量: ${devices.length}');
      
      final deviceLotList = await DBHelper().getDeviceLot();
      debugPrint('[设备位置] LOT数据数量: ${deviceLotList.length}');
      
      final deviceSyncList = await DBHelper().getDeviceSync();
      debugPrint('[设备位置] 对时数据数量: ${deviceSyncList.length}');
      
      if (devices.isEmpty) {
        debugPrint('[设备位置] 没有设备数据，请先在设备管理页面加载数据');
        return;
      }
      
      // 打印第一个设备的详细信息
      if (devices.isNotEmpty) {
        debugPrint('[设备位置] 第一个设备: ${devices[0]}');
      }
      
      // 打印第一个LOT的详细信息
      if (deviceLotList.isNotEmpty) {
        debugPrint('[设备位置] 第一个LOT: ${deviceLotList[0]}');
      }
      
      // 先加载蓝牙缓存GPS数据
      await _loadBluetoothGpsCache();
      
      // 构建设备LOT映射
      final lotMap = <String, Map<String, dynamic>>{};
      for (final lot in deviceLotList) {
        final deviceId = lot['deviceId'] as String;
        lotMap[deviceId] = lot;
        debugPrint('[设备位置] LOT原始数据: deviceId=$deviceId, time=${lot['time']}, time类型=${lot['time']?.runtimeType}');
      }
      
      debugPrint('[设备位置] LOT映射构建完成，共${lotMap.length}个设备');
      
      // 构建设备对时映射
      final syncMap = <String, Map<String, dynamic>>{};
      for (final sync in deviceSyncList) {
        final deviceId = sync['deviceId'] as String;
        syncMap[deviceId] = sync;
      }
      debugPrint('[设备位置] 对时映射构建完成，共${syncMap.length}个设备');
      
      // 合并设备和位置信息
      final positions = <Map<String, dynamic>>[];
      int noGpsCount = 0; // 没有GPS的设备数
      int invalidGpsCount = 0; // GPS无效的设备数
      int bluetoothGpsCount = 0; // 使用蓝牙GPS的设备数
      
      for (final device in devices) {
        final deviceId = device['deviceId'] as String;
        final rename = device['rename'];
        final productKey = device['ProductKey']?.toString() ?? '';
        
        // 跳过中继设备（有ProductKey的设备）
        if (productKey.isNotEmpty) continue;
        
        // 构造显示名称：deviceId + (rename)
        String displayName;
        if (rename != null && rename.toString().isNotEmpty) {
          displayName = '$deviceId ($rename)';
        } else {
          displayName = deviceId;
        }
        
        // 优先使用蓝牙缓存中的GPS坐标
        final bluetoothGps = _bluetoothGpsCache[deviceId];
        double? lat;
        double? lng;
        bool fromBluetooth = false;
        
        if (bluetoothGps != null) {
          // 使用蓝牙缓存的GPS
          lat = bluetoothGps['lat'] as double;
          lng = bluetoothGps['lng'] as double;
          fromBluetooth = true;
          bluetoothGpsCount++;
          debugPrint('[设备位置] ✓ 设备[$displayName] 使用蓝牙缓存GPS: ($lat, $lng)');
        } else {
          // 查找对应的LOT数据
          final lot = lotMap[deviceId];
          if (lot != null) {
            final gpsStr = lot['gps'] as String?;
            debugPrint('[设备位置] 设备[$displayName] GPS原始数据: "$gpsStr"');
            
            if (gpsStr == null || gpsStr.isEmpty) {
              noGpsCount++;
              debugPrint('[设备位置] 设备[$displayName] 没有GPS数据');
              continue;
            }
            
            if (!gpsStr.contains(',')) {
              invalidGpsCount++;
              debugPrint('[设备位置] 设备[$displayName] GPS格式错误: $gpsStr');
              continue;
            }
            
            try {
              // 解析GPS坐标："纬度,经度"
              final parts = gpsStr.split(',');
              if (parts.length >= 2) {
                final parsedLat = double.tryParse(parts[0].trim());
                final parsedLng = double.tryParse(parts[1].trim());
                
                debugPrint('[设备位置] 设备[$displayName] 解析后: lat=$parsedLat, lng=$parsedLng');
                
                if (parsedLat != null && parsedLng != null && parsedLat != 0 && parsedLng != 0) {
                  lat = parsedLat;
                  lng = parsedLng;
                  debugPrint('[设备位置] 设备[$displayName] 使用LOT GPS: ($lat, $lng)');
                } else {
                  invalidGpsCount++;
                  debugPrint('[设备位置] 设备[$displayName] GPS坐标为0');
                  continue;
                }
              }
            } catch (e) {
              invalidGpsCount++;
              debugPrint('[设备位置] 设备[$displayName] 解析GPS失败: $e');
              continue;
            }
          } else {
            noGpsCount++;
            debugPrint('[设备位置] 设备[$displayName] 没有找到LOT数据');
            continue;
          }
        }
        
        // 如果有GPS坐标，转换并添加到列表
        if (lat != null && lng != null && lat != 0 && lng != 0) {
          // WGS-84转GCJ-02
          final gcj02Coord = CoordTransform.wgs84ToGcj02(lat, lng);
          
          // 取LOT时间和对时时间中最新的，与当前时间对比判断GPS过期
          final lotTimeStr = lotMap[deviceId]?['time']?.toString() ?? '';
          final syncTimeStr = syncMap[deviceId]?['time']?.toString() ?? '';
          final lotDt = _parseGpsTime(lotTimeStr);
          final syncDt = _parseGpsTime(syncTimeStr);
          // 取两者中最新的时间
          DateTime? latestServerDt;
          String gpsTimeStr = '';
          if (lotDt != null && (syncDt == null || lotDt.isAfter(syncDt))) {
            latestServerDt = lotDt;
            gpsTimeStr = lotTimeStr;
          } else if (syncDt != null) {
            latestServerDt = syncDt;
            gpsTimeStr = syncTimeStr;
          }
          // 如果服务器时间都没有，用蓝牙时间
          if (latestServerDt == null) {
            gpsTimeStr = bluetoothGps?['time']?.toString() ?? '';
          }
          bool gpsExpired = false;
          if (gpsTimeStr.isNotEmpty) {
            final gpsTime = _parseGpsTime(gpsTimeStr);
            if (gpsTime != null) {
              gpsExpired = DateTime.now().difference(gpsTime) > const Duration(hours: 1);
              debugPrint('[GPS过期] 设备[$deviceId] lotTime=$lotTimeStr syncTime=$syncTimeStr latest=$gpsTimeStr expired=$gpsExpired');
            } else {
              debugPrint('[GPS过期] 设备[$deviceId] 时间解析失败: time=$gpsTimeStr');
            }
          }
          positions.add({
            'deviceId': deviceId,
            'name': displayName,
            'lat': gcj02Coord[0],
            'lng': gcj02Coord[1],
            'fromBluetooth': fromBluetooth, // 标记数据来源
            'gpsTime': gpsTimeStr,
            'gps_expired': gpsExpired,
          });
          
          debugPrint('[设备位置] ✓ $displayName: ($lat, $lng) -> (${gcj02Coord[0]}, ${gcj02Coord[1]}) gpsTime=$gpsTimeStr expired=$gpsExpired');
        }
      }
      
      setState(() {
        _devicePositions = positions;
        _showDevices = true; // 自动显示
      });
      
      debugPrint('[设备位置] ========== 加载结果 ==========');
      debugPrint('[设备位置] 总设备数: ${devices.length}');
      debugPrint('[设备位置] 有LOT数据: ${devices.length - noGpsCount}');
      debugPrint('[设备位置] 无GPS数据: $noGpsCount');
      debugPrint('[设备位置] GPS无效: $invalidGpsCount');
      debugPrint('[设备位置] 使用蓝牙GPS: $bluetoothGpsCount');
      debugPrint('[设备位置] 成功加载: ${positions.length}个设备位置');
      debugPrint('[设备位置] ====================================');
    } catch (e) {
      debugPrint('[设备位置] 加载失败: $e');
    }
  }

  /// 切换设备位置显示
  void _toggleDeviceDisplay() async {
    setState(() {
      _showDevices = !_showDevices;
    });
    
    debugPrint('[设备位置] 显示状态: $_showDevices, 数量: ${_devicePositions.length}');
  }

  /// 刷新设备位置数据
  Future<void> _refreshDevicePositions() async {
    await _loadDevicePositions();
    
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('已刷新设备位置: ${_devicePositions.length}个设备'),
          duration: const Duration(seconds: 2),
        ),
      );
    }
  }

  /// 清除地图瓦片缓存
  Future<void> _clearTileCache() async {
    // 显示加载对话框
    if (!mounted) return;
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (context) => const AlertDialog(
        content: Row(
          children: [
            CircularProgressIndicator(),
            SizedBox(width: 16),
            Text('正在清除缓存...'),
          ],
        ),
      ),
    );
    
    try {
      // 等待对话框显示
      await Future.delayed(const Duration(milliseconds: 50));
      
      final backend = FMTCObjectBoxBackend();
      
      // 快速方案：销毁整个 worker + 数据库目录（而不是逐个删除瓦片）
      // immediate: true 跳过等待进行中的操作
      await backend.uninitialise(deleteRoot: true, immediate: true);
      
      // 重新初始化 FMTC 后端
      await backend.initialise();
      
      // 重新创建存储
      await FMTCStore(_cacheStoreName).manage.create();
      
      // 关闭加载对话框
      if (mounted) Navigator.pop(context);
      
      setState(() {
        _cachedTileCount = -1;
        _mapStatus = '缓存已清除';
      });
      
      debugPrint('[瓦片缓存] 已清除所有缓存');
      
      // 3秒后清除提示
      Future.delayed(const Duration(seconds: 3), () {
        if (mounted) {
          setState(() {
            _mapStatus = '';
          });
        }
      });
    } catch (e) {
      debugPrint('[瓦片缓存] 清除失败: $e');
      // 关闭加载对话框
      if (mounted) Navigator.pop(context);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('清除缓存失败: $e')),
        );
      }
    }
  }

  /// 显示缓存信息对话框
  void _showCacheInfo() {
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('地图缓存信息'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(_cachedTileCount >= 0 
              ? '已缓存瓦片数: $_cachedTileCount'
              : '已缓存瓦片数: 统计中...'),
            const SizedBox(height: 8),
            const Text('说明:', style: TextStyle(fontWeight: FontWeight.bold)),
            const Text('- 自动缓存访问过的地图区域'),
            const Text('- 离线时可查看已缓存区域'),
            const Text('- 缓存长期有效'),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('关闭'),
          ),
          TextButton(
            onPressed: () {
              Navigator.pop(context);
              _clearTileCache();
            },
            child: const Text('清除缓存', style: TextStyle(color: Colors.red)),
          ),
        ],
      ),
    );
  }

  /// 清理字符串，确保UTF-16安全
  String _sanitizeString(String input) {
    if (input.isEmpty) return '';
    
    try {
      // 移除控制字符（除了常见的空白字符）
      final cleaned = input.replaceAll(RegExp(r'[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]'), '');
      
      // 检查是否为有效的UTF-16字符串
      // Flutter的Text widget要求字符串必须是有效的UTF-16
      final codeUnits = cleaned.codeUnits;
      
      // 尝试重新构建字符串
      final result = String.fromCharCodes(codeUnits);
      
      // 如果结果为空，返回空字符串
      if (result.isEmpty) {
        return '';
      }
      
      return result;
    } catch (e) {
      debugPrint('[字符串清理] 失败: $e, 原始: ${input.length > 50 ? input.substring(0, 50) : input}...');
      return ''; // 出错时返回空字符串
    }
  }

  /// 加载道路和地名数据（先从缓存，再根据日期决定是否请求网络）
  Future<void> _loadRouteAndPlaceData() async {
    setState(() {
      _isLoadingRoutePlace = true;
    });

    try {
      // 先尝试从缓存加载
      await _loadFromCache();
      
      // 检查是否需要从网络更新（每天只请求一次）
      final shouldFetchFromNetwork = await _shouldFetchRoutePlaceFromNetwork();
      
      if (shouldFetchFromNetwork) {
        debugPrint('[道路地名] 今天尚未请求网络数据，开始更新...');
        await _loadFromNetwork();
        
        // 记录今天的日期
        final now = DateTime.now();
        final today = '${now.year}-${now.month.toString().padLeft(2, '0')}-${now.day.toString().padLeft(2, '0')}';
        setState(() {
          _lastRoutePlaceFetchDate = today;
        });
        
        // 保存到本地存储
        await _saveLastRoutePlaceFetchDate(today);
        debugPrint('[道路地名] 已记录今日请求日期: $_lastRoutePlaceFetchDate');
      } else {
        debugPrint('[道路地名] 今天已请求过网络数据，使用缓存');
      }
      
      debugPrint('[道路地名] 数据加载完成');
    } catch (e) {
      debugPrint('[道路地名] 数据加载失败: $e');
    } finally {
      setState(() {
        _isLoadingRoutePlace = false;
      });
    }
  }

  /// 判断是否应该从网络请求道路地名数据
  Future<bool> _shouldFetchRoutePlaceFromNetwork() async {
    try {
      // 如果缓存中没有数据，必须请求网络
      if (_allRouteData.isEmpty && _allPlaceData.isEmpty) {
        debugPrint('[道路地名] 缓存为空，需要请求网络');
        return true;
      }
      
      // 获取今天的日期
      final now = DateTime.now();
      final today = '${now.year}-${now.month.toString().padLeft(2, '0')}-${now.day.toString().padLeft(2, '0')}';
      
      // 如果今天已经请求过，不再请求
      if (_lastRoutePlaceFetchDate == today) {
        debugPrint('[道路地名] 今天已请求过 ($_lastRoutePlaceFetchDate)，不需要再次请求');
        return false;
      }
      
      // 今天还没请求过，需要请求
      debugPrint('[道路地名] 上次请求日期: $_lastRoutePlaceFetchDate, 今天: $today, 需要请求');
      return true;
    } catch (e) {
      debugPrint('[道路地名] 判断是否请求网络失败: $e');
      // 出错时默认请求网络
      return true;
    }
  }

  /// 从缓存加载数据
  Future<void> _loadFromCache() async {
    try {
      // 从缓存加载所有数据（不过滤level）
      final cachedRoutes = await DBHelper().getAllRoutes();
      final cachedPlaces = await DBHelper().getAllPlaces();
      
      if (cachedRoutes.isNotEmpty || cachedPlaces.isNotEmpty) {
        setState(() {
          _allRouteData = cachedRoutes;
          _allPlaceData = cachedPlaces;
          _filterDataByLevel(); // 根据当前level过滤显示
        });
        debugPrint('[道路地名] 从缓存加载: 道路${cachedRoutes.length}条, 地名${cachedPlaces.length}条');
      }
    } catch (e) {
      debugPrint('[道路地名] 缓存加载失败: $e');
    }
  }

  /// 从网络加载数据
  Future<void> _loadFromNetwork() async {
    try {
      // 并行请求道路和地名数据
      final futures = await Future.wait([
        _loadRouteData(),
        _loadPlaceData(),
      ]);

      debugPrint('[道路地名] 网络数据加载完成');
    } catch (e) {
      debugPrint('[道路地名] 网络加载失败: $e');
      // 网络失败时，如果有缓存数据则不显示错误
      if (_allRouteData.isEmpty && _allPlaceData.isEmpty) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('网络连接失败: $e')),
          );
        }
      } else {
        setState(() {
          _levelStatus = '使用缓存数据（离线模式）';
        });
      }
    }
  }

  /// 加载道路数据
  Future<void> _loadRouteData() async {
    try {
      final resp = await http.post(
        Uri.parse('https://gpsmoveinfo.cn/fc/route_place'),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getroutetableall', 'info': {'wechatid': globalWechatId}}),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final data = json['data'] as List<dynamic>;
          final routes = data.map((e) => e as Map<String, dynamic>).toList();
          
          // 保存到数据库缓存
          await DBHelper().saveRoutes(routes);
          
          setState(() {
            _allRouteData = routes;
          });
          
          debugPrint('[道路] 加载成功: ${routes.length}条，已缓存');
        }
      }
    } catch (e) {
      debugPrint('[道路] 加载失败: $e');
      rethrow;
    }
  }

  /// 加载地名数据
  Future<void> _loadPlaceData() async {
    try {
      final resp = await http.post(
        Uri.parse('https://gpsmoveinfo.cn/fc/route_place'),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getplacetableall', 'info': {'wechatid': globalWechatId}}),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final data = json['data'] as List<dynamic>;
          final places = data.map((e) => e as Map<String, dynamic>).toList();
          
          // 保存到数据库缓存
          await DBHelper().savePlaces(places);
          
          setState(() {
            _allPlaceData = places;
          });
          
          debugPrint('[地名] 加载成功: ${places.length}条，已缓存');
        }
      }
    } catch (e) {
      debugPrint('[地名] 加载失败: $e');
      rethrow;
    }
  }

  /// 计算数据中实际存在的最大level值
  void _calculateMaxLevel() {
    int maxLevel = 1;
    
    // 检查道路数据中的最大level
    for (final route in _allRouteData) {
      try {
        final attributes = route['attributes'] as List<dynamic>?;
        if (attributes != null) {
          for (final attr in attributes) {
            final attrMap = attr as Map<String, dynamic>;
            final columnName = attrMap['columnName']?.toString() ?? '';
            if (columnName == 'level') {
              final levelValue = attrMap['columnValue'];
              if (levelValue != null && levelValue.toString().isNotEmpty) {
                final level = int.tryParse(levelValue.toString()) ?? 1;
                if (level > maxLevel) {
                  maxLevel = level;
                }
              }
              break;
            }
          }
        }
      } catch (e) {
        // 解析失败，忽略
      }
    }
    
    // 检查地名数据中的最大level
    for (final place in _allPlaceData) {
      try {
        final attributes = place['attributes'] as List<dynamic>?;
        if (attributes != null) {
          for (final attr in attributes) {
            final attrMap = attr as Map<String, dynamic>;
            final columnName = attrMap['columnName']?.toString() ?? '';
            if (columnName == 'level') {
              final levelValue = attrMap['columnValue'];
              if (levelValue != null && levelValue.toString().isNotEmpty) {
                final level = int.tryParse(levelValue.toString()) ?? 1;
                if (level > maxLevel) {
                  maxLevel = level;
                }
              }
              break;
            }
          }
        }
      } catch (e) {
        // 解析失败，忽略
      }
    }
    
    setState(() {
      _maxAvailableLevel = maxLevel;
    });
    
    debugPrint('[Level计算] 数据中最大level值为: $_maxAvailableLevel');
  }

  /// 根据当前level过滤数据
  void _filterDataByLevel() {
    if (_currentLevel == 0) {
      // level=0 不显示任何内容
      _displayedRouteData = [];
      _displayedPlaceData = [];
    } else {
      // level<=N 过滤数据（level为空时默认为1）
      _displayedRouteData = _allRouteData.where((route) {
        int level = 1; // 默认值为1
        try {
          final attributes = route['attributes'] as List<dynamic>?;
          if (attributes != null) {
            for (final attr in attributes) {
              final attrMap = attr as Map<String, dynamic>;
              final columnName = attrMap['columnName']?.toString() ?? '';
              if (columnName == 'level') {
                final levelValue = attrMap['columnValue'];
                if (levelValue != null && levelValue.toString().isNotEmpty) {
                  level = int.tryParse(levelValue.toString()) ?? 1;
                }
                break;
              }
            }
          }
        } catch (e) {
          // 解析失败，默认为1
        }
        return level <= _currentLevel;
      }).toList();

      _displayedPlaceData = _allPlaceData.where((place) {
        int level = 1; // 默认值为1
        try {
          final attributes = place['attributes'] as List<dynamic>?;
          if (attributes != null) {
            for (final attr in attributes) {
              final attrMap = attr as Map<String, dynamic>;
              final columnName = attrMap['columnName']?.toString() ?? '';
              if (columnName == 'level') {
                final levelValue = attrMap['columnValue'];
                if (levelValue != null && levelValue.toString().isNotEmpty) {
                  level = int.tryParse(levelValue.toString()) ?? 1;
                }
                break;
              }
            }
          }
        } catch (e) {
          // 解析失败，默认为1
        }
        return level <= _currentLevel;
      }).toList();
    }
    
    debugPrint('[Level过滤] 当前level=$_currentLevel, 显示道路${_displayedRouteData.length}条, 地名${_displayedPlaceData.length}条');
  }

  /// 切换道路和地名显示
  void _toggleRouteAndPlace() async {
    // 如果还没加载过数据，先加载
    if (_allRouteData.isEmpty && _allPlaceData.isEmpty && !_isLoadingRoutePlace) {
      debugPrint('[道路地名] 开始加载数据...');
      await _loadRouteAndPlaceData();
      
      // 打印道路数据
      debugPrint('\n========== 道路数据 ==========');
      debugPrint('道路总数: ${_allRouteData.length}');
      if (_allRouteData.isNotEmpty) {
        debugPrint('第一条道路数据: ${_allRouteData[0]}');
        if (_allRouteData.length > 1) {
          debugPrint('第二条道路数据: ${_allRouteData[1]}');
        }
      }
      debugPrint('================================\n');
      
      // 打印地名数据
      debugPrint('\n========== 地名数据 ==========');
      debugPrint('地名总数: ${_allPlaceData.length}');
      if (_allPlaceData.isNotEmpty) {
        debugPrint('第一名地名数据: ${_allPlaceData[0]}');
        if (_allPlaceData.length > 1) {
          debugPrint('第二名地名数据: ${_allPlaceData[1]}');
        }
      }
      debugPrint('================================\n');
      
      // 加载完成后显示
      if (_allRouteData.isNotEmpty || _allPlaceData.isNotEmpty) {
        // 计算数据中实际存在的最大level
        _calculateMaxLevel();
        
        setState(() {
          _showRouteAndPlace = true;
          _currentLevel = 1; // 初始level为1
          _filterDataByLevel();
          _updateLevelStatus();
        });
      }
      return;
    }

    // 已经加载过数据，循环切换level: 1 -> 2 -> 3 -> 0(隐藏) -> 1...
    // 但要根据实际数据的最大level来调整
    setState(() {
      if (!_showRouteAndPlace) {
        // 第一次点击，开启显示，显示level=1
        _showRouteAndPlace = true;
        _currentLevel = 1;
      } else {
        // 已显示，根据最大可用level循环切换
        if (_currentLevel < _maxAvailableLevel) {
          _currentLevel++;
        } else {
          _currentLevel = 0; // 超过最大level后隐藏
        }
        _filterDataByLevel(); // 重新过滤数据
      }
      _updateLevelStatus();
    });

    debugPrint('[道路地名] 显示状态: $_showRouteAndPlace, Level: $_currentLevel, MaxLevel: $_maxAvailableLevel');
  }

  /// 更新level状态提示
  void _updateLevelStatus() {
    String levelText;
    if (_currentLevel == 0) {
      levelText = '隐藏所有';
    } else {
      levelText = '显示级别≤$_currentLevel';
    }
    
    setState(() {
      _levelStatus = levelText;
    });
    
    // 3秒后清除提示
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted) {
        setState(() {
          _levelStatus = '';
        });
      }
    });
  }

  /// 获取当前位置
  Future<void> _getCurrentLocation() async {
    setState(() {
      _isLocating = true;
    });

    try {
      // 检查位置权限
      LocationPermission permission = await Geolocator.checkPermission();
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission == LocationPermission.denied) {
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('位置权限被拒绝')),
            );
          }
          setState(() {
            _isLocating = false;
          });
          return;
        }
      }

      if (permission == LocationPermission.deniedForever) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('位置权限被永久拒绝，请在设置中开启')),
          );
        }
        setState(() {
          _isLocating = false;
        });
        return;
      }

      // 获取当前位置（WGS-84坐标）
      final position = await Geolocator.getCurrentPosition(
        desiredAccuracy: LocationAccuracy.high,
      );

      // 将WGS-84坐标转换为GCJ-02坐标（火星坐标），用于高德地图
      final gcj02Coord = CoordTransform.wgs84ToGcj02(
        position.latitude,
        position.longitude,
      );

      setState(() {
        _currentPosition = LatLng(gcj02Coord[0], gcj02Coord[1]);
        _isLocating = false;
      });

      // 移动地图到当前位置
      if (_currentPosition != null) {
        _mapController.move(_currentPosition!, 15.0);
      }

      debugPrint('GPS原始坐标(WGS-84): ${position.latitude}, ${position.longitude}');
      debugPrint('转换后坐标(GCJ-02): ${gcj02Coord[0]}, ${gcj02Coord[1]}');
      debugPrint('坐标偏差: ${(gcj02Coord[0] - position.latitude).toStringAsFixed(6)}, ${(gcj02Coord[1] - position.longitude).toStringAsFixed(6)}');
    } catch (e) {
      debugPrint('获取位置失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('获取位置失败: $e')),
        );
      }
      setState(() {
        _isLocating = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('地图中心'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        actions: [
          // 缓存信息按钮
          IconButton(
            icon: Stack(
              children: [
                const Icon(Icons.cloud_download),
                if (_cachedTileCount > 0)
                  Positioned(
                    right: 0,
                    top: 0,
                    child: Container(
                      padding: const EdgeInsets.all(2),
                      decoration: BoxDecoration(
                        color: Colors.red,
                        shape: BoxShape.circle,
                      ),
                      constraints: const BoxConstraints(
                        minWidth: 16,
                        minHeight: 16,
                      ),
                      child: Text(
                        _cachedTileCount > 99 ? '99+' : '$_cachedTileCount',
                        style: const TextStyle(
                          color: Colors.white,
                          fontSize: 8,
                        ),
                        textAlign: TextAlign.center,
                      ),
                    ),
                  ),
              ],
            ),
            onPressed: _showCacheInfo,
            tooltip: '地图缓存信息',
          ),
          // 定位按钮
          IconButton(
            icon: _isLocating
                ? const SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Icon(Icons.my_location),
            onPressed: _isLocating ? null : _getCurrentLocation,
            tooltip: '定位到当前位置',
          ),
        ],
      ),
      body: Stack(
        children: [
          FlutterMap(
            mapController: _mapController,
            options: MapOptions(
              initialCenter: _currentPosition ?? const LatLng(39.9042, 116.4074), // 北京（GCJ-02坐标）
              initialZoom: _currentPosition != null ? 15.0 : 12.0,
              interactionOptions: const InteractionOptions(
                flags: InteractiveFlag.pinchZoom | InteractiveFlag.drag | InteractiveFlag.flingAnimation | InteractiveFlag.pinchMove,
              ),
              // 地图加载完成回调
              onMapReady: () {
                debugPrint('[地图] 地图加载完成');
                setState(() {
                  _mapStatus = '';
                });
              },
            ),
            children: [
              TileLayer(
                // 使用高德卫星地图瓦片（HTTPS）
                urlTemplate: 'https://webst0{s}.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}',
                subdomains: ['1', '2', '3', '4'],
                userAgentPackageName: 'com.example.fuck001',
                // iOS 兼容配置
                retinaMode: true, // 支持高分屏
                tileSize: 256,
                zoomOffset: 0,
                // 加载优化
                maxNativeZoom: 18,
                minZoom: 3,
                maxZoom: 20,
                // 错误处理
                errorImage: const NetworkImage(
                  'https://via.placeholder.com/256/CCCCCC/666666?text=Tile+Error',
                ),
                tileProvider: _isCacheEnabled
                    ? FMTCStore(_cacheStoreName).getTileProvider(
                        settings: FMTCTileProviderSettings(),
                      )
                    : NetworkTileProvider(
                        headers: {
                          'User-Agent': 'Mozilla/5.0 (iPhone; CPU iPhone OS 16_0 like Mac OS X)',
                        },
                      ),
              ),
              // 高德卫星图注记层（叠加在卫星图上显示地名标注）
              TileLayer(
                urlTemplate: 'https://webst0{s}.is.autonavi.com/appmaptile?style=8&x={x}&y={y}&z={z}',
                subdomains: ['1', '2', '3', '4'],
                userAgentPackageName: 'com.example.fuck001',
                retinaMode: true,
                tileSize: 256,
                maxNativeZoom: 18,
                minZoom: 3,
                maxZoom: 20,
                tileProvider: _isCacheEnabled
                    ? FMTCStore(_cacheStoreName).getTileProvider(
                        settings: FMTCTileProviderSettings(),
                      )
                    : NetworkTileProvider(),
              ),
              // 显示当前位置标记（绿色定位图标）
              if (_currentPosition != null)
                MarkerLayer(
                  markers: [
                    Marker(
                      point: _currentPosition!,
                      width: 20,
                      height: 20,
                      child: Container(
                        decoration: BoxDecoration(
                          color: Colors.green,
                          shape: BoxShape.circle,
                          border: Border.all(color: Colors.white, width: 1),
                        ),
                        child: const Icon(
                          Icons.location_on,
                          color: Colors.white,
                          size: 10,
                        ),
                      ),
                    ),
                  ],
                ),
              // 显示道路标记
              if (_showRouteAndPlace && _displayedRouteData.isNotEmpty)
                MarkerLayer(
                  markers: _displayedRouteData.map((route) {
                    String name = '';
                    List<LatLng> roadPoints = [];
                    
                    try {
                      // 解析新的数据格式：从 attributes 数组中提取字段
                      final attributes = route['attributes'] as List<dynamic>?;
                      if (attributes != null) {
                        for (final attr in attributes) {
                          final attrMap = attr as Map<String, dynamic>;
                          final columnName = attrMap['columnName']?.toString() ?? '';
                          final columnValue = attrMap['columnValue']?.toString() ?? '';
                          
                          if (columnName == 'roadinfo' && columnValue.contains(',')) {
                            // 解析道路坐标（新格式：第一组绝对坐标，后续为偏移量）
                            roadPoints = CoordTransform.parseRoadinfoToGcj02(columnValue);
                            debugPrint('[道路] 名称: $name, 坐标点数: ${roadPoints.length}');
                          } else if (columnName == 'roadname') {
                            // 清理道路名称，确保UTF-16安全
                            name = _sanitizeString(columnValue);
                          }
                        }
                      }
                    } catch (e) {
                      debugPrint('[道路] 解析失败: $e, 原始数据: $route');
                    }
                    
                    // 如果坐标点不足，跳过该道路
                    if (roadPoints.isEmpty) {
                      return null;
                    }
                    
                    // 返回一个标记用于调试，实际道路用 PolylineLayer 显示
                    return Marker(
                      point: roadPoints.first,
                      width: 1,
                      height: 1,
                      child: Container(),
                    );
                  }).whereType<Marker>().toList(),
                ),
              // 显示道路线条
              if (_showRouteAndPlace && _displayedRouteData.isNotEmpty)
                PolylineLayer(
                  polylines: _displayedRouteData.map((route) {
                    List<LatLng> roadPoints = [];
                    String name = '';
                    
                    try {
                      final attributes = route['attributes'] as List<dynamic>?;
                      if (attributes != null) {
                        for (final attr in attributes) {
                          final attrMap = attr as Map<String, dynamic>;
                          final columnName = attrMap['columnName']?.toString() ?? '';
                          final columnValue = attrMap['columnValue']?.toString() ?? '';
                          
                          if (columnName == 'roadinfo' && columnValue.contains(',')) {
                            // 解析道路坐标（新格式：第一组绝对坐标，后续为偏移量）
                            roadPoints = CoordTransform.parseRoadinfoToGcj02(columnValue);
                          } else if (columnName == 'roadname') {
                            // 清理道路名称，确保UTF-16安全
                            name = _sanitizeString(columnValue);
                          }
                        }
                      }
                    } catch (e) {
                      debugPrint('[道路线条] 解析失败: $e');
                    }
                    
                    return Polyline(
                      points: roadPoints,
                      strokeWidth: 3,
                      color: Colors.white,
                    );
                  }).toList(),
                ),
              // 显示地名标记
              if (_showRouteAndPlace && _displayedPlaceData.isNotEmpty)
                MarkerLayer(
                  markers: _displayedPlaceData.map((place) {
                    double lat = 0;
                    double lng = 0;
                    String name = '';
                    
                    try {
                      // 解析新的数据格式：从 attributes 数组中提取字段
                      final attributes = place['attributes'] as List<dynamic>?;
                      if (attributes != null) {
                        for (final attr in attributes) {
                          final attrMap = attr as Map<String, dynamic>;
                          final columnName = attrMap['columnName']?.toString() ?? '';
                          final columnValue = attrMap['columnValue']?.toString() ?? '';
                          
                          if (columnName == 'gps' && columnValue.contains(',')) {
                            // 解析 GPS 坐标："纬度, 经度"（WGS-84坐标）
                            final parts = columnValue.split(',');
                            if (parts.length >= 2) {
                              final wgs84Lat = double.tryParse(parts[0].trim()) ?? 0;
                              final wgs84Lng = double.tryParse(parts[1].trim()) ?? 0;
                              
                              // 将 WGS-84 坐标转换为 GCJ-02（火星坐标）
                              if (wgs84Lat != 0 && wgs84Lng != 0) {
                                final gcj02Coord = CoordTransform.wgs84ToGcj02(wgs84Lat, wgs84Lng);
                                lat = gcj02Coord[0];
                                lng = gcj02Coord[1];
                                debugPrint('[地名坐标转换] WGS-84: ($wgs84Lat, $wgs84Lng) -> GCJ-02: ($lat, $lng)');
                              }
                            }
                          } else if (columnName == 'name') {
                            // 清理地名，确保UTF-16安全
                            name = _sanitizeString(columnValue);
                          }
                        }
                      }
                    } catch (e) {
                      debugPrint('[地名] 解析失败: $e, 原始数据: $place');
                    }
                    
                    debugPrint('[地名标记] 名称: $name, 坐标: ($lat, $lng)');
                    
                    // 如果坐标为0，跳过该标记
                    if (lat == 0 && lng == 0) {
                      return null;
                    }
                    
                    // 清理地名字符串，确保UTF-16安全
                    String safeName = _sanitizeString(name);
                    
                    return Marker(
                      point: LatLng(lat, lng),
                      width: 24,
                      height: 24,
                      alignment: Alignment.center,
                      child: Stack(
                        clipBehavior: Clip.none,
                        children: [
                          // 红色圆圈图标（固定在GPS坐标点）
                          Container(
                            width: 24,
                            height: 24,
                            decoration: BoxDecoration(
                              color: Colors.red,
                              shape: BoxShape.circle,
                              border: Border.all(color: Colors.white, width: 2),
                            ),
                            child: const Icon(
                              Icons.arrow_drop_down,
                              color: Colors.white,
                              size: 18,
                            ),
                          ),
                          // 名称标签（向右延伸，不影响图标位置）
                          Positioned(
                            left: 30,
                            top: 2,
                            child: Container(
                              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                              decoration: BoxDecoration(
                                color: Colors.white,
                                borderRadius: BorderRadius.circular(6),
                                boxShadow: [
                                  BoxShadow(
                                    color: Colors.black.withOpacity(0.15),
                                    blurRadius: 3,
                                    offset: const Offset(0, 1),
                                  ),
                                ],
                              ),
                              child: Text(
                                safeName,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(
                                  fontSize: 12,
                                  fontWeight: FontWeight.w500,
                                  color: Colors.black87,
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                    );
                  }).whereType<Marker>().toList(),
                ),
              // 显示设备位置（图标+名称标签）
              if (_showDevices && _devicePositions.isNotEmpty)
                MarkerLayer(
                  markers: _devicePositions.map((device) {
                    final deviceId = device['deviceId'] as String;
                    String safeDeviceName = _sanitizeString(device['name'].toString());
                    final fromBluetooth = device['fromBluetooth'] as bool? ?? false;
                    final gpsExpired = device['gps_expired'] as bool? ?? false;
                    
                    return Marker(
                      point: LatLng(device['lat'], device['lng']),
                      width: 24,
                      height: 24,
                      alignment: Alignment.center,
                      child: Stack(
                        clipBehavior: Clip.none,
                        children: [
                          // GPS图标：1小时内绿色，超过1小时灰色 —— 点击显示气泡
                          GestureDetector(
                            onTap: () => setState(() { _selectedDeviceId = deviceId; }),
                            child: fromBluetooth
                              ? AnimatedBuilder(
                                  animation: _blinkAnimationController!,
                                  builder: (context, child) {
                                    final value = _blinkAnimationController!.value;
                                    final isRed = value >= 0.5;
                                    final dotColor = gpsExpired ? Colors.grey : (isRed ? Colors.red : Colors.green);
                                    final circleColor = gpsExpired ? Colors.grey[300] : Colors.white;
                                    return Container(
                                      width: 24,
                                      height: 24,
                                      decoration: BoxDecoration(
                                        color: circleColor,
                                        shape: BoxShape.circle,
                                        border: Border.all(color: dotColor, width: 2),
                                      ),
                                      child: CustomPaint(
                                        size: const Size(24, 24),
                                        painter: _DeviceTrianglePainter(dotColor),
                                      ),
                                    );
                                  },
                                )
                              : Container(
                                  width: 24,
                                  height: 24,
                                  decoration: BoxDecoration(
                                    color: gpsExpired ? Colors.grey[300] : Colors.white,
                                    shape: BoxShape.circle,
                                    border: Border.all(color: gpsExpired ? Colors.grey : Colors.green, width: 2),
                                  ),
                                  child: CustomPaint(
                                    size: const Size(24, 24),
                                    painter: _DeviceTrianglePainter(gpsExpired ? Colors.grey : Colors.green),
                                  ),
                                ),
                          ),
                          // 名称标签（始终显示，向右延伸）
                          Positioned(
                            left: 30,
                            top: 2,
                            child: Container(
                              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                              decoration: BoxDecoration(
                                color: Colors.white,
                                borderRadius: BorderRadius.circular(6),
                                boxShadow: [
                                  BoxShadow(
                                    color: Colors.black.withValues(alpha: 0.15),
                                    blurRadius: 3,
                                    offset: const Offset(0, 1),
                                  ),
                                ],
                              ),
                              child: Text(
                                safeDeviceName,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(
                                  fontSize: 12,
                                  fontWeight: FontWeight.w500,
                                  color: Colors.black87,
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                    );
                  }).toList(),
                ),
              // 气泡层（独立MarkerLayer，渲染在图标层之上，确保不被遮挡）
              if (_showDevices && _selectedDeviceId != null)
                MarkerLayer(
                  markers: () {
                    final devices = _devicePositions.where(
                      (d) => d['deviceId'] == _selectedDeviceId,
                    ).toList();
                    if (devices.isEmpty) return <Marker>[];
                    final device = devices.first;
                    final gpsTimeStr = device['gpsTime']?.toString() ?? '';
                    final safeDeviceName = _sanitizeString(device['name'].toString());
                    return [
                      Marker(
                        point: LatLng(device['lat'], device['lng']),
                        width: 24,
                        height: 24,
                        alignment: Alignment.center,
                        child: Stack(
                          clipBehavior: Clip.none,
                          children: [
                            Positioned(
                              left: 12,
                              bottom: 24,
                              child: GestureDetector(
                                onTap: () => setState(() { _selectedDeviceId = null; }),
                                behavior: HitTestBehavior.opaque,
                                child: SizedBox(
                                  width: 240,
                                  child: LayoutBuilder(
                                    builder: (context, constraints) {
                                      return Transform.translate(
                                        offset: const Offset(-120, 0),
                                        transformHitTests: true,
                                        child: Column(
                                          mainAxisSize: MainAxisSize.min,
                                          children: [
                                            Container(
                                              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                                              decoration: BoxDecoration(
                                                color: Colors.white,
                                                borderRadius: BorderRadius.circular(8),
                                                boxShadow: [
                                                  BoxShadow(
                                                    color: Colors.black.withValues(alpha: 0.15),
                                                    blurRadius: 4,
                                                    offset: const Offset(0, 1),
                                                  ),
                                                ],
                                              ),
                                              child: Column(
                                                mainAxisSize: MainAxisSize.min,
                                                crossAxisAlignment: CrossAxisAlignment.center,
                                                children: [
                                                  if (safeDeviceName.isNotEmpty)
                                                    Text(
                                                      safeDeviceName,
                                                      maxLines: 1,
                                                      overflow: TextOverflow.ellipsis,
                                                      style: const TextStyle(
                                                        fontSize: 14,
                                                        fontWeight: FontWeight.bold,
                                                        color: Colors.black,
                                                      ),
                                                    ),
                                                  if (gpsTimeStr.isNotEmpty)
                                                    Text(
                                                      gpsTimeStr,
                                                      maxLines: 1,
                                                      overflow: TextOverflow.ellipsis,
                                                      style: const TextStyle(
                                                        fontSize: 12,
                                                        color: Colors.black54,
                                                      ),
                                                    ),
                                                ],
                                              ),
                                            ),
                                            CustomPaint(
                                              size: const Size(10, 6),
                                              painter: _TrianglePainter(),
                                            ),
                                          ],
                                        ),
                                      );
                                    },
                                  ),
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ];
                  }(),
                ),
            ],
          ),
          // 地图状态提示
          if (_mapStatus.isNotEmpty)
            Positioned(
              top: 100,
              left: 20,
              right: 20,
              child: Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: Colors.black54,
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Text(
                  _mapStatus,
                  style: const TextStyle(color: Colors.white, fontSize: 14),
                  textAlign: TextAlign.center,
                ),
              ),
            ),
          // Level状态提示
         
          // 左下角道路地名切换按钮
          Positioned(
            left: 16,
            bottom: 16,
            child: FloatingActionButton.small(
              heroTag: 'map_route_place_fab',
              onPressed: _toggleRouteAndPlace,
              backgroundColor: _showRouteAndPlace ? Colors.blue : Colors.white,
              child: _isLoadingRoutePlace
                  ? const SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        color: Colors.blue,
                      ),
                    )
                  : Icon(
                      _showRouteAndPlace ? Icons.visibility : Icons.visibility_off,
                      color: _showRouteAndPlace ? Colors.white : Colors.black54,
                    ),
              tooltip: _showRouteAndPlace ? '隐藏道路和地名' : '显示道路和地名',
            ),
          ),
          // 右下角设备位置切换按钮
          Positioned(
            right: 16,
            bottom: 16,
            child: GestureDetector(
              onLongPress: _refreshDevicePositions,
              child: FloatingActionButton.small(
                heroTag: 'map_device_display_fab',
                onPressed: _toggleDeviceDisplay,
                backgroundColor: _showDevices ? Colors.red : Colors.white,
                child: Icon(
                  _showDevices ? Icons.devices_other : Icons.devices_other_outlined,
                  color: _showDevices ? Colors.white : Colors.black54,
                ),
                tooltip: _showDevices ? '隐藏设备位置（长按刷新）' : '显示设备位置（长按刷新）',
              ),
            ),
          ),
        ],
      ),
      floatingActionButton: FloatingActionButton(
        heroTag: 'map_location_fab', // 设置唯一tag避免Hero动画冲突
        onPressed: _getCurrentLocation,
        child: _isLocating
            ? const SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            : const Icon(Icons.my_location),
      ),
    );
  }
}

/// 向下小三角箭头绘制器（用于气泡底部指向图标）
/// 设备图标内填充倒三角（正好以圆内径为最大容积）
/// 通用GPS时间解析：支持多种格式
/// - "2026/9/18 23:17:43"（斜线不补零）
/// - "2026-09-18 23:17:43"（横线补零）
/// - Unix时间戳（秒或毫秒）
DateTime? _parseGpsTime(String timeStr) {
  if (timeStr.isEmpty) return null;
  // 1. 尝试纯数字（Unix时间戳）
  final ts = int.tryParse(timeStr);
  if (ts != null) {
    // 毫秒级时间戳（13位以上）
    if (ts > 9999999999) return DateTime.fromMillisecondsSinceEpoch(ts);
    // 秒级时间戳
    return DateTime.fromMillisecondsSinceEpoch(ts * 1000);
  }
  // 2. 尝试日期字符串
  try {
    if (timeStr.contains(' ')) {
      final parts = timeStr.split(' ');
      if (parts.length >= 2) {
        // 日期部分：支持 "/" 或 "-" 分隔
        final dp = parts[0].replaceAll('/', '-').split('-');
        final tp = parts[1].split(':');
        if (dp.length >= 3 && tp.length >= 2) {
          return DateTime(
            int.parse(dp[0]), int.parse(dp[1]), int.parse(dp[2]),
            int.parse(tp[0]), int.parse(tp[1]),
            tp.length >= 3 ? int.parse(tp[2]) : 0,
          );
        }
      }
    }
    // 3. 尝试 ISO 格式
    return DateTime.tryParse(timeStr);
  } catch (_) {}
  return null;
}

class _DeviceTrianglePainter extends CustomPainter {
  final Color color;
  _DeviceTrianglePainter(this.color);

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = color
      ..style = PaintingStyle.fill;
    final w = size.width;
    final h = size.height;
    // 倒三角填满圆内径：顶边在圆心偏上，底点在圆底部
    final path = ui.Path()
      ..moveTo(w * 0.15, h * 0.28)
      ..lineTo(w * 0.85, h * 0.28)
      ..lineTo(w * 0.5, h * 0.88)
      ..close();
    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(covariant _DeviceTrianglePainter oldDelegate) => color != oldDelegate.color;
}

class _TrianglePainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = Colors.white
      ..style = PaintingStyle.fill;
    final path = ui.Path()
      ..moveTo(0, 0)
      ..lineTo(size.width, 0)
      ..lineTo(size.width / 2, size.height)
      ..close();
    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}