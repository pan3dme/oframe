import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_map_tile_caching/flutter_map_tile_caching.dart';
import 'package:latlong2/latlong.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';
import 'package:geolocator/geolocator.dart';
import '../utils/coord_transform.dart';
import '../utils/db_helper.dart';

/// 设备今日GPS轨迹地图页面
class DeviceTrajectoryPage extends StatefulWidget {
  final String deviceId;
  final String deviceName; // 显示名称

  const DeviceTrajectoryPage({
    super.key,
    required this.deviceId,
    required this.deviceName,
  });

  @override
  State<DeviceTrajectoryPage> createState() => _DeviceTrajectoryPageState();
}

class _DeviceTrajectoryPageState extends State<DeviceTrajectoryPage> {
  final MapController _mapController = MapController();

  // 轨迹数据
  List<_TrajectoryPoint> _points = [];
  bool _isLoading = true;
  String _errorMessage = '';
  String _mapStatus = '';

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
  bool _showTimeLabels = false; // 是否显示时间标签
  int _dayOffset = 0; // 0=今天, 1=昨天, 2=前天 ... 6=6天前
  bool _isLocating = false; // 是否正在定位

  /// 获取当前选中日期
  String _getSelectedDate() {
    final now = DateTime.now();
    final date = now.subtract(Duration(days: _dayOffset));
    return '${date.year}-${date.month.toString().padLeft(2, '0')}-${date.day.toString().padLeft(2, '0')}';
  }

  /// 获取日期标签
  String _getDayLabel() {
    if (_dayOffset == 0) return '今天';
    if (_dayOffset == 1) return '昨天';
    if (_dayOffset == 2) return '前天';
    final now = DateTime.now();
    final date = now.subtract(Duration(days: _dayOffset));
    return '${date.month}/${date.day}';
  }

  /// 日期选项列表
  static const List<int> _dayOffsets = [0, 1, 2, 3, 4, 5, 6];

  @override
  void initState() {
    super.initState();
    _restoreLastFetchDate();
    _loadTrajectory();
  }

