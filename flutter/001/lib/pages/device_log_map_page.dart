import 'dart:convert';
import 'dart:math' as math;
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_map_tile_caching/flutter_map_tile_caching.dart';
import 'package:latlong2/latlong.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';
import 'package:geolocator/geolocator.dart';
import '../utils/coord_transform.dart';
import '../utils/db_helper.dart';
import '../main.dart'; // 全局 globalWechatId

/// 设备日志定位地图页面（卫星地图显示单条记录的GPS坐标）
class DeviceLogMapPage extends StatefulWidget {
  final double latitude;
  final double longitude;
  final String time;
  final String deviceId;
  final String type;
  final String deviceName;

  const DeviceLogMapPage({
    super.key,
    required this.latitude,
    required this.longitude,
    required this.time,
    required this.deviceId,
    required this.type,
    this.deviceName = '',
  });

  @override
  State<DeviceLogMapPage> createState() => _DeviceLogMapPageState();
}

class _DeviceLogMapPageState extends State<DeviceLogMapPage> {
  final MapController _mapController = MapController();
  String _mapStatus = '地图加载中...';

  // 道路和地名数据
  List<Map<String, dynamic>> _allRouteData = [];
  List<Map<String, dynamic>> _allPlaceData = [];
  List<Map<String, dynamic>> _displayedRouteData = [];
  List<Map<String, dynamic>> _displayedPlaceData = [];
  bool _showRouteAndPlace = false;
  bool _isLoadingRoutePlace = false;
  int _currentLevel = 1;
  String _levelStatus = '';
  int _maxAvailableLevel = 1;
  String? _lastRoutePlaceFetchDate;

  // 我的位置
  LatLng? _myLocation;
  String _distanceText = '';
  double _distanceMeters = 0;
  bool _showDistanceLabel = true;
  LatLng? _deviceMarkerPoint; // 设备标记点（GCJ-02）
  bool _showBubble = true; // 是否显示设备信息气泡
  bool _isSendingGps = false; // 是否正在发送上报GPS指令

  /// 生成虚线段（手动模拟，因当前flutter_map版本不支持dashPattern）
  /// dashLen: 每段虚线长度(米)，gapLen: 间隔长度(米)
  List<Polyline> _buildDashedLine(LatLng from, LatLng to, {double dashLen = 30.0, double gapLen = 20.0}) {
    final distance = const Distance();
    final totalMeters = distance.as(LengthUnit.Meter, from, to);
    if (totalMeters < 1) return [];

    final bearing = distance.bearing(from, to);
    final step = dashLen + gapLen;
    final result = <Polyline>[];

    for (double d = 0; d < totalMeters; d += step) {
      final dashEnd = math.min(d + dashLen, totalMeters);
      final p1 = distance.offset(from, d, bearing);
      final p2 = distance.offset(from, dashEnd, bearing);
      result.add(Polyline(
        points: [p1, p2],
        strokeWidth: 2,
        color: Colors.cyanAccent,
      ));
    }
    return result;
  }

  /// 检查像素距离，决定是否显示距离标签
  void _checkPixelDistance() {
    if (_myLocation == null || _deviceMarkerPoint == null || _distanceMeters < 10) return;
    try {
      final camera = _mapController.camera;
      final p1 = camera.project(_myLocation!);
      final p2 = camera.project(_deviceMarkerPoint!);
      final dx = p2.x - p1.x;
      final dy = p2.y - p1.y;
      final pixelDist = math.sqrt(dx * dx + dy * dy);
      final shouldShow = pixelDist >= 50;
      if (shouldShow != _showDistanceLabel) {
        setState(() { _showDistanceLabel = shouldShow; });
      }
    } catch (_) {}
  }

