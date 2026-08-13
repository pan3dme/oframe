import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_map_tile_caching/flutter_map_tile_caching.dart';
import 'package:latlong2/latlong.dart';
import 'package:geolocator/geolocator.dart';
import '../utils/coord_transform.dart';

// GPS路径记录页面
// 返回值: Map<String, dynamic>，包含:
//   - 'roadinfo': String  坐标字符串 "lat1,lng1,lat2,lng2,..." (WGS-84)
//   - 'points': List of LatLng  记录的路径点列表 (GCJ-02)
class GpsPathRecordPage extends StatefulWidget {
  const GpsPathRecordPage({super.key});

  @override
  State<GpsPathRecordPage> createState() => _GpsPathRecordPageState();
}

class _GpsPathRecordPageState extends State<GpsPathRecordPage> {
  final MapController _mapController = MapController();

  // 定位与记录状态
  LatLng? _currentPosition;
  bool _isRecording = false;
  bool _isLocating = false;
  StreamSubscription<Position>? _positionSub;

  // 路径点：存储 WGS-84 原始坐标（用于上传）
  final List<LatLng> _recordedWgs84Points = [];
  // 路径点：存储 GCJ-02 转换后坐标（用于地图显示）
  final List<LatLng> _displayedGcj02Points = [];

  // 最小记录距离（米）
  static const double _minRecordDistance = 8.0;
  // 最少记录点数
  static const int _minPointCount = 3;

  // 距离计算
  final Distance _distance = const Distance();

  @override
  void initState() {
    super.initState();
    _initLocation();
  }

  @override
  void dispose() {
    _positionSub?.cancel();
    super.dispose();
  }

  /// 初始化定位
  Future<void> _initLocation() async {
    setState(() => _isLocating = true);

    try {
      LocationPermission permission = await Geolocator.checkPermission();
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission == LocationPermission.denied) {
          _showError('位置权限被拒绝');
          setState(() => _isLocating = false);
          return;
        }
      }
      if (permission == LocationPermission.deniedForever) {
        _showError('位置权限被永久拒绝，请在设置中开启');
        setState(() => _isLocating = false);
        return;
      }

      // 获取初始位置
      final position = await Geolocator.getCurrentPosition(
        desiredAccuracy: LocationAccuracy.high,
      );
      final gcj02 = CoordTransform.wgs84ToGcj02(position.latitude, position.longitude);
      setState(() {
        _currentPosition = LatLng(gcj02[0], gcj02[1]);
        _isLocating = false;
      });
      _mapController.move(_currentPosition!, 17.0);

