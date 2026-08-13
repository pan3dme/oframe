import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_map_tile_caching/flutter_map_tile_caching.dart';
import 'package:latlong2/latlong.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';
import '../utils/coord_transform.dart';
import '../utils/db_helper.dart';

/// 设备日志定位地图页面（卫星地图显示单条记录的GPS坐标）
class DeviceLogMapPage extends StatefulWidget {
  final double latitude;
  final double longitude;
  final String time;
  final String deviceId;
  final String type;

  const DeviceLogMapPage({
    super.key,
    required this.latitude,
    required this.longitude,
    required this.time,
    required this.deviceId,
    required this.type,
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

  @override
  void initState() {
    super.initState();
    _restoreLastFetchDate();
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
      body: jsonEncode({'action': 'getroutetableall'}),
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
      body: jsonEncode({'action': 'getplacetableall'}),
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
              // 坐标圆点（独立层，位置始终固定）
              MarkerLayer(
                markers: [
                  Marker(
                    point: markerPoint,
                    width: 24,
                    height: 24,
                    alignment: Alignment.bottomCenter,
                    child: Container(
                      width: 24,
                      height: 24,
                      decoration: BoxDecoration(
                        color: _typeColor,
                        shape: BoxShape.circle,
                        border: Border.all(color: Colors.white, width: 2),
                        boxShadow: [
                          BoxShadow(
                            color: _typeColor.withValues(alpha: 0.4),
                            blurRadius: 5,
                            spreadRadius: 1,
                          ),
                        ],
                      ),
                      child: const Icon(
                        Icons.arrow_drop_down,
                        color: Colors.white,
                        size: 18,
                      ),
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
                            final parts = columnValue.split(',');
                            if (parts.length >= 2) {
                              for (int i = 0; i < parts.length - 1; i += 2) {
                                final wgs84Lat = double.tryParse(parts[i].trim()) ?? 0;
                                final wgs84Lng = double.tryParse(parts[i + 1].trim()) ?? 0;
                                if (wgs84Lat != 0 && wgs84Lng != 0) {
                                  final gcj02Coord = CoordTransform.wgs84ToGcj02(wgs84Lat, wgs84Lng);
                                  roadPoints.add(LatLng(gcj02Coord[0], gcj02Coord[1]));
                                }
                              }
                              debugPrint('[道路] 名称: $name, 坐标点数: ${roadPoints.length}');
                            }
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
                            final parts = columnValue.split(',');
                            if (parts.length >= 2) {
                              for (int i = 0; i < parts.length - 1; i += 2) {
                                final wgs84Lat = double.tryParse(parts[i].trim()) ?? 0;
                                final wgs84Lng = double.tryParse(parts[i + 1].trim()) ?? 0;
                                if (wgs84Lat != 0 && wgs84Lng != 0) {
                                  final gcj02Coord = CoordTransform.wgs84ToGcj02(wgs84Lat, wgs84Lng);
                                  roadPoints.add(LatLng(gcj02Coord[0], gcj02Coord[1]));
                                }
                              }
                            }
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
        ],
      ),
    );
  }
}