  /// 发送上报GPS指令（通过最优2台中继转发）
  Future<void> _sendUpGpsCommand() async {
    if (_isSendingGps) return;
    setState(() { _isSendingGps = true; });

    try {
      final targetDeviceId = widget.deviceId;

      // 1. 获取RSSI最优的中继设备列表
      final rssResp = await http.post(
        Uri.parse('https://gpsmoveinfo.cn/fc/device'),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getDeviceBestRssibyId',
          'info': {'limit': 3, 'deviceId': targetDeviceId, 'wechatid': globalWechatId},
        }),
      );
      debugPrint('[上报GPS] BestRssi响应: ${rssResp.body}');

      // 收集所有候选中继，按RSSI绝对值升序排列，取前2台
      final List<Map<String, String>> relayList = []; // [{deviceId, deviceName, productKey}]

      if (rssResp.statusCode == 200) {
        final rssJson = jsonDecode(rssResp.body) as Map<String, dynamic>;
        if (rssJson['status'] == 'success') {
          final data = rssJson['data'];
          if (data is List && data.isNotEmpty) {
            // 解析每条记录的 upDateDevice 和 rssi
            final candidates = <Map<String, dynamic>>[];
            for (final item in data) {
              final attrs = item['attributes'] as List<dynamic>? ?? [];
              String? rssiStr;
              String? upDateDeviceStr;
              for (final attr in attrs) {
                final name = attr['columnName']?.toString() ?? '';
                final value = attr['columnValue'];
                if (name == 'rssi') rssiStr = value?.toString();
                if (name == 'upDateDevice') upDateDeviceStr = value?.toString();
              }
              final rssi = double.tryParse(rssiStr ?? '') ?? double.infinity;
              if (upDateDeviceStr != null && upDateDeviceStr.isNotEmpty) {
                candidates.add({'upDateDevice': upDateDeviceStr, 'absRssi': rssi.abs()});
              }
            }
            // 按RSSI绝对值升序排序
            candidates.sort((a, b) => (a['absRssi'] as double).compareTo(b['absRssi'] as double));

            // 去重后取前2台，匹配本地中继设备信息
            final allDevices = await DBHelper().getDevices();
            final seen = <String>{};
            for (final c in candidates) {
              if (relayList.length >= 2) break;
              final upDateDevice = c['upDateDevice'] as String;
              if (seen.contains(upDateDevice)) continue;
              seen.add(upDateDevice);
              final matched = allDevices.firstWhere(
                (d) => d['deviceId']?.toString() == upDateDevice,
                orElse: () => <String, dynamic>{},
              );
              if (matched.isNotEmpty) {
                final dn = matched['DeviceName']?.toString() ?? '';
                final pk = matched['ProductKey']?.toString() ?? '';
                if (dn.isNotEmpty && pk.isNotEmpty) {
                  relayList.add({'deviceId': upDateDevice, 'deviceName': dn, 'productKey': pk});
                  debugPrint('[上报GPS] 选中继#${relayList.length}: $upDateDevice, rssi=${c['absRssi']}');
                }
              }
            }
          }
        }
      }

