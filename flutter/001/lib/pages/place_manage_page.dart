import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'map_pick_location_page.dart';

/// FC 地址常量
const String _placeFcUrl = 'https://gpsmoveinfo.cn/fc/route_place';

class PlaceManagePage extends StatefulWidget {
  const PlaceManagePage({super.key});

  @override
  State<PlaceManagePage> createState() => _PlaceManagePageState();
}

class _PlaceManagePageState extends State<PlaceManagePage> {
  List<Map<String, dynamic>> _places = [];
  bool _isLoading = true;
  String _errorMessage = '';

  @override
  void initState() {
    super.initState();
    _loadPlaces();
  }

  /// 加载地名列表
  Future<void> _loadPlaces() async {
    setState(() {
      _isLoading = true;
      _errorMessage = '';
    });

    try {
      final resp = await http.post(
        Uri.parse(_placeFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getplacetableall'}),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final data = json['data'] as List<dynamic>;
          final places = data.map((e) => _parsePlace(e as Map<String, dynamic>)).toList();
          setState(() {
            _places = places;
            _isLoading = false;
          });
          debugPrint('[地名管理] 加载成功: ${places.length}条');
        } else {
          setState(() {
            _errorMessage = json['msg'] ?? '加载失败';
            _isLoading = false;
          });
        }
      } else {
        setState(() {
          _errorMessage = 'HTTP错误: ${resp.statusCode}';
          _isLoading = false;
        });
      }
    } catch (e) {
      setState(() {
        _errorMessage = '加载失败: $e';
        _isLoading = false;
      });
    }
  }

  /// 解析地名数据
  Map<String, dynamic> _parsePlace(Map<String, dynamic> raw) {
    final result = <String, dynamic>{};

    // 解析主键
    final pkList = raw['primaryKey'] as List<dynamic>? ?? [];
    for (final pk in pkList) {
      final pkMap = pk as Map<String, dynamic>;
      result[pkMap['name'] as String] = pkMap['value'];
    }

    // 解析属性
    final attrList = raw['attributes'] as List<dynamic>? ?? [];
    for (final attr in attrList) {
      final attrMap = attr as Map<String, dynamic>;
      result[attrMap['columnName'] as String] = attrMap['columnValue'];
    }

    return result;
  }

  /// 获取地名名称
  String _getPlaceName(Map<String, dynamic> place) {
    return place['name']?.toString() ?? '未命名';
  }

  /// 获取地名ID
  String _getPlaceId(Map<String, dynamic> place) {
    return place['placeid']?.toString() ?? '';
  }

  /// 获取地名等级
  int _getPlaceLevel(Map<String, dynamic> place) {
    final level = place['level'];
    if (level == null) return 1;
    return int.tryParse(level.toString()) ?? 1;
  }

