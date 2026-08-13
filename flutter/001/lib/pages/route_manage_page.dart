import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';
import 'gps_path_record_page.dart';
import 'route_detail_map_page.dart';

/// FC 地址常量
const String _routeFcUrl = 'https://gpsmoveinfo.cn/fc/route_place';

class RouteManagePage extends StatefulWidget {
  const RouteManagePage({super.key});

  @override
  State<RouteManagePage> createState() => _RouteManagePageState();
}

class _RouteManagePageState extends State<RouteManagePage> {
  List<Map<String, dynamic>> _routes = [];
  bool _isLoading = true;
  String _errorMessage = '';
  bool _isUsingCache = false; // 是否正在使用缓存数据
  bool _isAdmin = false; // 是否为管理员模式

  @override
  void initState() {
    super.initState();
    _loadAdminSetting();
    _loadRoutes();
  }

  /// 加载管理员设置
  Future<void> _loadAdminSetting() async {
    try {
      final isAdmin = await DBHelper().getBoolSetting(
        'is_admin_mode',
        defaultValue: false,
      );
      setState(() {
        _isAdmin = isAdmin;
      });
    } catch (e) {
      debugPrint('[道路管理] 加载管理员设置失败: $e');
    }
  }

  /// 加载道路列表（先缓存后网络）
  Future<void> _loadRoutes() async {
    setState(() {
      _isLoading = _routes.isEmpty; // 只有首次无数据时显示loading
      _errorMessage = '';
      _isUsingCache = false;
    });

    // 第一步：先从本地缓存加载，立即显示
    try {
      final cachedRawRoutes = await DBHelper().getAllRoutes();
      if (cachedRawRoutes.isNotEmpty) {
        final cachedRoutes = cachedRawRoutes.map((e) => _parseRoute(e)).toList();
        setState(() {
          _routes = cachedRoutes;
          _isLoading = false;
          _isUsingCache = true;
        });
        debugPrint('[道路管理] 从缓存加载: ${cachedRoutes.length}条');
      }
    } catch (e) {
      debugPrint('[道路管理] 缓存加载失败: $e');
    }

    // 第二步：尝试从网络加载最新数据
    try {
      final resp = await http.post(
        Uri.parse(_routeFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getroutetableall'}),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final data = json['data'] as List<dynamic>;
          // 保存原始数据到缓存
          final rawList = data.map((e) => e as Map<String, dynamic>).toList();
          await DBHelper().saveRoutes(rawList);
          // 解析并显示
          final routes = rawList.map((e) => _parseRoute(e)).toList();
          setState(() {
            _routes = routes;
            _isLoading = false;
            _isUsingCache = false;
          });
          debugPrint('[道路管理] 网络加载成功: ${routes.length}条，已缓存');
          return;
        } else {
          // 网络返回失败，如果有缓存则用缓存
          if (_routes.isNotEmpty) {
            setState(() { _isUsingCache = true; });
            debugPrint('[道路管理] 网络返回失败，使用缓存数据');
            return;
          }
          setState(() {
            _errorMessage = json['msg'] ?? '加载失败';
            _isLoading = false;
          });
          return;
        }
      } else {
        if (_routes.isNotEmpty) {
          setState(() { _isUsingCache = true; });
          return;
        }
        setState(() {
          _errorMessage = 'HTTP错误: ${resp.statusCode}';
          _isLoading = false;
        });
        return;
      }
    } catch (e) {
      // 网络异常，使用缓存数据
      if (_routes.isNotEmpty) {
        setState(() { _isUsingCache = true; });
        debugPrint('[道路管理] 网络异常，使用缓存数据: $e');
        return;
      }
      setState(() {
        _errorMessage = '网络连接失败，且无缓存数据';
        _isLoading = false;
      });
    }
  }

