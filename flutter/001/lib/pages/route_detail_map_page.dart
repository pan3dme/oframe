import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_map_tile_caching/flutter_map_tile_caching.dart';
import 'package:latlong2/latlong.dart';
import '../utils/coord_transform.dart';

/// 道路详情地图页面
/// 参数: route - 道路数据 Map，包含 roadname, roadinfo, level, route_id 等字段
class RouteDetailMapPage extends StatefulWidget {
  final Map<String, dynamic> route;
  const RouteDetailMapPage({super.key, required this.route});

  @override
  State<RouteDetailMapPage> createState() => _RouteDetailMapPageState();
}

class _RouteDetailMapPageState extends State<RouteDetailMapPage> {
  final MapController _mapController = MapController();
  List<LatLng> _pathPoints = []; // GCJ-02 坐标（用于显示）
  String _roadName = '';
  String _roadId = '';
  int _level = 1;
  bool _hasError = false;

  @override
  void initState() {
    super.initState();
    _parseRouteData();
  }

  /// 解析道路数据
  void _parseRouteData() {
    _roadName = widget.route['roadname']?.toString() ?? '未命名道路';
    _roadId = widget.route['route_id']?.toString() ?? '';
    final levelVal = widget.route['level'];
    _level = levelVal != null ? (int.tryParse(levelVal.toString()) ?? 1) : 1;

    final roadinfo = widget.route['roadinfo']?.toString() ?? '';
    if (roadinfo.isEmpty) {
      setState(() => _hasError = true);
      return;
    }

    try {
      // roadinfo 格式: "lat1,lng1,lat2,lng2,..." (WGS-84)
      final parts = roadinfo.split(',');
      final points = <LatLng>[];
      for (int i = 0; i < parts.length - 1; i += 2) {
        final wgs84Lat = double.tryParse(parts[i].trim()) ?? 0;
        final wgs84Lng = double.tryParse(parts[i + 1].trim()) ?? 0;
        if (wgs84Lat != 0 && wgs84Lng != 0) {
          // WGS-84 转 GCJ-02
          final gcj02 = CoordTransform.wgs84ToGcj02(wgs84Lat, wgs84Lng);
          points.add(LatLng(gcj02[0], gcj02[1]));
        }
      }
      setState(() {
        _pathPoints = points;
        _hasError = points.isEmpty;
      });

      // 地图就绪后移动到路径中心
      if (points.isNotEmpty) {
        WidgetsBinding.instance.addPostFrameCallback((_) {
          _fitPathBounds();
        });
      }
    } catch (e) {
      debugPrint('[道路详情] 解析 roadinfo 失败: $e');
      setState(() => _hasError = true);
    }
  }

  /// 将地图视野适配到路径范围
  void _fitPathBounds() {
    if (_pathPoints.isEmpty) return;
    if (_pathPoints.length == 1) {
      _mapController.move(_pathPoints.first, 16.0);
      return;
    }
    // 计算边界
    double minLat = _pathPoints.first.latitude;
    double maxLat = _pathPoints.first.latitude;
    double minLng = _pathPoints.first.longitude;
    double maxLng = _pathPoints.first.longitude;
    for (final p in _pathPoints) {
      if (p.latitude < minLat) minLat = p.latitude;
      if (p.latitude > maxLat) maxLat = p.latitude;
      if (p.longitude < minLng) minLng = p.longitude;
      if (p.longitude > maxLng) maxLng = p.longitude;
    }
    final bounds = LatLngBounds(
      LatLng(minLat, minLng),
      LatLng(maxLat, maxLng),
    );
    _mapController.fitCamera(
      CameraFit.bounds(
        bounds: bounds,
        padding: const EdgeInsets.all(60),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(_roadName, style: const TextStyle(fontSize: 16)),
            const SizedBox(height: 2),
            Text(
              'ID: $_roadId  |  Lv.$_level  |  ${_pathPoints.length}个坐标点',
              style: const TextStyle(fontSize: 10, color: Colors.white70),
            ),
          ],
        ),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: _hasError
          ? const Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.error_outline, size: 48, color: Colors.orange),
                  SizedBox(height: 12),
                  Text('该道路暂无路径坐标数据', style: TextStyle(color: Colors.grey)),
                ],
              ),
            )
          : _pathPoints.isEmpty
              ? const Center(child: CircularProgressIndicator())
              : Stack(
                  children: [
                    FlutterMap(
                      mapController: _mapController,
                      options: MapOptions(
                        initialCenter: _pathPoints.isNotEmpty
                            ? _pathPoints[_pathPoints.length ~/ 2]
                            : const LatLng(39.9042, 116.4074),
                        initialZoom: 15.0,
                        interactionOptions: const InteractionOptions(
                          flags: InteractiveFlag.pinchZoom |
                              InteractiveFlag.drag |
                              InteractiveFlag.flingAnimation,
                        ),
                      ),
                      children: [
                        // 瓦片图层（高德卫星影像，使用共享缓存）
                        TileLayer(
                          urlTemplate: 'https://webst0{s}.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}',
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
                        // 路径线
                        if (_pathPoints.length >= 2)
                          PolylineLayer(
                            polylines: [
                              Polyline(
                                points: _pathPoints,
                                strokeWidth: 5.0,
                                color: const Color(0xFF2ECC71),
                              ),
                            ],
                          ),
                      ],
                    ),
                    // 右下角：适配视野按钮
                    Positioned(
                      right: 16,
                      bottom: 24,
                      child: FloatingActionButton(
                        heroTag: 'route_detail_fit_$_roadId',
                        onPressed: _fitPathBounds,
                        backgroundColor: Colors.white,
                        child: const Icon(Icons.fit_screen, color: Color(0xFF2ECC71)),
                      ),
                    ),
                    // 左上角：路径信息面板
                    Positioned(
                      top: 12,
                      left: 12,
                      right: 12,
                      child: _buildInfoPanel(),
                    ),
                  ],
                ),
    );
  }

  /// 路径信息面板
  Widget _buildInfoPanel() {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(12),
        boxShadow: const [BoxShadow(color: Colors.black12, blurRadius: 6)],
      ),
      child: Row(
        children: [
          Container(
            width: 10,
            height: 10,
            decoration: const BoxDecoration(
              color: Color(0xFF2ECC71),
              shape: BoxShape.circle,
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  _roadName,
                  style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 14),
                ),
                const SizedBox(height: 2),
                Text(
                  '共 ${_pathPoints.length} 个坐标点  |  等级 Lv.$_level',
                  style: TextStyle(fontSize: 11, color: Colors.grey[600]),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