      // 监听位置变化
      _startPositionStream();
    } catch (e) {
      _showError('获取位置失败: $e');
      setState(() => _isLocating = false);
    }
  }

  /// 开始位置流监听
  void _startPositionStream() {
    _positionSub?.cancel();
    _positionSub = Geolocator.getPositionStream(
      locationSettings: const LocationSettings(
        accuracy: LocationAccuracy.high,
        distanceFilter: 3, // 每移动3米触发一次（比记录阈值小，确保不漏点）
      ),
    ).listen((position) {
      final gcj02 = CoordTransform.wgs84ToGcj02(position.latitude, position.longitude);
      final newGcj02Point = LatLng(gcj02[0], gcj02[1]);
      final newWgs84Point = LatLng(position.latitude, position.longitude);

      setState(() {
        _currentPosition = newGcj02Point;
      });

      // 如果正在记录，检查距离是否达到阈值
      if (_isRecording) {
        _tryRecordPoint(newWgs84Point, newGcj02Point);
      }
    });
  }

  /// 尝试记录一个路径点
  void _tryRecordPoint(LatLng wgs84Point, LatLng gcj02Point) {
    if (_recordedWgs84Points.isEmpty) {
      // 第一个点直接记录
      _addPoint(wgs84Point, gcj02Point);
    } else {
      // 检查与上一个点的距离
      final lastWgs84 = _recordedWgs84Points.last;
      final meters = _distance.as(LengthUnit.Meter, lastWgs84, wgs84Point);
      if (meters >= _minRecordDistance) {
        _addPoint(wgs84Point, gcj02Point);
      }
    }
  }

  /// 添加路径点
  void _addPoint(LatLng wgs84Point, LatLng gcj02Point) {
    setState(() {
      _recordedWgs84Points.add(wgs84Point);
      _displayedGcj02Points.add(gcj02Point);
    });
    debugPrint('[路径记录] 第${_recordedWgs84Points.length}个点: WGS84=(${wgs84Point.latitude}, ${wgs84Point.longitude}), GCJ02=(${gcj02Point.latitude}, ${gcj02Point.longitude})');
  }

  /// 开始记录
  void _startRecording() {
    setState(() {
      _isRecording = true;
    });
    // 立即记录当前位置作为第一个点
    if (_currentPosition != null) {
      // 将当前GCJ-02坐标近似作为第一个点（反向转换回WGS-84记录）
      // 实际上应该从最新的position获取WGS-84坐标
      // 这里简化处理：记录当前GPS位置
      _recordCurrentPosition();
    }
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(
        content: Text('开始记录路径，请移动至少8米以记录下一个点'),
        duration: Duration(seconds: 2),
      ),
    );
  }

  /// 记录当前GPS位置
  Future<void> _recordCurrentPosition() async {
    try {
      final position = await Geolocator.getCurrentPosition(
        desiredAccuracy: LocationAccuracy.high,
      );
      final wgs84 = LatLng(position.latitude, position.longitude);
      final gcj02 = CoordTransform.wgs84ToGcj02(position.latitude, position.longitude);
      _addPoint(wgs84, LatLng(gcj02[0], gcj02[1]));
    } catch (e) {
      debugPrint('[路径记录] 获取当前位置失败: $e');
    }
  }

  /// 停止记录
  void _stopRecording() {
    setState(() {
      _isRecording = false;
    });

    final pointCount = _recordedWgs84Points.length;

    if (pointCount < _minPointCount) {
      // 点数不足，提示并允许重新开始
      _showStopDialog(insufficient: true);
    } else {
      // 点数足够，显示确认对话框
      _showStopDialog(insufficient: false);
    }
  }

  /// 显示停止后的对话框
  void _showStopDialog({required bool insufficient}) {
    final pointCount = _recordedWgs84Points.length;

    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (context) => AlertDialog(
        title: Text(insufficient ? '点数不足' : '路径记录完成'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('已记录 $pointCount 个坐标点'),
            if (insufficient) ...[
              const SizedBox(height: 8),
              Text(
                '至少需要记录 $_minPointCount 个点，请重新开始或继续记录。',
                style: const TextStyle(color: Colors.orange),
              ),
            ] else ...[
              const SizedBox(height: 8),
              const Text('是否确认当前路径？', style: TextStyle(color: Colors.green)),
            ],
          ],
        ),
        actions: [
          TextButton(
            onPressed: () {
              Navigator.pop(context);
              // 继续记录（恢复录制状态）
              setState(() {
                _isRecording = true;
              });
            },
            child: const Text('继续记录'),
          ),
          TextButton(
            onPressed: () {
              Navigator.pop(context);
              // 重新开始：清空数据
              _resetRecording();
            },
            child: const Text('重新开始', style: TextStyle(color: Colors.orange)),
          ),
          if (!insufficient)
            ElevatedButton(
              onPressed: () {
                Navigator.pop(context);
                // 确认，返回坐标数据
                _returnResult();
              },
              style: ElevatedButton.styleFrom(
                backgroundColor: const Color(0xFF2ECC71),
                foregroundColor: Colors.white,
              ),
              child: const Text('确定'),
            ),
        ],
      ),
    );
  }

  /// 重置记录
  void _resetRecording() {
    setState(() {
      _isRecording = false;
      _recordedWgs84Points.clear();
      _displayedGcj02Points.clear();
    });
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('已清空，可重新开始记录'), duration: Duration(seconds: 2)),
    );
  }

  /// 返回结果给上一页
  void _returnResult() {
    // 构建 roadinfo 字符串: "lat1,lng1,lat2,lng2,..." (WGS-84坐标)
    final roadinfo = _recordedWgs84Points
        .map((p) => '${p.latitude.toStringAsFixed(6)},${p.longitude.toStringAsFixed(6)}')
        .join(',');

    Navigator.pop(context, {
      'roadinfo': roadinfo,
      'points': List<LatLng>.from(_displayedGcj02Points),
      'pointCount': _recordedWgs84Points.length,
    });
  }

  void _showError(String msg) {
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(msg), backgroundColor: Colors.red),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('路径记录'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: () => Navigator.pop(context, null),
        ),
      ),
      body: Stack(
        children: [
          // 地图层
          FlutterMap(
            mapController: _mapController,
            options: MapOptions(
              initialCenter: _currentPosition ?? const LatLng(39.9042, 116.4074),
              initialZoom: _currentPosition != null ? 17.0 : 12.0,
              interactionOptions: const InteractionOptions(
                flags: InteractiveFlag.pinchZoom | InteractiveFlag.drag | InteractiveFlag.flingAnimation,
              ),
            ),
            children: [
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
              // 当前位置标记
              if (_currentPosition != null)
                MarkerLayer(
                  markers: [
                    Marker(
                      point: _currentPosition!,
                      width: 24,
                      height: 24,
                      child: Container(
                        decoration: BoxDecoration(
                          color: Colors.blue,
                          shape: BoxShape.circle,
                          border: Border.all(color: Colors.white, width: 2),
                        ),
                        child: const Icon(Icons.navigation, color: Colors.white, size: 12),
                      ),
                    ),
                  ],
                ),
              // 已记录的路径点标记
              if (_displayedGcj02Points.isNotEmpty)
                MarkerLayer(
                  markers: _displayedGcj02Points.asMap().entries.map((entry) {
                    final idx = entry.key;
                    final point = entry.value;
                    return Marker(
                      point: point,
                      width: 20,
                      height: 20,
                      child: Container(
                        decoration: BoxDecoration(
                          color: idx == 0
                              ? Colors.green
                              : (idx == _displayedGcj02Points.length - 1 ? Colors.red : Colors.orange),
                          shape: BoxShape.circle,
                          border: Border.all(color: Colors.white, width: 1.5),
                        ),
                        child: Center(
                          child: Text(
                            '${idx + 1}',
                            style: const TextStyle(color: Colors.white, fontSize: 9, fontWeight: FontWeight.bold),
                          ),
                        ),
                      ),
                    );
                  }).toList(),
                ),
              // 路径线
              if (_displayedGcj02Points.length >= 2)
                PolylineLayer(
                  polylines: [
                    Polyline(
                      points: _displayedGcj02Points,
                      strokeWidth: 4.0,
                      color: Colors.blue,
                    ),
                  ],
                ),
            ],
          ),

          // 顶部信息面板
          Positioned(
            top: 12,
            left: 12,
            right: 12,
            child: _buildInfoPanel(),
          ),

          // 底部控制按钮
          Positioned(
            bottom: 24,
            left: 16,
            right: 16,
            child: _buildControlButtons(),
          ),

          // 定位中提示
          if (_isLocating)
            const Positioned.fill(
              child: ColoredBox(
                color: Colors.black26,
                child: Center(child: CircularProgressIndicator()),
              ),
            ),
        ],
      ),
    );
  }

  /// 顶部信息面板
  Widget _buildInfoPanel() {
    final pointCount = _recordedWgs84Points.length;
    final statusColor = _isRecording ? Colors.red : Colors.grey;
    final statusText = _isRecording ? '正在记录' : '未记录';

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(12),
        boxShadow: const [BoxShadow(color: Colors.black12, blurRadius: 6)],
      ),
      child: Row(
        children: [
          // 状态指示灯
          Container(
            width: 10,
            height: 10,
            decoration: BoxDecoration(color: statusColor, shape: BoxShape.circle),
          ),
          const SizedBox(width: 8),
          Text(statusText, style: TextStyle(color: statusColor, fontWeight: FontWeight.w600)),
          const Spacer(),
          Text(
            '已记录: $pointCount 个点',
            style: TextStyle(fontSize: 13, color: Colors.grey[700]),
          ),
          if (pointCount < _minPointCount) ...[
            const SizedBox(width: 6),
            Text(
              '(需≥$_minPointCount)',
              style: const TextStyle(fontSize: 11, color: Colors.orange),
            ),
          ],
        ],
      ),
    );
  }

  /// 底部控制按钮
  Widget _buildControlButtons() {
    return Row(
      children: [
        // 定位按钮
        Expanded(
          child: OutlinedButton.icon(
            onPressed: () {
              if (_currentPosition != null) {
                _mapController.move(_currentPosition!, 17.0);
              }
            },
            icon: const Icon(Icons.my_location),
            label: const Text('定位'),
            style: OutlinedButton.styleFrom(
              padding: const EdgeInsets.symmetric(vertical: 14),
              side: const BorderSide(color: Colors.blue),
            ),
          ),
        ),
        const SizedBox(width: 12),
        // 开始/停止记录按钮
        Expanded(
          flex: 2,
          child: ElevatedButton.icon(
            onPressed: _isRecording ? _stopRecording : _startRecording,
            icon: Icon(_isRecording ? Icons.stop : Icons.fiber_manual_record),
            label: Text(_isRecording ? '停止记录' : '开始记录'),
            style: ElevatedButton.styleFrom(
              padding: const EdgeInsets.symmetric(vertical: 14),
              backgroundColor: _isRecording ? Colors.red : const Color(0xFF2ECC71),
              foregroundColor: Colors.white,
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
            ),
          ),
        ),
        const SizedBox(width: 12),
        // 返回按钮（有足够点时可点击）
        Expanded(
          child: ElevatedButton(
            onPressed: _recordedWgs84Points.length >= _minPointCount
                ? () => _showStopDialog(insufficient: false)
                : null,
            style: ElevatedButton.styleFrom(
              padding: const EdgeInsets.symmetric(vertical: 14),
              backgroundColor: const Color(0xFF2ECC71),
              foregroundColor: Colors.white,
              disabledBackgroundColor: Colors.grey,
            ),
            child: const Text('完成'),
          ),
        ),
      ],
    );
  }
}