  /// 解析道路数据
  Map<String, dynamic> _parseRoute(Map<String, dynamic> raw) {
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

  /// 获取道路名称
  String _getRouteName(Map<String, dynamic> route) {
    return route['roadname']?.toString() ?? '未命名';
  }

  /// 获取道路ID
  String _getRouteId(Map<String, dynamic> route) {
    return route['route_id']?.toString() ?? '';
  }

  /// 获取道路等级
  int _getRouteLevel(Map<String, dynamic> route) {
    final level = route['level'];
    if (level == null) return 1;
    return int.tryParse(level.toString()) ?? 1;
  }

  /// 获取路径点文本（截断显示）
  String _getPathText(Map<String, dynamic> route) {
    final roadinfo = route['roadinfo']?.toString() ?? '';
    if (roadinfo.isEmpty) return '—';
    // 截断显示
    if (roadinfo.length > 40) {
      return '${roadinfo.substring(0, 40)}...';
    }
    return roadinfo;
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(_isUsingCache ? '道路管理(断网)' : '道路管理'),
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
                        onPressed: _loadRoutes,
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
                            '共 ${_routes.length} 条道路',
                            style: TextStyle(fontSize: 14, color: Colors.grey[600]),
                          ),
                          const Spacer(),
                          if (_isAdmin)
                            ElevatedButton.icon(
                              onPressed: () => _showAddRouteDialog(),
                              icon: const Icon(Icons.add, size: 18),
                              label: const Text('新增道路'),
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
                    // 道路列表
                    Expanded(
                      child: _routes.isEmpty
                          ? const Center(
                              child: Column(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  Icon(Icons.route, size: 64, color: Colors.grey),
                                  SizedBox(height: 16),
                                  Text('暂无道路数据', style: TextStyle(color: Colors.grey)),
                                ],
                              ),
                            )
                          : RefreshIndicator(
                              onRefresh: _loadRoutes,
                              child: ListView.builder(
                                padding: const EdgeInsets.fromLTRB(12, 0, 12, 12),
                                itemCount: _routes.length,
                                itemBuilder: (context, index) {
                                  final route = _routes[index];
                                  return _buildRouteCard(route, index);
                                },
                              ),
                            ),
                    ),
                  ],
                ),
    );
  }

  /// 构建道路卡片
  Widget _buildRouteCard(Map<String, dynamic> route, int index) {
    final name = _getRouteName(route);
    final id = _getRouteId(route);
    final level = _getRouteLevel(route);
    final pathText = _getPathText(route);

    return GestureDetector(
      onTap: () {
        // 点击卡片打开地图详情页
        Navigator.push(
          context,
          MaterialPageRoute(
            builder: (_) => RouteDetailMapPage(route: route),
          ),
        );
      },
      child: Card(
        margin: const EdgeInsets.only(bottom: 8),
        elevation: 1,
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // 第一行：序号 + 名称 + 等级 + 按钮
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
                          '路径点: $pathText',
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
                  // 按钮列（仅管理员可见）
                  if (_isAdmin)
                    Column(
                      children: [
                        OutlinedButton(
                          onPressed: () => _showEditRouteDialog(route),
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
                          onPressed: () => _confirmDeleteRoute(route),
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
      ),
    );
  }

  /// 显示新增道路对话框
  void _showAddRouteDialog() {
    final nameController = TextEditingController();
    final levelController = TextEditingController(text: '1');
    String roadinfo = '';   // 坐标字符串 "lat1,lng1,lat2,lng2,..."
    int pointCount = 0;     // 已记录点数
    // 用于在 setState 中更新弹框内显示
    final ValueNotifier<String> roadinfoNotifier = ValueNotifier('');
    final ValueNotifier<int> pointCountNotifier = ValueNotifier(0);

    showDialog(
      context: context,
      builder: (dialogContext) => StatefulBuilder(
        builder: (context, setDialogState) => AlertDialog(
          title: const Text('新增道路'),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                TextField(
                  controller: nameController,
                  decoration: const InputDecoration(
                    labelText: '道路名称',
                    border: OutlineInputBorder(),
                    prefixIcon: Icon(Icons.route),
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
                const SizedBox(height: 16),
                // 获取路径按钮
                Row(
                  children: [
                    Expanded(
                      child: OutlinedButton.icon(
                        onPressed: () async {
                          // 先关闭弹框，打开地图记录页
                          Navigator.pop(dialogContext);
                          final result = await Navigator.push<Map<String, dynamic>>(
                            context,
                            MaterialPageRoute(
                              builder: (_) => const GpsPathRecordPage(),
                            ),
                          );
                          // 地图页返回后重新打开弹框
                          if (result != null && mounted) {
                            roadinfo = result['roadinfo'] as String? ?? '';
                            pointCount = result['pointCount'] as int? ?? 0;
                            roadinfoNotifier.value = roadinfo;
                            pointCountNotifier.value = pointCount;
                            // 重新打开弹框，保留已填内容
                            _showAddRouteDialogWithResult(
                              nameController.text,
                              levelController.text,
                              roadinfo,
                              pointCount,
                            );
                          } else if (mounted) {
                            // 用户取消了地图页，重新打开弹框
                            _showAddRouteDialogWithResult(
                              nameController.text,
                              levelController.text,
                              roadinfo,
                              pointCount,
                            );
                          }
                        },
                        icon: const Icon(Icons.gps_fixed, size: 18),
                        label: const Text('获取路径'),
                        style: OutlinedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(vertical: 12),
                          side: const BorderSide(color: Color(0xFF2ECC71)),
                          foregroundColor: const Color(0xFF2ECC71),
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                // 路径信息显示
                ValueListenableBuilder<String>(
                  valueListenable: roadinfoNotifier,
                  builder: (context, val, _) {
                    if (val.isEmpty) {
                      return const Text(
                        '尚未获取路径坐标',
                        style: TextStyle(fontSize: 12, color: Colors.grey),
                      );
                    }
                    return ValueListenableBuilder<int>(
                      valueListenable: pointCountNotifier,
                      builder: (context, count, _) => Container(
                        padding: const EdgeInsets.all(8),
                        decoration: BoxDecoration(
                          color: const Color(0xFFE8F8F0),
                          borderRadius: BorderRadius.circular(6),
                          border: Border.all(color: const Color(0xFF2ECC71)),
                        ),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Row(
                              children: [
                                const Icon(Icons.check_circle, color: Color(0xFF2ECC71), size: 14),
                                const SizedBox(width: 4),
                                Text(
                                  '已记录 $count 个坐标点',
                                  style: const TextStyle(
                                    fontSize: 12,
                                    color: Color(0xFF2ECC71),
                                    fontWeight: FontWeight.w600,
                                  ),
                                ),
                              ],
                            ),
                            const SizedBox(height: 4),
                            Text(
                              val.length > 60 ? '${val.substring(0, 60)}...' : val,
                              style: const TextStyle(fontSize: 10, color: Colors.grey),
                              maxLines: 2,
                              overflow: TextOverflow.ellipsis,
                            ),
                          ],
                        ),
                      ),
                    );
                  },
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
                final level = levelController.text.trim();
                if (name.isEmpty) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('请输入道路名称')),
                  );
                  return;
                }
                if (roadinfo.isEmpty) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('请先获取路径坐标'), backgroundColor: Colors.orange),
                  );
                  return;
                }
                Navigator.pop(dialogContext);
                _addRoute(name, level, roadinfo);
              },
              child: const Text('确定'),
            ),
          ],
        ),
      ),
    );
  }

  /// 带已有数据的新增道路弹框（从地图页返回后恢复数据）
  void _showAddRouteDialogWithResult(String initName, String initLevel, String initRoadinfo, int initPointCount) {
    final nameController = TextEditingController(text: initName);
    final levelController = TextEditingController(text: initLevel);
    String roadinfo = initRoadinfo;

    showDialog(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: const Text('新增道路'),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              TextField(
                controller: nameController,
                decoration: const InputDecoration(
                  labelText: '道路名称',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.route),
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
              const SizedBox(height: 16),
              Row(
                children: [
                  Expanded(
                    child: OutlinedButton.icon(
                      onPressed: () async {
                        Navigator.pop(dialogContext);
                        final result = await Navigator.push<Map<String, dynamic>>(
                          context,
                          MaterialPageRoute(builder: (_) => const GpsPathRecordPage()),
                        );
                        if (result != null && mounted) {
                          final newRoadinfo = result['roadinfo'] as String? ?? '';
                          final newCount = result['pointCount'] as int? ?? 0;
                          _showAddRouteDialogWithResult(
                            nameController.text,
                            levelController.text,
                            newRoadinfo,
                            newCount,
                          );
                        } else if (mounted) {
                          _showAddRouteDialogWithResult(
                            nameController.text,
                            levelController.text,
                            roadinfo,
                            initPointCount,
                          );
                        }
                      },
                      icon: const Icon(Icons.gps_fixed, size: 18),
                      label: const Text('重新获取路径'),
                      style: OutlinedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(vertical: 12),
                        side: const BorderSide(color: Color(0xFF2ECC71)),
                        foregroundColor: const Color(0xFF2ECC71),
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 8),
              Container(
                padding: const EdgeInsets.all(8),
                decoration: BoxDecoration(
                  color: const Color(0xFFE8F8F0),
                  borderRadius: BorderRadius.circular(6),
                  border: Border.all(color: const Color(0xFF2ECC71)),
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        const Icon(Icons.check_circle, color: Color(0xFF2ECC71), size: 14),
                        const SizedBox(width: 4),
                        Text(
                          '已记录 $initPointCount 个坐标点',
                          style: const TextStyle(
                            fontSize: 12,
                            color: Color(0xFF2ECC71),
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 4),
                    Text(
                      initRoadinfo.length > 60 ? '${initRoadinfo.substring(0, 60)}...' : initRoadinfo,
                      style: const TextStyle(fontSize: 10, color: Colors.grey),
                      maxLines: 2,
                      overflow: TextOverflow.ellipsis,
                    ),
                  ],
                ),
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
              final level = levelController.text.trim();
              if (name.isEmpty) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('请输入道路名称')),
                );
                return;
              }
              if (roadinfo.isEmpty) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('请先获取路径坐标'), backgroundColor: Colors.orange),
                );
                return;
              }
              Navigator.pop(dialogContext);
              _addRoute(name, level, roadinfo);
            },
            child: const Text('确定'),
          ),
        ],
      ),
    );
  }

  /// 新增道路
  Future<void> _addRoute(String name, String level, String roadinfo) async {
    debugPrint('[道路管理] 新增道路: name=$name, level=$level, roadinfo=$roadinfo');

    try {
      final resp = await http.post(
        Uri.parse(_routeFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'addRoad',
          'info': {
            'route_id': 'id${DateTime.now().millisecondsSinceEpoch ~/ 1000}',
            'roadname': name,
            'level': level,
            'roadinfo': roadinfo,
          },
        }),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          debugPrint('[道路管理] 新增成功');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('新增成功'), backgroundColor: Colors.green),
            );
            _loadRoutes();
          }
        } else {
          debugPrint('[道路管理] 新增失败: ${json['msg']}');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(content: Text('新增失败: ${json['msg']}'), backgroundColor: Colors.red),
            );
          }
        }
      }
    } catch (e) {
      debugPrint('[道路管理] 新增失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('新增失败: $e'), backgroundColor: Colors.red),
        );
      }
    }
  }

  /// 显示编辑道路对话框
  void _showEditRouteDialog(Map<String, dynamic> route) {
    final nameController = TextEditingController(text: _getRouteName(route));
    final levelController = TextEditingController(text: _getRouteLevel(route).toString());

    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('编辑道路'),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              TextField(
                controller: nameController,
                decoration: const InputDecoration(
                  labelText: '道路名称',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.route),
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
            onPressed: () => Navigator.pop(context),
            child: const Text('取消'),
          ),
          ElevatedButton(
            onPressed: () {
              final id = _getRouteId(route);
              final name = nameController.text.trim();
              final level = levelController.text.trim();
              if (name.isEmpty) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('请输入道路名称')),
                );
                return;
              }
              Navigator.pop(context);
              _updateRoute(id, name, level);
            },
            child: const Text('保存'),
          ),
        ],
      ),
    );
  }

  /// 更新道路
  Future<void> _updateRoute(String id, String name, String level) async {
    debugPrint('[道路管理] 更新道路: id=$id, name=$name, level=$level');

    try {
      final resp = await http.post(
        Uri.parse(_routeFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'updateRoad',
          'info': {
            'route_id': id,
            'roadname': name,
            'level': level,
          },
        }),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          debugPrint('[道路管理] 更新成功');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('更新成功'), backgroundColor: Colors.green),
            );
            _loadRoutes();
          }
        } else {
          debugPrint('[道路管理] 更新失败: ${json['msg']}');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(content: Text('更新失败: ${json['msg']}'), backgroundColor: Colors.red),
            );
          }
        }
      }
    } catch (e) {
      debugPrint('[道路管理] 更新失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('更新失败: $e'), backgroundColor: Colors.red),
        );
      }
    }
  }

  /// 确认删除道路
  void _confirmDeleteRoute(Map<String, dynamic> route) {
    final name = _getRouteName(route);
    final id = _getRouteId(route);

    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('确认删除'),
        content: Text('确定要删除道路 "$name" (ID: $id) 吗？'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('取消'),
          ),
          ElevatedButton(
            onPressed: () {
              Navigator.pop(context);
              _deleteRoute(route);
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

  /// 删除道路
  Future<void> _deleteRoute(Map<String, dynamic> route) async {
    final id = _getRouteId(route);
    debugPrint('[道路管理] 删除道路: $id');

    try {
      final resp = await http.post(
        Uri.parse(_routeFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'deleteRoad',
          'info': {'route_id': id},
        }),
      );

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          debugPrint('[道路管理] 删除成功');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('删除成功'), backgroundColor: Colors.green),
            );
            _loadRoutes();
          }
        } else {
          debugPrint('[道路管理] 删除失败: ${json['msg']}');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(content: Text('删除失败: ${json['msg']}'), backgroundColor: Colors.red),
            );
          }
        }
      }
    } catch (e) {
      debugPrint('[道路管理] 删除失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('删除失败: $e'), backgroundColor: Colors.red),
        );
      }
    }
  }
}