  /// 获取坐标文本
  String _getGpsText(Map<String, dynamic> place) {
    final gps = place['gps']?.toString() ?? '';
    return gps.isEmpty ? '—' : gps;
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('地名管理'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : _errorMessage.isNotEmpty
              ? Center(
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      const Icon(Icons.error_outline, size: 48, color: Colors.red),
                      const SizedBox(height: 16),
                      Text(_errorMessage, style: const TextStyle(color: Colors.red)),
                      const SizedBox(height: 16),
                      ElevatedButton.icon(
                        onPressed: _loadPlaces,
                        icon: const Icon(Icons.refresh),
                        label: const Text('刷新'),
                      ),
                    ],
                  ),
                )
              : Column(
                  children: [
                    // 顶部信息栏
                    Padding(
                      padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
                      child: Row(
                        children: [
                          Text(
                            '共 ${_places.length} 条地名',
                            style: TextStyle(fontSize: 14, color: Colors.grey[600]),
                          ),
                          const Spacer(),
                          ElevatedButton.icon(
                            onPressed: () => _showAddPlaceDialog(),
                            icon: const Icon(Icons.add, size: 18),
                            label: const Text('新增地名'),
                            style: ElevatedButton.styleFrom(
                              backgroundColor: const Color(0xFF2ECC71),
                              foregroundColor: Colors.white,
                              shape: RoundedRectangleBorder(
                                borderRadius: BorderRadius.circular(20),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                    // 地名列表
                    Expanded(
                      child: _places.isEmpty
                          ? const Center(
                              child: Column(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  Icon(Icons.place, size: 64, color: Colors.grey),
                                  SizedBox(height: 16),
                                  Text('暂无地名数据', style: TextStyle(color: Colors.grey)),
                                ],
                              ),
                            )
                          : RefreshIndicator(
                              onRefresh: _loadPlaces,
                              child: ListView.builder(
                                padding: const EdgeInsets.fromLTRB(12, 0, 12, 12),
                                itemCount: _places.length,
                                itemBuilder: (context, index) {
                                  final place = _places[index];
                                  return _buildPlaceCard(place, index);
                                },
                              ),
                            ),
                    ),
                  ],
                ),
    );
  }

  /// 构建地名卡片
  Widget _buildPlaceCard(Map<String, dynamic> place, int index) {
    final name = _getPlaceName(place);
    final id = _getPlaceId(place);
    final level = _getPlaceLevel(place);
    final gpsText = _getGpsText(place);

    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      elevation: 1,
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
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
                // 名称和ID
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        name,
                        style: const TextStyle(
                          fontSize: 15,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                      const SizedBox(height: 2),
                      Text(
                        'ID: $id',
                        style: const TextStyle(
                          fontSize: 12,
                          color: Colors.blue,
                        ),
                      ),
                      const SizedBox(height: 2),
                      Text(
                        '坐标: $gpsText',
                        style: TextStyle(
                          fontSize: 11,
                          color: Colors.grey[500],
                        ),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: 8),
                // 等级标签
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                  decoration: BoxDecoration(
                    color: const Color(0xFFE8F8F0),
                    borderRadius: BorderRadius.circular(4),
                    border: Border.all(color: const Color(0xFF2ECC71), width: 1),
                  ),
                  child: Text(
                    'Lv.$level',
                    style: const TextStyle(
                      fontSize: 11,
                      fontWeight: FontWeight.w600,
                      color: Color(0xFF2ECC71),
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                // 按钮列
                Column(
                  children: [
                    OutlinedButton(
                      onPressed: () => _showEditPlaceDialog(place),
                      style: OutlinedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
                        minimumSize: Size.zero,
                        tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                        side: const BorderSide(color: Colors.blue),
                      ),
                      child: const Text(
                        '编辑',
                        style: TextStyle(fontSize: 12, color: Colors.blue),
                      ),
                    ),
                    const SizedBox(height: 4),
                    OutlinedButton(
                      onPressed: () => _confirmDeletePlace(place),
                      style: OutlinedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
                        minimumSize: Size.zero,
                        tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                        side: const BorderSide(color: Colors.red),
                      ),
                      child: const Text(
                        '删除',
                        style: TextStyle(fontSize: 12, color: Colors.red),
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  /// 显示新增地名对话框
  void _showAddPlaceDialog() {
    final nameController = TextEditingController();
    final levelController = TextEditingController(text: '1');
    final gpsController = TextEditingController();
    double? _pickedLat;
    double? _pickedLng;

    showDialog(
      context: context,
      builder: (dialogContext) => StatefulBuilder(
        builder: (context, setDialogState) => AlertDialog(
          title: const Text('新增地名'),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                TextField(
                  controller: nameController,
                  decoration: const InputDecoration(
                    labelText: '地名名称',
                    border: OutlineInputBorder(),
                    prefixIcon: Icon(Icons.place),
                  ),
                ),
                const SizedBox(height: 12),
                // 坐标字段：点击打开地图选点
                InkWell(
                  onTap: () async {
                    final result = await Navigator.push(
                      context,
                      MaterialPageRoute(
                        builder: (_) => MapPickLocationPage(
                          initialLat: _pickedLat,
                          initialLng: _pickedLng,
                        ),
                      ),
                    );
                    if (result != null && result is Map<String, dynamic>) {
                      _pickedLat = result['lat'] as double;
                      _pickedLng = result['lng'] as double;
                      final coordStr = '${_pickedLat!.toStringAsFixed(6)},${_pickedLng!.toStringAsFixed(6)}';
                      setDialogState(() {
                        gpsController.text = coordStr;
                      });
                    }
                  },
                  child: AbsorbPointer(
                    child: TextField(
                      controller: gpsController,
                      readOnly: true,
                      decoration: InputDecoration(
                        labelText: '坐标（点击地图选择）',
                        border: const OutlineInputBorder(),
                        prefixIcon: const Icon(Icons.location_on),
                        suffixIcon: const Icon(Icons.map, color: Colors.blue),
                        hintText: '点击打开地图选点',
                        hintStyle: TextStyle(color: Colors.grey[400], fontSize: 13),
                      ),
                    ),
                  ),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: levelController,
                  decoration: const InputDecoration(
                    labelText: '等级',
                    border: OutlineInputBorder(),
                    prefixIcon: Icon(Icons.flag),
                  ),
                  keyboardType: TextInputType.number,
                ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(dialogContext),
              child: const Text('取消'),
            ),
            ElevatedButton(
              onPressed: () {
                final name = nameController.text.trim();
                final gps = gpsController.text.trim();
                final level = levelController.text.trim();
                if (name.isEmpty) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('请输入地名名称')),
                  );
                  return;
                }
                if (gps.isEmpty) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('请选择坐标')),
                  );
                  return;
                }
                Navigator.pop(dialogContext);
                _addPlace(name, gps, level);
              },
              child: const Text('确定'),
            ),
          ],
        ),
      ),
    );
  }

  /// 新增地名
  Future<void> _addPlace(String name, String gps, String level) async {
    final newId = 'id${DateTime.now().millisecondsSinceEpoch ~/ 1000}';
    debugPrint('[地名管理] 新增地名: name=$name, gps=$gps, level=$level, id=$newId');

    try {
      final resp = await http.post(
        Uri.parse(_placeFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'addPlace',
          'info': {
            'placeid': newId,
            'deviceId': newId,
            'name': name,
            'gps': gps,
            'level': level,
          },
        }),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          debugPrint('[地名管理] 新增成功');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('新增成功'), backgroundColor: Colors.green),
            );
            _loadPlaces();
          }
        } else {
          debugPrint('[地名管理] 新增失败: ${json['msg']}');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(content: Text('新增失败: ${json['msg']}'), backgroundColor: Colors.red),
            );
          }
        }
      }
    } catch (e) {
      debugPrint('[地名管理] 新增失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('新增失败: $e'), backgroundColor: Colors.red),
        );
      }
    }
  }

  /// 显示编辑地名对话框
  void _showEditPlaceDialog(Map<String, dynamic> place) {
    final nameController = TextEditingController(text: _getPlaceName(place));
    final levelController = TextEditingController(text: _getPlaceLevel(place).toString());
    final gpsController = TextEditingController(text: _getGpsText(place));

    // 解析当前坐标用于地图初始位置
    double? _pickedLat;
    double? _pickedLng;
    final gpsText = _getGpsText(place);
    if (gpsText != '—') {
      final parts = gpsText.split(',');
      if (parts.length >= 2) {
        _pickedLat = double.tryParse(parts[0].trim());
        _pickedLng = double.tryParse(parts[1].trim());
      }
    }

    showDialog(
      context: context,
      builder: (dialogContext) => StatefulBuilder(
        builder: (context, setDialogState) => AlertDialog(
          title: const Text('编辑地名'),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                TextField(
                  controller: nameController,
                  decoration: const InputDecoration(
                    labelText: '地名名称',
                    border: OutlineInputBorder(),
                    prefixIcon: Icon(Icons.place),
                  ),
                ),
                const SizedBox(height: 12),
                // 坐标字段：点击打开地图选点
                InkWell(
                  onTap: () async {
                    final result = await Navigator.push(
                      context,
                      MaterialPageRoute(
                        builder: (_) => MapPickLocationPage(
                          initialLat: _pickedLat,
                          initialLng: _pickedLng,
                        ),
                      ),
                    );
                    if (result != null && result is Map<String, dynamic>) {
                      _pickedLat = result['lat'] as double;
                      _pickedLng = result['lng'] as double;
                      final coordStr = '${_pickedLat!.toStringAsFixed(6)},${_pickedLng!.toStringAsFixed(6)}';
                      setDialogState(() {
                        gpsController.text = coordStr;
                      });
                    }
                  },
                  child: AbsorbPointer(
                    child: TextField(
                      controller: gpsController,
                      readOnly: true,
                      decoration: InputDecoration(
                        labelText: '坐标（点击地图选择）',
                        border: const OutlineInputBorder(),
                        prefixIcon: const Icon(Icons.location_on),
                        suffixIcon: const Icon(Icons.map, color: Colors.blue),
                        hintText: '点击打开地图选点',
                        hintStyle: TextStyle(color: Colors.grey[400], fontSize: 13),
                      ),
                    ),
                  ),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: levelController,
                  decoration: const InputDecoration(
                    labelText: '等级',
                    border: OutlineInputBorder(),
                    prefixIcon: Icon(Icons.flag),
                  ),
                  keyboardType: TextInputType.number,
                ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(dialogContext),
              child: const Text('取消'),
            ),
            ElevatedButton(
              onPressed: () {
                final id = _getPlaceId(place);
                final name = nameController.text.trim();
                final gps = gpsController.text.trim();
                final level = levelController.text.trim();
                if (name.isEmpty) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('请输入地名名称')),
                  );
                  return;
                }
                Navigator.pop(dialogContext);
                _updatePlace(id, name, gps, level);
              },
              child: const Text('保存'),
            ),
          ],
        ),
      ),
    );
  }

  /// 更新地名
  Future<void> _updatePlace(String id, String name, String gps, String level) async {
    debugPrint('[地名管理] 更新地名: id=$id, name=$name, gps=$gps, level=$level');

    try {
      final resp = await http.post(
        Uri.parse(_placeFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'updatePlace',
          'info': {
            'placeid': id,
            'name': name,
            'gps': gps,
            'level': level,
          },
        }),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          debugPrint('[地名管理] 更新成功');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('更新成功'), backgroundColor: Colors.green),
            );
            _loadPlaces();
          }
        } else {
          debugPrint('[地名管理] 更新失败: ${json['msg']}');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(content: Text('更新失败: ${json['msg']}'), backgroundColor: Colors.red),
            );
          }
        }
      }
    } catch (e) {
      debugPrint('[地名管理] 更新失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('更新失败: $e'), backgroundColor: Colors.red),
        );
      }
    }
  }

  /// 确认删除地名
  void _confirmDeletePlace(Map<String, dynamic> place) {
    final name = _getPlaceName(place);
    final id = _getPlaceId(place);

    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('确认删除'),
        content: Text('确定要删除地名 "$name" (ID: $id) 吗？'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('取消'),
          ),
          ElevatedButton(
            onPressed: () {
              Navigator.pop(context);
              _deletePlace(place);
            },
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.red,
              foregroundColor: Colors.white,
            ),
            child: const Text('删除'),
          ),
        ],
      ),
    );
  }

  /// 删除地名
  Future<void> _deletePlace(Map<String, dynamic> place) async {
    final id = _getPlaceId(place);
    debugPrint('[地名管理] 删除地名: $id');

    try {
      final resp = await http.post(
        Uri.parse(_placeFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'deletePlace',
          'info': {'placeid': id},
        }),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          debugPrint('[地名管理] 删除成功');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('删除成功'), backgroundColor: Colors.green),
            );
            _loadPlaces();
          }
        } else {
          debugPrint('[地名管理] 删除失败: ${json['msg']}');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(content: Text('删除失败: ${json['msg']}'), backgroundColor: Colors.red),
            );
          }
        }
      }
    } catch (e) {
      debugPrint('[地名管理] 删除失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('删除失败: $e'), backgroundColor: Colors.red),
        );
      }
    }
  }
}