  Future<void> _restoreLastFetchDate() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final saved = prefs.getString('traj_last_route_place_fetch_date');
      if (saved != null) setState(() { _lastRoutePlaceFetchDate = saved; });
    } catch (_) {}
  }

  /// 加载GPS轨迹
  Future<void> _loadTrajectory() async {
    setState(() {
      _isLoading = true;
      _errorMessage = '';
    });

    final curdate = _getSelectedDate();

    try {
      final resp = await http.post(
        Uri.parse('https://gpsmoveinfo.cn/fc/device'),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getDeviceLogGpsbyId',
          'info': {
            'deviceId': widget.deviceId,
            'limit': 99,
            'curdate': curdate,
          },
        }),
      );

      debugPrint('[轨迹] 响应状态: ${resp.statusCode}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>? ?? [];
          final parsedPoints = _parseTrajectoryRows(rawRows);

          // 网络成功，保存到缓存
          _saveToCache(curdate, rawRows);

          setState(() {
            _points = parsedPoints;
            _isLoading = false;
          });

          debugPrint('[轨迹] 网络加载，解析到 ${parsedPoints.length} 个有效GPS点');

          if (parsedPoints.isNotEmpty) {
            WidgetsBinding.instance.addPostFrameCallback((_) {
              _fitMapToBounds();
            });
          }
        } else {
          // 接口返回失败，尝试缓存
          await _loadFromCacheFallback(curdate);
        }
      } else {
        // HTTP状态码异常，尝试缓存
        await _loadFromCacheFallback(curdate);
      }
    } catch (e) {
      debugPrint('[轨迹] 网络加载失败: $e，尝试从缓存读取');
      // 断网或请求异常，从缓存加载
      await _loadFromCacheFallback(curdate);
    }
  }

  /// 解析API返回的原始数据为轨迹点
  List<_TrajectoryPoint> _parseTrajectoryRows(List<dynamic> rawRows) {
    final parsedPoints = <_TrajectoryPoint>[];

    for (final row in rawRows) {
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

      // 解析GPS坐标：从lorastr字段 "type|deviceMarker|lat,lng|value"
      final lorastr = result['lorastr']?.toString() ?? '';
      final timeStr = result['time']?.toString() ?? '';
      final parts = lorastr.split('|');
      if (parts.length >= 3) {
        final gpsStr = parts[2];
        final gpsParts = gpsStr.split(',');
        if (gpsParts.length >= 2) {
          final lat = double.tryParse(gpsParts[0].trim());
          final lng = double.tryParse(gpsParts[1].trim());
          if (lat != null && lng != null && (lat != 0 || lng != 0)) {
            parsedPoints.add(_TrajectoryPoint(
              lat: lat,
              lng: lng,
              time: timeStr,
              lorastr: lorastr,
            ));
          }
        }
      }
    }

    // 按时间升序排序（最早在前，连线方向正确）
    parsedPoints.sort((a, b) => a.time.compareTo(b.time));
    return parsedPoints;
  }

  /// 网络成功后保存数据到缓存
  Future<void> _saveToCache(String curdate, List<dynamic> rawRows) async {
    try {
      final cacheRows = rawRows.map((row) {
        final rowMap = row as Map<String, dynamic>;
        final result = <String, dynamic>{};
        final pkList = rowMap['primaryKey'] as List<dynamic>? ?? [];
        for (final pk in pkList) {
          final pkMap = pk as Map<String, dynamic>;
          result[pkMap['name'] as String] = pkMap['value'];
        }
        final attrList = rowMap['attributes'] as List<dynamic>? ?? [];
        for (final attr in attrList) {
          final attrMap = attr as Map<String, dynamic>;
          result[attrMap['columnName'] as String] = attrMap['columnValue'];
        }
        return result;
      }).toList();
      await DBHelper().saveTrajectoryCache(widget.deviceId, curdate, cacheRows);
    } catch (e) {
      debugPrint('[轨迹] 保存缓存失败: $e');
    }
  }

  /// 从缓存加载轨迹数据（断网回退）
  Future<void> _loadFromCacheFallback(String curdate) async {
    try {
      final cachedLogs = await DBHelper().getLogsByDeviceIdAndDate(widget.deviceId, curdate);
      if (cachedLogs.isNotEmpty) {
        // 将缓存数据转换为与API返回相同的格式供解析
        final rawRows = cachedLogs.map((log) {
          return {
            'primaryKey': <dynamic>[],
            'attributes': <dynamic>[
              {'columnName': 'lorastr', 'columnValue': log['lorastr']},
              {'columnName': 'time', 'columnValue': log['time']},
            ],
          };
        }).toList();

        final parsedPoints = _parseTrajectoryRows(rawRows);
        setState(() {
          _points = parsedPoints;
          _isLoading = false;
          _errorMessage = '';
        });

        debugPrint('[轨迹] 从缓存加载 ${parsedPoints.length} 个GPS点');

        if (parsedPoints.isNotEmpty) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            _fitMapToBounds();
          });
        }
      } else {
        setState(() {
          _errorMessage = '网络不可用，且无缓存轨迹数据';
          _isLoading = false;
        });
      }
    } catch (e) {
      debugPrint('[轨迹] 缓存加载失败: $e');
      setState(() {
        _errorMessage = '加载失败: $e';
        _isLoading = false;
      });
    }
  }

  /// 自动适配地图显示范围
  void _fitMapToBounds() {
    if (_points.isEmpty) return;
    final gcj02Points = _points
        .map((p) => CoordTransform.wgs84ToGcj02(p.lat, p.lng))
        .toList();

    if (gcj02Points.length == 1) {
      _mapController.move(
        LatLng(gcj02Points[0][0], gcj02Points[0][1]),
        16.0,
      );
      return;
    }

    double minLat = gcj02Points[0][0];
    double maxLat = gcj02Points[0][0];
    double minLng = gcj02Points[0][1];
    double maxLng = gcj02Points[0][1];

    for (final p in gcj02Points) {
      if (p[0] < minLat) minLat = p[0];
      if (p[0] > maxLat) maxLat = p[0];
      if (p[1] < minLng) minLng = p[1];
      if (p[1] > maxLng) maxLng = p[1];
    }

    final bounds = LatLngBounds(
      LatLng(minLat, minLng),
      LatLng(maxLat, maxLng),
    );
    _mapController.fitCamera(
      CameraFit.bounds(
        bounds: bounds,
        padding: const EdgeInsets.all(120), // 宽松视野，留出更多空间
      ),
    );
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

  /// 格式化时间显示（只取时分秒）
  String _formatTimeShort(String timeStr) {
    try {
      // 支持格式: "2026/08/13 14:30:22" 或 "2026-08-13 14:30:22"
      final parts = timeStr.split(' ');
      if (parts.length >= 2) return parts[1];
      return timeStr;
    } catch (_) {
      return timeStr;
    }
  }

  // ─── 道路地名相关逻辑（与 device_log_map_page 相同） ───────────────────

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
        await prefs.setString('traj_last_route_place_fetch_date', today);
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
  }

  void _updateLevelStatus() {
    final text = _currentLevel == 0 ? '隐藏所有' : '显示级别≤$_currentLevel';
    setState(() { _levelStatus = text; });
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted) setState(() { _levelStatus = ''; });
    });
  }

  // ─── 定位功能 ─────────────────────────────────────────────────────────

  /// 获取当前位置并移动地图
  Future<void> _getCurrentLocation() async {
    setState(() { _isLocating = true; });
    try {
      LocationPermission permission = await Geolocator.checkPermission();
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission == LocationPermission.denied) {
          if (mounted) ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('位置权限被拒绝')));
          setState(() { _isLocating = false; });
          return;
        }
      }
      if (permission == LocationPermission.deniedForever) {
        if (mounted) ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('位置权限被永久拒绝，请在设置中开启')));
        setState(() { _isLocating = false; });
        return;
      }
      final position = await Geolocator.getCurrentPosition(desiredAccuracy: LocationAccuracy.high);
      final gcj02Coord = CoordTransform.wgs84ToGcj02(position.latitude, position.longitude);
      final target = LatLng(gcj02Coord[0], gcj02Coord[1]);
      setState(() { _isLocating = false; });
      _mapController.move(target, 16.0);
    } catch (e) {
      debugPrint('获取位置失败: $e');
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('获取位置失败: $e')));
      setState(() { _isLocating = false; });
    }
  }

  // ─── 构建UI ─────────────────────────────────────────────────────────────

  /// 构建日期切换芯片
  Widget _buildDayChip(String label, int offset) {
    final isSelected = _dayOffset == offset;
    return InkWell(
      onTap: _isLoading ? null : () {
        if (_dayOffset == offset) return;
        setState(() { _dayOffset = offset; });
        _loadTrajectory();
      },
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
        decoration: BoxDecoration(
          color: isSelected ? const Color(0xFF2196F3) : Colors.transparent,
          borderRadius: BorderRadius.circular(6),
        ),
        child: Text(
          label,
          style: TextStyle(
            color: isSelected ? Colors.white : Colors.white60,
            fontSize: 12,
            fontWeight: isSelected ? FontWeight.w600 : FontWeight.normal,
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF1A1A2E),
      appBar: AppBar(
        title: Text('${_getDayLabel()}轨迹 (${widget.deviceName})'),
        backgroundColor: const Color(0xFF16213E),
        foregroundColor: Colors.white,
        elevation: 0,
        actions: [
          // 刷新按钮
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: _loadTrajectory,
            tooltip: '刷新轨迹',
          ),
        ],
      ),
      body: Stack(
        children: [
          // 卫星地图（始终渲染，即使正在加载）
          _buildMap(),

          // 加载中遮罩
          if (_isLoading)
            Container(
              color: Colors.black38,
              child: const Center(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    CircularProgressIndicator(color: Colors.white),
                    SizedBox(height: 16),
                    Text('正在加载轨迹数据...', style: TextStyle(color: Colors.white, fontSize: 15)),
                  ],
                ),
              ),
            ),

          // 错误提示
          if (_errorMessage.isNotEmpty && !_isLoading)
            Center(
              child: Container(
                margin: const EdgeInsets.all(32),
                padding: const EdgeInsets.all(20),
                decoration: BoxDecoration(
                  color: Colors.black54,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    const Icon(Icons.error_outline, color: Colors.redAccent, size: 40),
                    const SizedBox(height: 12),
                    Text(
                      _errorMessage,
                      style: const TextStyle(color: Colors.white, fontSize: 14),
                      textAlign: TextAlign.center,
                    ),
                    const SizedBox(height: 16),
                    ElevatedButton(
                      onPressed: _loadTrajectory,
                      style: ElevatedButton.styleFrom(
                        backgroundColor: const Color(0xFF1976D2),
                        foregroundColor: Colors.white,
                      ),
                      child: const Text('重试'),
                    ),
                  ],
                ),
              ),
            ),

          // 轨迹信息条（顶部，含日期切换）
          if (!_isLoading && _errorMessage.isEmpty)
            Positioned(
              top: 12,
              left: 16,
              right: 16,
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
                decoration: BoxDecoration(
                  color: Colors.black54,
                  borderRadius: BorderRadius.circular(10),
                ),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // 日期选择行（可横向滚动）
                    SizedBox(
                      height: 28,
                      child: ListView.builder(
                        scrollDirection: Axis.horizontal,
                        itemCount: _dayOffsets.length,
                        itemBuilder: (context, i) {
                          final offset = _dayOffsets[i];
                          final now = DateTime.now();
                          final date = now.subtract(Duration(days: offset));
                          final label = offset == 0 ? '今天' : offset == 1 ? '昨天' : offset == 2 ? '前天' : '${date.month}/${date.day}';
                          return _buildDayChip(label, offset);
                        },
                      ),
                    ),
                    const SizedBox(height: 4),
                    Container(height: 0.5, color: Colors.white12),
                    const SizedBox(height: 4),
                    // 信息行
                    SizedBox(
                      height: 22,
                      child: Row(
                        children: [
                          const Icon(Icons.route, color: Colors.white, size: 14),
                          const SizedBox(width: 4),
                          Text(
                            '${_points.length}点',
                            style: const TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.w500),
                          ),
                          const Spacer(),
                          if (_points.isNotEmpty) ...[
                            const Icon(Icons.access_time, color: Colors.white70, size: 12),
                            const SizedBox(width: 3),
                            Text(
                              '${_formatTimeShort(_points.first.time)} ~ ${_formatTimeShort(_points.last.time)}',
                              style: const TextStyle(color: Colors.white70, fontSize: 11),
                            ),
                          ],
                        ],
                      ),
                    ),
                  ],
                ),
              ),
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
              top: 60,
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
              heroTag: 'traj_map_route_place_fab',
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

          // 右下角时间标签切换按钮
          Positioned(
            right: 16,
            bottom: 16,
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                // 定位按钮
                FloatingActionButton.small(
                  heroTag: 'traj_map_locate_fab',
                  onPressed: _isLocating ? null : _getCurrentLocation,
                  backgroundColor: Colors.white,
                  child: _isLocating
                      ? const SizedBox(
                          width: 20,
                          height: 20,
                          child: CircularProgressIndicator(strokeWidth: 2, color: Colors.blue),
                        )
                      : const Icon(Icons.my_location, color: Colors.black54),
                  tooltip: '定位到当前位置',
                ),
                const SizedBox(height: 10),
                // 时间标签切换
                FloatingActionButton.small(
                  heroTag: 'traj_map_time_fab',
                  onPressed: () {
                    setState(() { _showTimeLabels = !_showTimeLabels; });
                  },
                  backgroundColor: _showTimeLabels ? Colors.orange : Colors.white,
                  child: Icon(
                    Icons.access_time,
                    color: _showTimeLabels ? Colors.white : Colors.black54,
                  ),
                  tooltip: _showTimeLabels ? '隐藏时间' : '显示时间',
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildMap() {
    // 转换所有点为 GCJ-02
    final allGcj02 = _points
        .map((p) => CoordTransform.wgs84ToGcj02(p.lat, p.lng))
        .toList();

    // 距离过滤：相邻点间距 < 5米时跳过，避免标记重叠
    const double minDistanceMeters = 5.0;
    final distance = Distance();
    final filteredIndices = <int>[];
    LatLng? lastKeptPoint;
    for (int i = 0; i < allGcj02.length; i++) {
      final current = LatLng(allGcj02[i][0], allGcj02[i][1]);
      if (i == 0 || i == allGcj02.length - 1) {
        // 起点和终点始终保留
        filteredIndices.add(i);
        lastKeptPoint = current;
      } else if (lastKeptPoint != null) {
        final meters = distance.as(LengthUnit.Meter, current, lastKeptPoint);
        if (meters >= minDistanceMeters) {
          filteredIndices.add(i);
          lastKeptPoint = current;
        }
      }
    }
    debugPrint('[轨迹过滤] 原始${allGcj02.length}点 -> 过滤后${filteredIndices.length}点 (阈值=${minDistanceMeters}m)');

    // 构建标记列表（仅使用过滤后的索引）
    final markers = <Marker>[];
    for (final idx in filteredIndices) {
      final gcj02 = allGcj02[idx];
      final point = _points[idx];
      final isFirst = idx == 0;
      final isLast = idx == _points.length - 1;

      // 起点绿色，终点红色，中间蓝色
      Color markerColor;
      if (isFirst) {
        markerColor = const Color(0xFF4CAF50); // 起点绿色
      } else if (isLast) {
        markerColor = const Color(0xFFF44336); // 终点红色
      } else {
        markerColor = const Color(0xFF2196F3); // 中间蓝色
      }

      final timeLabel = _formatTimeShort(point.time);

      markers.add(
        Marker(
          point: LatLng(gcj02[0], gcj02[1]),
          width: 24,
          height: 24,
          alignment: Alignment.center,
          child: Stack(
            clipBehavior: Clip.none,
            children: [
              // 圆点（固定在坐标中心，始终对准GPS位置）
              Container(
                width: 24,
                height: 24,
                decoration: BoxDecoration(
                  color: markerColor,
                  shape: BoxShape.circle,
                  border: Border.all(color: Colors.white, width: 2),
                  boxShadow: [
                    BoxShadow(
                      color: markerColor.withValues(alpha: 0.4),
                      blurRadius: 4,
                      spreadRadius: 1,
                    ),
                  ],
                ),
                child: isFirst
                    ? const Icon(Icons.play_arrow, color: Colors.white, size: 12)
                    : isLast
                        ? const Icon(Icons.flag, color: Colors.white, size: 12)
                        : null,
              ),
              // 时间标签（向右延伸，不影响圆点位置）
              if (_showTimeLabels)
                Positioned(
                  left: 30,
                  top: 4,
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 2),
                    decoration: BoxDecoration(
                      color: Colors.white,
                      borderRadius: BorderRadius.circular(4),
                      boxShadow: [
                        BoxShadow(
                          color: Colors.black.withOpacity(0.15),
                          blurRadius: 2,
                          offset: const Offset(0, 1),
                        ),
                      ],
                    ),
                    child: Text(
                      timeLabel,
                      style: TextStyle(
                        fontSize: 10,
                        color: markerColor,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ),
                ),
            ],
          ),
        ),
      );
    }

    // 轨迹连线（使用全部点，保持线条平滑）
    final polylinePoints = allGcj02
        .map((c) => LatLng(c[0], c[1]))
        .toList();

    return FlutterMap(
      mapController: _mapController,
      options: MapOptions(
        initialCenter: const LatLng(35.0, 105.0), // 默认：中国中心
        initialZoom: 4.5, // 默认：全国视野
        interactionOptions: const InteractionOptions(
          flags: InteractiveFlag.pinchZoom |
              InteractiveFlag.drag |
              InteractiveFlag.flingAnimation |
              InteractiveFlag.pinchMove,
        ),
        onMapReady: () {
          setState(() { _mapStatus = ''; });
          // 数据已加载时自动适配范围
          if (_points.isNotEmpty && !_isLoading) {
            WidgetsBinding.instance.addPostFrameCallback((_) {
              _fitMapToBounds();
            });
          } else if (_isLoading) {
            // 数据还在加载中，延迟后再检查
            Future.delayed(const Duration(milliseconds: 1500), () {
              if (mounted && _points.isNotEmpty && !_isLoading) {
                _fitMapToBounds();
              }
            });
          }
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

        // 轨迹连线
        if (polylinePoints.length >= 2)
          PolylineLayer(
            polylines: [
              Polyline(
                points: polylinePoints,
                strokeWidth: 3.5,
                color: const Color(0xFF00E5FF),
              ),
            ],
          ),

        // GPS轨迹点标记
        if (markers.isNotEmpty)
          MarkerLayer(markers: markers),

        // 道路线条
        if (_showRouteAndPlace && _displayedRouteData.isNotEmpty)
          PolylineLayer(
            polylines: _displayedRouteData.map((route) {
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
                      }
                    }
                  }
                }
              } catch (_) {}
              return Polyline(
                points: roadPoints,
                strokeWidth: 3,
                color: Colors.white,
              );
            }).toList(),
          ),

        // 地名标记
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
                        }
                      }
                    } else if (columnName == 'name') {
                      name = _sanitizeString(columnValue);
                    }
                  }
                }
              } catch (_) {}
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
    );
  }
}

/// 轨迹点数据模型
class _TrajectoryPoint {
  final double lat;
  final double lng;
  final String time;
  final String lorastr;

  const _TrajectoryPoint({
    required this.lat,
    required this.lng,
    required this.time,
    required this.lorastr,
  });
}
