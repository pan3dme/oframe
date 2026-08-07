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
  bool _showMarkerInfo = false;

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
      final shouldFetch = await _shouldFetchFromNetwork();
      if (shouldFetch) {
        await _loadFromNetwork();
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
    setState(() { _maxAvailableLevel = maxLevel; });
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
  }

  void _toggleRouteAndPlace() async {
    if (_allRouteData.isEmpty && _allPlaceData.isEmpty && !_isLoadingRoutePlace) {
      await _loadRouteAndPlaceData();
      if (_allRouteData.isNotEmpty || _allPlaceData.isNotEmpty) {
        _calculateMaxLevel();
        setState(() {
          _showRouteAndPlace = true;
          _currentLevel = 1;
          _filterDataByLevel();
          _updateLevelStatus();
        });
      }
      return;
    }
    setState(() {
      if (!_showRouteAndPlace) {
        _showRouteAndPlace = true;
        _currentLevel = 1;
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
  }

  void _updateLevelStatus() {
    final text = _currentLevel == 0 ? '隐藏所有' : '显示级别≤$_currentLevel';
    setState(() { _levelStatus = text; });
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted) setState(() { _levelStatus = ''; });
    });
  }

  /// 解析道路坐标点
  List<LatLng> _parseRoutePoints(Map<String, dynamic> route) {
    final points = <LatLng>[];
    try {
      final attributes = route['attributes'] as List<dynamic>?;
      if (attributes != null) {
        for (final attr in attributes) {
          final attrMap = attr as Map<String, dynamic>;
          if (attrMap['columnName'] == 'roadinfo') {
            final val = attrMap['columnValue']?.toString() ?? '';
            if (val.contains(',')) {
              final parts = val.split(',');
              for (int i = 0; i < parts.length - 1; i += 2) {
                final wgsLat = double.tryParse(parts[i].trim()) ?? 0;
                final wgsLng = double.tryParse(parts[i + 1].trim()) ?? 0;
                if (wgsLat != 0 && wgsLng != 0) {
                  final gcj = CoordTransform.wgs84ToGcj02(wgsLat, wgsLng);
                  points.add(LatLng(gcj[0], gcj[1]));
                }
              }
            }
            break;
          }
        }
      }
    } catch (_) {}
    return points;
  }

  /// 解析地名
  ({LatLng point, String name})? _parsePlace(Map<String, dynamic> place) {
    double lat = 0, lng = 0;
    String name = '';
    try {
      final attributes = place['attributes'] as List<dynamic>?;
      if (attributes != null) {
        for (final attr in attributes) {
          final attrMap = attr as Map<String, dynamic>;
          final col = attrMap['columnName']?.toString() ?? '';
          final val = attrMap['columnValue']?.toString() ?? '';
          if (col == 'gps' && val.contains(',')) {
            final parts = val.split(',');
            if (parts.length >= 2) {
              final wgsLat = double.tryParse(parts[0].trim()) ?? 0;
              final wgsLng = double.tryParse(parts[1].trim()) ?? 0;
              if (wgsLat != 0 && wgsLng != 0) {
                final gcj = CoordTransform.wgs84ToGcj02(wgsLat, wgsLng);
                lat = gcj[0];
                lng = gcj[1];
              }
            }
          } else if (col == 'name') {
            name = _sanitizeString(val);
          }
        }
      }
    } catch (_) {}
    if (lat == 0 && lng == 0) return null;
    return (point: LatLng(lat, lng), name: name);
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
              // 高德卫星地图瓦片
              TileLayer(
                urlTemplate:
                    'https://webst0{s}.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}',
                subdomains: ['1', '2', '3', '4'],
                userAgentPackageName: 'com.example.fuck001',
                retinaMode: true,
                tileSize: 256,
                zoomOffset: 0,
                maxNativeZoom: 18,
                minZoom: 3,
                maxZoom: 19,
                errorImage: const NetworkImage(
                  'https://via.placeholder.com/256/CCCCCC/666666?text=Tile+Error',
                ),
                // 使用与地图中心页相同的瓦片缓存
                tileProvider: FMTCStore('map_cache').getTileProvider(
                  settings: FMTCTileProviderSettings(),
                ),
              ),
              // 定位标记 + 气泡提示 + 设备标签
              MarkerLayer(
                markers: [
                  Marker(
                    point: markerPoint,
                    width: 320,
                    height: 130,
                    alignment: Alignment.bottomCenter,
                    child: GestureDetector(
                      onTap: () => setState(() { _showMarkerInfo = !_showMarkerInfo; }),
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          // 气泡提示框
                          if (_showMarkerInfo)
                            Container(
                              margin: const EdgeInsets.only(bottom: 4),
                              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
                              decoration: BoxDecoration(
                                color: Colors.white,
                                borderRadius: BorderRadius.circular(10),
                                boxShadow: [
                                  BoxShadow(
                                    color: Colors.black.withValues(alpha: 0.15),
                                    blurRadius: 8,
                                    offset: const Offset(0, 2),
                                  ),
                                ],
                              ),
                              child: Column(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  Text(
                                    '设备:${widget.deviceId}',
                                    style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: Colors.black87),
                                  ),
                                  const SizedBox(height: 4),
                                  Text(
                                    'GPS:${widget.latitude.toStringAsFixed(4)},${widget.longitude.toStringAsFixed(5)}',
                                    style: const TextStyle(fontSize: 13, color: Colors.black87),
                                  ),
                                  const SizedBox(height: 4),
                                  Text(
                                    '更新:${widget.time}',
                                    style: const TextStyle(fontSize: 13, color: Colors.black54),
                                  ),
                                ],
                              ),
                            ),
                          // 向下三角箭头
                          if (_showMarkerInfo)
                            Container(
                              width: 0,
                              height: 0,
                              decoration: BoxDecoration(
                                border: Border(
                                  left: BorderSide(width: 8, color: Colors.transparent),
                                  right: BorderSide(width: 8, color: Colors.transparent),
                                  top: BorderSide(width: 8, color: Colors.white),
                                ),
                              ),
                            ),
                          // 坐标圆点 + 设备标签
                          Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Container(
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
                              const SizedBox(width: 8),
                              Container(
                                padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                                decoration: BoxDecoration(
                                  color: Colors.white,
                                  borderRadius: BorderRadius.circular(6),
                                  boxShadow: [
                                    BoxShadow(
                                      color: Colors.black.withValues(alpha: 0.1),
                                      blurRadius: 4,
                                      offset: const Offset(0, 1),
                                    ),
                                  ],
                                ),
                                child: Text(
                                  widget.deviceId,
                                  style: const TextStyle(fontSize: 13, color: Colors.black87),
                                ),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
              // 道路线条
              if (_showRouteAndPlace && _displayedRouteData.isNotEmpty)
                PolylineLayer(
                  polylines: _displayedRouteData.map((route) {
                    final points = _parseRoutePoints(route);
                    return Polyline(
                      points: points,
                      strokeWidth: 3,
                      color: Colors.white,
                    );
                  }).where((p) => p.points.isNotEmpty).toList(),
                ),
              // 地名标记
              if (_showRouteAndPlace && _displayedPlaceData.isNotEmpty)
                MarkerLayer(
                  markers: _displayedPlaceData.map((place) {
                    final parsed = _parsePlace(place);
                    if (parsed == null) return null;
                    final safeName = _sanitizeString(parsed.name);
                    return Marker(
                      point: parsed.point,
                      width: 60,
                      height: 40,
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Container(
                            width: 10,
                            height: 10,
                            decoration: BoxDecoration(
                              color: Colors.blue,
                              shape: BoxShape.circle,
                              border: Border.all(color: Colors.white, width: 1.5),
                            ),
                          ),
                          const SizedBox(height: 1),
                          SizedBox(
                            width: 60,
                            child: Text(
                              safeName.length > 5 ? safeName.substring(0, 5) : safeName,
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                              textAlign: TextAlign.center,
                              style: const TextStyle(
                                color: Colors.white,
                                fontSize: 10,
                                fontWeight: FontWeight.bold,
                                shadows: [
                                  Shadow(offset: Offset(1, 1), blurRadius: 2, color: Colors.black),
                                ],
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

