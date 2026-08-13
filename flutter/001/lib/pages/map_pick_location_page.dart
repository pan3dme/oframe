import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_map_tile_caching/flutter_map_tile_caching.dart';
import 'package:latlong2/latlong.dart';
import 'package:geolocator/geolocator.dart';
import '../utils/coord_transform.dart';

/// 地图选点页面 - 点击地图选择位置并返回坐标
class MapPickLocationPage extends StatefulWidget {
  /// 初始坐标（可选），如果有则地图初始显示该位置
  final double? initialLat;
  final double? initialLng;

  const MapPickLocationPage({
    super.key,
    this.initialLat,
    this.initialLng,
  });

  @override
  State<MapPickLocationPage> createState() => _MapPickLocationPageState();
}

class _MapPickLocationPageState extends State<MapPickLocationPage> {
  final MapController _mapController = MapController();
  LatLng? _selectedPoint; // 当前选中的坐标（GCJ-02）
  bool _isLocating = false;
  String _mapStatus = '地图加载中...';

  @override
  void initState() {
    super.initState();
    // 如果有初始坐标，使用它
    if (widget.initialLat != null && widget.initialLng != null &&
        widget.initialLat!.abs() > 0.0001 && widget.initialLng!.abs() > 0.0001) {
      // 输入的是WGS-84，转为GCJ-02显示
      final gcj02 = CoordTransform.wgs84ToGcj02(widget.initialLat!, widget.initialLng!);
      _selectedPoint = LatLng(gcj02[0], gcj02[1]);
    }
    // 自动定位
    _getCurrentLocation();
  }

  /// 获取当前位置
  Future<void> _getCurrentLocation() async {
    setState(() => _isLocating = true);

    try {
      bool serviceEnabled = await Geolocator.isLocationServiceEnabled();
      if (!serviceEnabled) {
        if (mounted) {
          setState(() => _isLocating = false);
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('定位服务未开启')),
          );
        }
        return;
      }

      LocationPermission permission = await Geolocator.checkPermission();
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission == LocationPermission.denied) {
          if (mounted) {
            setState(() => _isLocating = false);
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('定位权限被拒绝')),
            );
          }
          return;
        }
      }

      final position = await Geolocator.getCurrentPosition(
        desiredAccuracy: LocationAccuracy.high,
      );

      // WGS-84 转 GCJ-02
      final gcj02Coord = CoordTransform.wgs84ToGcj02(
        position.latitude,
        position.longitude,
      );

      if (mounted) {
        setState(() {
          _selectedPoint = LatLng(gcj02Coord[0], gcj02Coord[1]);
          _isLocating = false;
        });
        _mapController.move(_selectedPoint!, 16.0);
      }
    } catch (e) {
      if (mounted) {
        setState(() => _isLocating = false);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('获取位置失败: $e')),
        );
      }
    }
  }

  /// 处理地图点击
  void _onMapTap(TapPosition tapPosition, LatLng latlng) {
    setState(() {
      _selectedPoint = latlng;
    });
  }

  /// 确认选择，返回WGS-84坐标
  void _confirmSelection() {
    if (_selectedPoint == null) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('请先在地图上点击选择位置')),
      );
      return;
    }
    // GCJ-02 转回 WGS-84 返回给调用方
    final wgs84 = CoordTransform.gcj02ToWgs84(
      _selectedPoint!.latitude,
      _selectedPoint!.longitude,
    );
    Navigator.pop(context, {
      'lat': wgs84[0],
      'lng': wgs84[1],
    });
  }

  @override
  Widget build(BuildContext context) {
    final hasInitialPoint = _selectedPoint != null;

    return Scaffold(
      appBar: AppBar(
        title: const Text('选择位置'),
        backgroundColor: const Color(0xFF16213E),
        foregroundColor: Colors.white,
        elevation: 0,
        actions: [
          TextButton(
            onPressed: _confirmSelection,
            child: const Text(
              '确认',
              style: TextStyle(color: Colors.white, fontSize: 16, fontWeight: FontWeight.bold),
            ),
          ),
        ],
      ),
      body: Stack(
        children: [
          // 地图
          FlutterMap(
            mapController: _mapController,
            options: MapOptions(
              initialCenter: hasInitialPoint
                  ? _selectedPoint!
                  : const LatLng(39.9042, 116.4074), // 默认北京
              initialZoom: hasInitialPoint ? 16.0 : 12.0,
              onTap: _onMapTap,
              interactionOptions: const InteractionOptions(
                flags: InteractiveFlag.pinchZoom |
                    InteractiveFlag.drag |
                    InteractiveFlag.flingAnimation |
                    InteractiveFlag.pinchMove,
              ),
              onMapReady: () {
                setState(() => _mapStatus = '');
              },
            ),
            children: [
              // 高德卫星影像
              TileLayer(
                urlTemplate: 'https://webst0{s}.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}',
                subdomains: ['1', '2', '3', '4'],
                userAgentPackageName: 'com.example.fuck001',
                retinaMode: true,
                tileSize: 256,
                zoomOffset: 0,
                maxNativeZoom: 18,
                minZoom: 3,
                maxZoom: 19,
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
                maxZoom: 19,
                tileProvider: FMTCStore('map_cache').getTileProvider(
                  settings: FMTCTileProviderSettings(),
                ),
              ),
              // 选中标记
              if (_selectedPoint != null)
                MarkerLayer(
                  markers: [
                    Marker(
                      point: _selectedPoint!,
                      width: 40,
                      height: 50,
                      alignment: Alignment.topCenter,
                      child: const Icon(
                        Icons.location_on,
                        color: Colors.red,
                        size: 40,
                      ),
                    ),
                  ],
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

        ],
      ),
    );
  }
}
