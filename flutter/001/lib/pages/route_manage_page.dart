import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

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

  @override
  void initState() {
    super.initState();
    _loadRoutes();
  }

  /// 加载道路列表
  Future<void> _loadRoutes() async {
    setState(() {
      _isLoading = true;
      _errorMessage = '';
    });

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
          final routes = data.map((e) => _parseRoute(e as Map<String, dynamic>)).toList();
          setState(() {
            _routes = routes;
            _isLoading = false;
          });
          debugPrint('[道路管理] 加载成功: ${routes.length}条');
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
    return route['deviceId']?.toString() ?? '';
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
        title: const Text('道路管理'),
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

    return Card(
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
                // 按钮列
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
    );
  }

  /// 显示新增道路对话框
  void _showAddRouteDialog() {
    final nameController = TextEditingController();
    final levelController = TextEditingController(text: '1');

    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('新增道路'),
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
              final name = nameController.text.trim();
              final level = levelController.text.trim();
              if (name.isEmpty) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('请输入道路名称')),
                );
                return;
              }
              Navigator.pop(context);
              _addRoute(name, level);
            },
            child: const Text('确定'),
          ),
        ],
      ),
    );
  }

  /// 新增道路
  Future<void> _addRoute(String name, String level) async {
    debugPrint('[道路管理] 新增道路: name=$name, level=$level');

    try {
      final resp = await http.post(
        Uri.parse(_routeFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'addRoad',
          'info': {
            'roadid': 'id${DateTime.now().millisecondsSinceEpoch ~/ 1000}',
            'roadname': name,
            'level': level,
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
            'roadid': id,
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
          'info': {'roadid': id},
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