      if (relayList.isEmpty) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('未找到可用的中继转发设备')),
          );
        }
        return;
      }

      // 2. 构建指令（自动添加deviceId）
      final command = jsonEncode({'cmd': 'upgps', 'value': '0', 'deviceId': targetDeviceId});

      // 3. 并行向所有选中的中继发送指令
      final futures = relayList.map((relay) async {
        final resp = await http.post(
          Uri.parse('https://gpsmoveinfo.cn/fc/sendtodtucmd'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({
            'action': 'com',
            'deviceName': relay['deviceName'],
            'productKey': relay['productKey'],
            'msg': command,
            'timestamp': DateTime.now().millisecondsSinceEpoch,
            'wechatid': globalWechatId,
          }),
        );
        debugPrint('[上报GPS] 中继${relay['deviceId']} 响应: ${resp.statusCode} ${resp.body}');
        return resp.statusCode == 200;
      }).toList();

      final results = await Future.wait(futures);
      final successCount = results.where((r) => r).length;

      if (mounted) {
        if (successCount > 0) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('上报GPS指令已发送（$successCount/${relayList.length}台中继）')),
          );
        } else {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('发送失败，请检查网络')),
          );
        }
      }
    } catch (e) {
      debugPrint('[上报GPS] 发送失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('发送指令失败')),
        );
      }
    } finally {
      if (mounted) {
        setState(() { _isSendingGps = false; });
      }
    }
  }

  @override
  void initState() {
    super.initState();
    _restoreLastFetchDate();
    _getCurrentLocation();
  }

  /// 获取我的当前位置
  Future<void> _getCurrentLocation() async {
    try {
      LocationPermission permission = await Geolocator.checkPermission();
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission == LocationPermission.denied) {
          debugPrint('[定位详情] 位置权限被拒绝');
          return;
        }
      }
      if (permission == LocationPermission.deniedForever) {
        debugPrint('[定位详情] 位置权限被永久拒绝');
        return;
      }
      final position = await Geolocator.getCurrentPosition(
        desiredAccuracy: LocationAccuracy.high,
      );
      // WGS-84 转 GCJ-02（与高德地图对齐）
      final gcj02 = CoordTransform.wgs84ToGcj02(position.latitude, position.longitude);
      final myPoint = LatLng(gcj02[0], gcj02[1]);

      // 计算与设备位置的距离
      final deviceGcj02 = CoordTransform.wgs84ToGcj02(widget.latitude, widget.longitude);
      final devicePoint = LatLng(deviceGcj02[0], deviceGcj02[1]);
      final distance = const Distance().as(LengthUnit.Meter, myPoint, devicePoint);
      String distText;
      if (distance >= 1000) {
        distText = '${(distance / 1000).toStringAsFixed(1)}km';
      } else {
        distText = '${distance.toStringAsFixed(0)}m';
      }
      debugPrint('[定位详情] 我的位置: ($myPoint), 距离: $distText');

      setState(() {
        _myLocation = myPoint;
        _distanceText = distText;
        _distanceMeters = distance;
        _deviceMarkerPoint = devicePoint;
        // 距离小于10米不显示距离标签
        _showDistanceLabel = distance >= 10;
      });
    } catch (e) {
      debugPrint('[定位详情] 获取位置失败: $e');
    }
  }

  Future<void> _restoreLastFetchDate() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final saved = prefs.getString('last_route_place_fetch_date');
      if (saved != null) setState(() { _lastRoutePlaceFetchDate = saved; });
    } catch (_) {}
  }

  /// 类型颜色
  Color get _typeColor {
    switch (widget.type) {
      case '1':
        return const Color(0xFF4CAF50);
      case '5':
        return const Color(0xFF2196F3);
      default:
        return const Color(0xFF4CAF50);
    }
  }

  /// 清理字符串确保UTF-16安全
  String _sanitizeString(String input) {
    if (input.isEmpty) return '';
    try {
      final cleaned = input.replaceAll(RegExp(r'[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]'), '');
      final result = String.fromCharCodes(cleaned.codeUnits);
      return result.isEmpty ? '' : result;
    } catch (_) {
      return '';
    }
  }

  /// 加载道路和地名数据
  Future<void> _loadRouteAndPlaceData() async {
    setState(() { _isLoadingRoutePlace = true; });
    try {
      await _loadFromCache();
      debugPrint('[道路地名] 缓存加载后: allRoute=${_allRouteData.length}, allPlace=${_allPlaceData.length}, displayedRoute=${_displayedRouteData.length}, displayedPlace=${_displayedPlaceData.length}');
      final shouldFetch = await _shouldFetchFromNetwork();
      if (shouldFetch) {
        await _loadFromNetwork();
        debugPrint('[道路地名] 网络加载后: allRoute=${_allRouteData.length}, allPlace=${_allPlaceData.length}');
        final now = DateTime.now();
        final today = '${now.year}-${now.month.toString().padLeft(2, '0')}-${now.day.toString().padLeft(2, '0')}';
        setState(() { _lastRoutePlaceFetchDate = today; });
        final prefs = await SharedPreferences.getInstance();
        await prefs.setString('last_route_place_fetch_date', today);
      }
    } catch (e) {
      debugPrint('[道路地名] 加载失败: $e');
    } finally {
      setState(() { _isLoadingRoutePlace = false; });
    }
  }

  Future<bool> _shouldFetchFromNetwork() async {
    if (_allRouteData.isEmpty && _allPlaceData.isEmpty) return true;
    final now = DateTime.now();
    final today = '${now.year}-${now.month.toString().padLeft(2, '0')}-${now.day.toString().padLeft(2, '0')}';
    if (_lastRoutePlaceFetchDate == today) return false;
    return true;
  }

  Future<void> _loadFromCache() async {
    try {
      final cachedRoutes = await DBHelper().getAllRoutes();
      final cachedPlaces = await DBHelper().getAllPlaces();
      if (cachedRoutes.isNotEmpty || cachedPlaces.isNotEmpty) {
        setState(() {
          _allRouteData = cachedRoutes;
          _allPlaceData = cachedPlaces;
          _filterDataByLevel();
        });
      }
    } catch (e) {
      debugPrint('[道路地名] 缓存加载失败: $e');
    }
  }

  Future<void> _loadFromNetwork() async {
    try {
      await Future.wait([_loadRouteData(), _loadPlaceData()]);
    } catch (e) {
      debugPrint('[道路地名] 网络加载失败: $e');
      if (_allRouteData.isEmpty && _allPlaceData.isEmpty && mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('网络连接失败: $e')),
        );
      }
    }
  }

  Future<void> _loadRouteData() async {
    final resp = await http.post(
      Uri.parse('https://gpsmoveinfo.cn/fc/route_place'),
      headers: {'Content-Type': 'application/json'},
      body: jsonEncode({'action': 'getroutetableall', 'info': {'wechatid': globalWechatId}}),
    );
    if (resp.statusCode == 200) {
      final json = jsonDecode(resp.body) as Map<String, dynamic>;
      if (json['status'] == 'success') {
        final data = (json['data'] as List<dynamic>).map((e) => e as Map<String, dynamic>).toList();
        await DBHelper().saveRoutes(data);
        setState(() { _allRouteData = data; });
      }
    }
  }

  Future<void> _loadPlaceData() async {
    final resp = await http.post(
      Uri.parse('https://gpsmoveinfo.cn/fc/route_place'),
      headers: {'Content-Type': 'application/json'},
      body: jsonEncode({'action': 'getplacetableall', 'info': {'wechatid': globalWechatId}}),
    );
    if (resp.statusCode == 200) {
      final json = jsonDecode(resp.body) as Map<String, dynamic>;
      if (json['status'] == 'success') {
        final data = (json['data'] as List<dynamic>).map((e) => e as Map<String, dynamic>).toList();
        await DBHelper().savePlaces(data);
        setState(() { _allPlaceData = data; });
      }
    }
  }

  void _calculateMaxLevel() {
    int maxLevel = 1;
    // 检查道路数据中的最大level
    for (final route in _allRouteData) {
      try {
        final attributes = route['attributes'] as List<dynamic>?;
        if (attributes != null) {
          for (final attr in attributes) {
            final attrMap = attr as Map<String, dynamic>;
            if (attrMap['columnName'] == 'level') {
              final v = int.tryParse(attrMap['columnValue']?.toString() ?? '') ?? 1;
              if (v > maxLevel) maxLevel = v;
              break;
            }
          }
        }
      } catch (_) {}
    }
    // 检查地名数据中的最大level
    for (final place in _allPlaceData) {
      try {
        final attributes = place['attributes'] as List<dynamic>?;
        if (attributes != null) {
          for (final attr in attributes) {
            final attrMap = attr as Map<String, dynamic>;
            if (attrMap['columnName'] == 'level') {
              final v = int.tryParse(attrMap['columnValue']?.toString() ?? '') ?? 1;
              if (v > maxLevel) maxLevel = v;
              break;
            }
          }
        }
      } catch (_) {}
    }
    setState(() { _maxAvailableLevel = maxLevel; });
    debugPrint('[Level计算] 数据中最大level值为: $_maxAvailableLevel');
  }

  void _filterDataByLevel() {
    if (_currentLevel == 0) {
      _displayedRouteData = [];
      _displayedPlaceData = [];
    } else {
      _displayedRouteData = _allRouteData.where((route) {
        int level = 1;
        try {
          final attributes = route['attributes'] as List<dynamic>?;
          if (attributes != null) {
            for (final attr in attributes) {
              final attrMap = attr as Map<String, dynamic>;
              if (attrMap['columnName'] == 'level') {
                level = int.tryParse(attrMap['columnValue']?.toString() ?? '') ?? 1;
                break;
              }
            }
          }
        } catch (_) {}
        return level <= _currentLevel;
      }).toList();
      _displayedPlaceData = _allPlaceData.where((place) {
        int level = 1;
        try {
          final attributes = place['attributes'] as List<dynamic>?;
          if (attributes != null) {
            for (final attr in attributes) {
              final attrMap = attr as Map<String, dynamic>;
              if (attrMap['columnName'] == 'level') {
                level = int.tryParse(attrMap['columnValue']?.toString() ?? '') ?? 1;
                break;
              }
            }
          }
        } catch (_) {}
        return level <= _currentLevel;
      }).toList();
    }
    debugPrint('[Level过滤] 当前level=$_currentLevel, 全部道路=${_allRouteData.length}, 显示道路=${_displayedRouteData.length}, 全部地名=${_allPlaceData.length}, 显示地名=${_displayedPlaceData.length}');
  }

  void _toggleRouteAndPlace() async {
    debugPrint('[Toggle] 开始切换: showRouteAndPlace=$_showRouteAndPlace, currentLevel=$_currentLevel, maxLevel=$_maxAvailableLevel, allRoute=${_allRouteData.length}, allPlace=${_allPlaceData.length}, isLoading=$_isLoadingRoutePlace');
    if (_allRouteData.isEmpty && _allPlaceData.isEmpty && !_isLoadingRoutePlace) {
      await _loadRouteAndPlaceData();
      debugPrint('[Toggle] 加载完成: allRoute=${_allRouteData.length}, allPlace=${_allPlaceData.length}');
      if (_allRouteData.isNotEmpty || _allPlaceData.isNotEmpty) {
        _calculateMaxLevel();
        setState(() {
          _showRouteAndPlace = true;
          _currentLevel = 1;
          _filterDataByLevel();
          _updateLevelStatus();
        });
        debugPrint('[Toggle] 首次显示: displayedRoute=${_displayedRouteData.length}, displayedPlace=${_displayedPlaceData.length}');
      } else {
        debugPrint('[Toggle] 数据为空，无法显示');
      }
      return;
    }
    debugPrint('[Toggle] 切换后: showRouteAndPlace=$_showRouteAndPlace, currentLevel=$_currentLevel');
    setState(() {
      if (!_showRouteAndPlace) {
        _showRouteAndPlace = true;
        _currentLevel = 1;
        _filterDataByLevel();
      } else {
        if (_currentLevel < _maxAvailableLevel) {
          _currentLevel++;
        } else {
          _currentLevel = 0;
        }
        _filterDataByLevel();
      }
      _updateLevelStatus();
    });
    debugPrint('[Toggle] 切换完成: displayedRoute=${_displayedRouteData.length}, displayedPlace=${_displayedPlaceData.length}');
  }

  void _updateLevelStatus() {
    final text = _currentLevel == 0 ? '隐藏所有' : '显示级别≤$_currentLevel';
    setState(() { _levelStatus = text; });
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted) setState(() { _levelStatus = ''; });
    });
  }

  @override
  Widget build(BuildContext context) {
    // WGS-84 坐标转 GCJ-02（火星坐标，高德卫星图使用）
    final gcj02Coord = CoordTransform.wgs84ToGcj02(widget.latitude, widget.longitude);
    final markerPoint = LatLng(gcj02Coord[0], gcj02Coord[1]);

    return Scaffold(
      backgroundColor: const Color(0xFF1A1A2E),
      appBar: AppBar(
        title: const Text('定位详情'),
        backgroundColor: const Color(0xFF16213E),
        foregroundColor: Colors.white,
        elevation: 0,
      ),
      body: Stack(
        children: [
          // 卫星地图
          FlutterMap(
            mapController: _mapController,
            options: MapOptions(
              initialCenter: markerPoint,
              initialZoom: 16.0,
              interactionOptions: const InteractionOptions(
                flags: InteractiveFlag.pinchZoom |
                    InteractiveFlag.drag |
                    InteractiveFlag.flingAnimation |
                    InteractiveFlag.pinchMove,
              ),
              onMapReady: () {
                setState(() {
                  _mapStatus = '';
                });
              },
              onMapEvent: (event) {
                // 地图缩放/移动结束后检查像素距离
                _checkPixelDistance();
              },
            ),
            children: [
              // 高德卫星影像瓦片
              TileLayer(
                urlTemplate: 'https://webst0{s}.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}',
                subdomains: ['1', '2', '3', '4'],
                userAgentPackageName: 'com.example.fuck001',
                retinaMode: true,
                tileSize: 256,
                zoomOffset: 0,
                maxNativeZoom: 18,
                minZoom: 3,
                maxZoom: 20,
                errorImage: const NetworkImage(
                  'https://via.placeholder.com/256/CCCCCC/666666?text=Tile+Error',
                ),
                tileProvider: FMTCStore('map_cache').getTileProvider(
                  settings: FMTCTileProviderSettings(),
                ),
              ),
              // 高德卫星图注记层
              TileLayer(
                urlTemplate: 'https://webst0{s}.is.autonavi.com/appmaptile?style=8&x={x}&y={y}&z={z}',
                subdomains: ['1', '2', '3', '4'],
                userAgentPackageName: 'com.example.fuck001',
                retinaMode: true,
                tileSize: 256,
                maxNativeZoom: 18,
                minZoom: 3,
                maxZoom: 20,
                tileProvider: FMTCStore('map_cache').getTileProvider(
                  settings: FMTCTileProviderSettings(),
                ),
              ),
              // 我的位置到设备的虚线连接 + 距离
              if (_myLocation != null) ...[
                // 虚线（手动分段模拟）
                PolylineLayer(
                  polylines: _buildDashedLine(_myLocation!, markerPoint),
                ),
                // 我的位置蓝色圆点
                MarkerLayer(
                  markers: [
                    Marker(
                      point: _myLocation!,
                      width: 20,
                      height: 20,
                      alignment: Alignment.center,
                      child: Container(
                        width: 20,
                        height: 20,
                        decoration: BoxDecoration(
                          color: const Color(0xFF2196F3),
                          shape: BoxShape.circle,
                          border: Border.all(color: Colors.white, width: 2.5),
                          boxShadow: [
                            BoxShadow(
                              color: const Color(0xFF2196F3).withValues(alpha: 0.5),
                              blurRadius: 8,
                              spreadRadius: 2,
                            ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
                // 距离标签（两点中点位置）—— 距离<10米或像素距离<50px时隐藏
                if (_showDistanceLabel && _distanceMeters >= 10)
                MarkerLayer(
                  markers: [
                    Marker(
                      point: LatLng(
                        (_myLocation!.latitude + markerPoint.latitude) / 2,
                        (_myLocation!.longitude + markerPoint.longitude) / 2,
                      ),
                      width: 80,
                      height: 28,
                      alignment: Alignment.center,
                      child: Container(
                        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                        decoration: BoxDecoration(
                          color: Colors.black87,
                          borderRadius: BorderRadius.circular(14),
                          border: Border.all(color: Colors.cyanAccent.withValues(alpha: 0.6), width: 1),
                        ),
                        child: Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            const Icon(Icons.straighten, size: 13, color: Colors.cyanAccent),
                            const SizedBox(width: 3),
                            Text(
                              _distanceText,
                              style: const TextStyle(
                                color: Colors.cyanAccent,
                                fontSize: 12,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
              ],

              // 设备位置标记（绿色圆圈 + 上方合并气泡：设备名+时间+向下箭头）
              MarkerLayer(
                markers: [
                  Marker(
                    point: markerPoint,
                    width: 24,
                    height: 24,
                    alignment: Alignment.center,
                    child: Stack(
                      clipBehavior: Clip.none,
                      children: [
                        // 绿色圆圈图标（固定在GPS坐标点）—— 点击显示气泡
                        GestureDetector(
                          onTap: () => setState(() { _showBubble = true; }),
                          child: Container(
                            width: 24,
                            height: 24,
                            decoration: BoxDecoration(
                              color: Colors.green,
                              shape: BoxShape.circle,
                              border: Border.all(color: Colors.white, width: 2),
                            ),
                            child: const Icon(
                              Icons.arrow_drop_down,
                              color: Colors.white,
                              size: 18,
                            ),
                          ),
                        ),
                        // 合并气泡（设备名+时间+向下箭头，位于图标上方）—— 点击隐藏
                        if (_showBubble && (widget.deviceName.isNotEmpty || (widget.time.isNotEmpty && widget.time != '—')))
                          Positioned(
                            left: 12,
                            bottom: 24,
                            child: GestureDetector(
                              onTap: () => setState(() { _showBubble = false; }),
                              behavior: HitTestBehavior.opaque,
                              child: SizedBox(
                                width: 240,
                                child: LayoutBuilder(
                                  builder: (context, constraints) {
                                    return Transform.translate(
                                      offset: const Offset(-120, 0),
                                      transformHitTests: true, // 关键：让点击区域跟随视觉内容移动
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
                                                if (widget.deviceName.isNotEmpty)
                                                  Text(
                                                    widget.deviceName,
                                                    maxLines: 1,
                                                    overflow: TextOverflow.ellipsis,
                                                    style: const TextStyle(
                                                      fontSize: 14,
                                                      fontWeight: FontWeight.bold,
                                                      color: Colors.black,
                                                    ),
                                                  ),
                                                if (widget.time.isNotEmpty && widget.time != '—')
                                                  Text(
                                                    widget.time,
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
                ],
              ),

              // 显示道路标记（调试用）
              if (_showRouteAndPlace && _displayedRouteData.isNotEmpty)
                MarkerLayer(
                  markers: _displayedRouteData.map((route) {
                    String name = '';
                    List<LatLng> roadPoints = [];
                    try {
                      final attributes = route['attributes'] as List<dynamic>?;
                      if (attributes != null) {
                        for (final attr in attributes) {
                          final attrMap = attr as Map<String, dynamic>;
                          final columnName = attrMap['columnName']?.toString() ?? '';
                          final columnValue = attrMap['columnValue']?.toString() ?? '';
                          if (columnName == 'roadinfo' && columnValue.contains(',')) {
                            roadPoints = CoordTransform.parseRoadinfoToGcj02(columnValue);
                            debugPrint('[道路] 名称: $name, 坐标点数: ${roadPoints.length}');
                          } else if (columnName == 'roadname') {
                            name = _sanitizeString(columnValue);
                          }
                        }
                      }
                    } catch (e) {
                      debugPrint('[道路] 解析失败: $e');
                    }
                    if (roadPoints.isEmpty) return null;
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
                            roadPoints = CoordTransform.parseRoadinfoToGcj02(columnValue);
                          } else if (columnName == 'roadname') {
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
                      final attributes = place['attributes'] as List<dynamic>?;
                      if (attributes != null) {
                        for (final attr in attributes) {
                          final attrMap = attr as Map<String, dynamic>;
                          final columnName = attrMap['columnName']?.toString() ?? '';
                          final columnValue = attrMap['columnValue']?.toString() ?? '';
                          if (columnName == 'gps' && columnValue.contains(',')) {
                            final parts = columnValue.split(',');
                            if (parts.length >= 2) {
                              final wgs84Lat = double.tryParse(parts[0].trim()) ?? 0;
                              final wgs84Lng = double.tryParse(parts[1].trim()) ?? 0;
                              if (wgs84Lat != 0 && wgs84Lng != 0) {
                                final gcj02Coord = CoordTransform.wgs84ToGcj02(wgs84Lat, wgs84Lng);
                                lat = gcj02Coord[0];
                                lng = gcj02Coord[1];
                                debugPrint('[地名坐标转换] WGS-84: ($wgs84Lat, $wgs84Lng) -> GCJ-02: ($lat, $lng)');
                              }
                            }
                          } else if (columnName == 'name') {
                            name = _sanitizeString(columnValue);
                          }
                        }
                      }
                    } catch (e) {
                      debugPrint('[地名] 解析失败: $e, 原始数据: $place');
                    }
                    debugPrint('[地名标记] 名称: $name, 坐标: ($lat, $lng)');
                    if (lat == 0 && lng == 0) return null;
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
            ],
          ),

          // 地图状态提示
          if (_mapStatus.isNotEmpty)
            Center(
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                decoration: BoxDecoration(
                  color: Colors.black54,
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Text(
                  _mapStatus,
                  style: const TextStyle(color: Colors.white, fontSize: 14),
                ),
              ),
            ),

          // Level状态提示
          if (_levelStatus.isNotEmpty)
            Positioned(
              top: 16,
              left: 0,
              right: 0,
              child: Center(
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                  decoration: BoxDecoration(
                    color: Colors.black54,
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Text(
                    _levelStatus,
                    style: const TextStyle(color: Colors.white, fontSize: 13),
                  ),
                ),
              ),
            ),

          // 左下角道路地名切换按钮
          Positioned(
            left: 16,
            bottom: 16,
            child: FloatingActionButton.small(
              heroTag: 'log_map_route_place_fab',
              onPressed: _toggleRouteAndPlace,
              backgroundColor: _showRouteAndPlace ? Colors.blue : Colors.white,
              child: _isLoadingRoutePlace
                  ? const SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(strokeWidth: 2, color: Colors.blue),
                    )
                  : Icon(
                      _showRouteAndPlace ? Icons.visibility : Icons.visibility_off,
                      color: _showRouteAndPlace ? Colors.white : Colors.black54,
                    ),
              tooltip: _showRouteAndPlace ? '隐藏道路和地名' : '显示道路和地名',
            ),
          ),

          // 左上角上报GPS指令按钮
          Positioned(
            left: 16,
            top: 16,
            child: FloatingActionButton.small(
              heroTag: 'log_map_upgps_fab',
              onPressed: _isSendingGps ? null : _sendUpGpsCommand,
              backgroundColor: _isSendingGps ? Colors.grey : Colors.red,
              child: _isSendingGps
                  ? const SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white),
                    )
                  : const Icon(Icons.location_on, color: Colors.white, size: 20),
              tooltip: '上报GPS',
            ),
          ),
        ],
      ),
    );
  }
}

/// 向下小三角箭头绘制器（用于气泡底部指向图标）
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
