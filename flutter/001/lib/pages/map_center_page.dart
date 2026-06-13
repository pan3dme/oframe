import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:latlong2/latlong.dart';
import 'package:geolocator/geolocator.dart';
import 'package:http/http.dart' as http;
import '../utils/coord_transform.dart';
import '../utils/db_helper.dart';

class MapCenterPage extends StatefulWidget {
  const MapCenterPage({super.key});

  @override
  State<MapCenterPage> createState() => _MapCenterPageState();
}

class _MapCenterPageState extends State<MapCenterPage> {
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
  
  // 地图缓存相关
  bool _isCacheEnabled = true; // 是否启用缓存（预留）

  @override
  void initState() {
    super.initState();
    // 页面加载后自动定位
    _getCurrentLocation();
    // 不预加载，点击按钮时才加载
  }

  /// 加载道路和地名数据（先从缓存，再从网络）
  Future<void> _loadRouteAndPlaceData() async {
    setState(() {
      _isLoadingRoutePlace = true;
    });

    try {
      // 先尝试从缓存加载
      await _loadFromCache();
      
      // 然后尝试从网络获取最新数据
      await _loadFromNetwork();
      
      debugPrint('[道路地名] 数据加载完成');
    } catch (e) {
      debugPrint('[道路地名] 数据加载失败: $e');
    } finally {
      setState(() {
        _isLoadingRoutePlace = false;
      });
    }
  }

  /// 从缓存加载数据
  Future<void> _loadFromCache() async {
    try {
      final cachedRoutes = await DBHelper().getRoutes(maxLevel: _currentLevel);
      final cachedPlaces = await DBHelper().getPlaces(maxLevel: _currentLevel);
      
      if (cachedRoutes.isNotEmpty || cachedPlaces.isNotEmpty) {
        setState(() {
          _allRouteData = cachedRoutes;
          _allPlaceData = cachedPlaces;
          _filterDataByLevel(); // 根据level过滤
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
        body: jsonEncode({'action': 'getroutetableall'}),
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
        body: jsonEncode({'action': 'getplacetableall'}),
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
                maxZoom: 19,
                // 错误处理
                errorImage: const NetworkImage(
                  'https://via.placeholder.com/256/CCCCCC/666666?text=Tile+Error',
                ),
                // 瓦片缓存配置 - 使用默认的网络提供者，flutter_map会自动缓存
                tileProvider: NetworkTileProvider(
                  headers: {
                    'User-Agent': 'Mozilla/5.0 (iPhone; CPU iPhone OS 16_0 like Mac OS X)',
                  },
                ),
              ),
              // 显示当前位置标记
              if (_currentPosition != null)
                MarkerLayer(
                  markers: [
                    Marker(
                      point: _currentPosition!,
                      child: Container(
                        decoration: BoxDecoration(
                          color: Colors.blue,
                          shape: BoxShape.circle,
                          border: Border.all(color: Colors.white, width: 2),
                        ),
                        child: const Icon(
                          Icons.location_on,
                          color: Colors.white,
                          size: 30,
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
                            // 解析道路坐标点序列："lat1,lng1,lat2,lng2,..."
                            final parts = columnValue.split(',');
                            if (parts.length >= 2) {
                              for (int i = 0; i < parts.length - 1; i += 2) {
                                final wgs84Lat = double.tryParse(parts[i].trim()) ?? 0;
                                final wgs84Lng = double.tryParse(parts[i + 1].trim()) ?? 0;
                                
                                // 将 WGS-84 坐标转换为 GCJ-02（火星坐标）
                                if (wgs84Lat != 0 && wgs84Lng != 0) {
                                  final gcj02Coord = CoordTransform.wgs84ToGcj02(wgs84Lat, wgs84Lng);
                                  roadPoints.add(LatLng(gcj02Coord[0], gcj02Coord[1]));
                                }
                              }
                              debugPrint('[道路] 名称: $name, 坐标点数: ${roadPoints.length}');
                            }
                          } else if (columnName == 'roadname') {
                            name = columnValue;
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
                            name = columnValue;
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
                            name = columnValue;
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
                    
                    return Marker(
                      point: LatLng(lat, lng),
                      width: 60,
                      height: 40,
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          // 蓝点
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
                          // 地名文字（居中对齐）
                          SizedBox(
                            width: 60,
                            child: Text(
                              name.length > 5 ? name.substring(0, 5) : name,
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                              textAlign: TextAlign.center,
                              style: const TextStyle(
                                color: Colors.white,
                                fontSize: 10,
                                fontWeight: FontWeight.bold,
                                shadows: [
                                  Shadow(
                                    offset: Offset(1, 1),
                                    blurRadius: 2,
                                    color: Colors.black,
                                  ),
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
          if (_levelStatus.isNotEmpty && _showRouteAndPlace)
            Positioned(
              top: _mapStatus.isNotEmpty ? 160 : 100,
              left: 20,
              right: 20,
              child: Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: Colors.blue.shade700.withOpacity(0.8),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Text(
                  _levelStatus,
                  style: const TextStyle(color: Colors.white, fontSize: 14, fontWeight: FontWeight.bold),
                  textAlign: TextAlign.center,
                ),
              ),
            ),
          // 左下角道路地名切换按钮
          Positioned(
            left: 16,
            bottom: 16,
            child: FloatingActionButton.small(
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
        ],
      ),
      floatingActionButton: FloatingActionButton(
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