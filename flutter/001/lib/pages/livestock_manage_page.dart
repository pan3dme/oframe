import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';

/// FC 地址常量
const String _cowSheepFcUrl = 'https://gpsmoveinfo.cn/fc/cowsheep';

class LivestockManagePage extends StatefulWidget {
  const LivestockManagePage({super.key});

  @override
  State<LivestockManagePage> createState() => _LivestockManagePageState();
}

class _LivestockManagePageState extends State<LivestockManagePage> {
  List<Map<String, dynamic>> _data = [];
  String _loadStatus = '';
  bool _isLoading = true;
  bool _isFromCache = false; // 标记是否使用缓存数据
  int _refreshCounter = 0; // 刷新计数器，用于强制重新渲染图片

  @override
  void initState() {
    super.initState();
    _loadData();
  }

  Future<void> _loadData() async {
    // 先尝试加载缓存数据
    await _loadFromCache();
    
    // 然后尝试从网络获取最新数据
    await _loadFromNetwork();
  }

  /// 从缓存加载数据
  Future<void> _loadFromCache() async {
    try {
      debugPrint('[牛羊缓存] 开始读取缓存数据');
      final cachedData = await DBHelper().getLivestock();
      debugPrint('[牛羊缓存] 读取到 ${cachedData.length} 条数据');
      
      if (cachedData.isNotEmpty) {
        debugPrint('[牛羊缓存] 第一条数据: ${cachedData[0]}');
        setState(() {
          _data = cachedData;
          _isLoading = false;
          // 不设置 _isFromCache 和 _loadStatus，等网络请求结果再决定
        });
        debugPrint('[牛羊缓存] ✓ 已加载缓存数据，显示列表');
      } else {
        debugPrint('[牛羊缓存] ✗ 缓存为空，将等待网络请求');
      }
    } catch (e, stackTrace) {
      debugPrint('[牛羊缓存] ✗ 加载缓存失败: $e');
      debugPrint('[牛羊缓存] 堆栈: $stackTrace');
    }
  }

  /// 从网络加载数据
  Future<void> _loadFromNetwork() async {
    try {
      debugPrint('[牛羊] 开始请求网络: $_cowSheepFcUrl');
      debugPrint('[牛羊] 请求参数: action=getLivestockList');
      
      final resp = await http.post(
        Uri.parse(_cowSheepFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getLivestockList'}),
      );

      debugPrint('[牛羊] FC 响应状态: ${resp.statusCode}');
      debugPrint('[牛羊] FC 响应体: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        debugPrint('[牛羊] 解析JSON成功, status=${json['status']}');
        
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>;
          debugPrint('[牛羊] 原始数据行数: ${rawRows.length}');
          
          final parsedData = rawRows.map(_parseOtsRow).toList();
          debugPrint('[牛羊] 解析后数据: ${parsedData.length} 条');
          
          if (parsedData.isNotEmpty) {
            debugPrint('[牛羊] 第一条数据: ${parsedData[0]}');
          }
          
          // 保存到缓存
          debugPrint('[牛羊] 开始保存 ${parsedData.length} 条数据到缓存');
          try {
            await DBHelper().saveLivestock(parsedData);
            debugPrint('[牛羊] ✓ 已保存到缓存');
          } catch (e) {
            debugPrint('[牛羊] ✗ 保存缓存失败（不影响显示）: $e');
          }
          
          setState(() {
            _data = parsedData;
            _isLoading = false;
            _isFromCache = false; // 网络请求成功，不是缓存
            _loadStatus = ''; // 清除所有提示
            _refreshCounter++; // 增加刷新计数，强制重新渲染图片
          });
          
          debugPrint('[牛羊] ✓ 从网络加载牛羊数据: ${parsedData.length} 条，已缓存');
        } else {
          debugPrint('[牛羊] 请求返回错误: ${json['msg']} ${json['error'] ?? ''}');
          // 网络请求返回错误
          setState(() {
            _loadStatus = '请求错误: ${json['msg']} ${json['error'] ?? ''}';
            _isLoading = false;
            // 保持_isFromCache状态不变
          });
        }
      } else {
        debugPrint('[牛羊] HTTP 错误: ${resp.statusCode}');
        setState(() {
          _loadStatus = 'HTTP ${resp.statusCode}';
          _isLoading = false;
        });
      }
    } catch (e, stackTrace) {
      debugPrint('[牛羊] 请求异常: $e');
      debugPrint('[牛羊] 堆栈: $stackTrace');
      debugPrint('[牛羊] 当前数据状态: _data.isEmpty=${_data.isEmpty}, _data.length=${_data.length}');
      debugPrint('[牛羊] _isFromCache=$_isFromCache, _isLoading=$_isLoading');
      
      // 网络请求失败，如果有缓存数据则不显示错误
      if (_data.isEmpty) {
        debugPrint('[牛羊] ✗ 无缓存数据，显示错误提示');
        setState(() {
          _loadStatus = '连接失败: $e';
          _isLoading = false;
        });
      } else {
        // 有缓存数据，网络失败，显示缓存模式
        debugPrint('[牛羊] ✓ 使用缓存数据，显示离线模式');
        setState(() {
          _isFromCache = true;
          _loadStatus = '使用缓存数据（离线模式）';
          _isLoading = false;
        });
      }
    }
  }

  /// 解析 TableStore Node.js SDK 返回的行数据
  Map<String, dynamic> _parseOtsRow(dynamic rawRow) {
    final row = rawRow as Map<String, dynamic>;
    final result = <String, dynamic>{};

    final pkList = row['primaryKey'] as List<dynamic>? ?? [];
    for (final pk in pkList) {
      final pkMap = pk as Map<String, dynamic>;
      result[pkMap['name'] as String] = pkMap['value'];
    }

    final attrList = row['attributes'] as List<dynamic>? ?? [];
    for (final attr in attrList) {
      final attrMap = attr as Map<String, dynamic>;
      result[attrMap['columnName'] as String] = attrMap['columnValue'];
    }

    return result;
  }

  /// 从记录中安全取值，空值显示占位文字
  String _str(Map<String, dynamic> item, String key) {
    final v = item[key];
    if (v == null || v.toString().isEmpty) return '—';
    return v.toString();
  }

  /// 图片占位符
  Widget _buildImagePlaceholderSmall() {
    return Container(
      width: 80,
      height: 80,
      color: Colors.grey[200],
      child: Icon(
        Icons.pets,
        size: 40,
        color: Colors.grey[400],
      ),
    );
  }

  /// 处理下拉刷新
  Future<void> _handleRefresh() async {
    await _loadData();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('牛羊管理'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : Column(
              children: [
                if (_loadStatus.isNotEmpty)
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
                    color: _isFromCache ? Colors.blue.shade100 : Colors.red.shade100,
                    child: Row(
                      children: [
                        Icon(
                          _isFromCache ? Icons.cloud_off : Icons.error_outline,
                          color: _isFromCache ? Colors.blue : Colors.red,
                          size: 20,
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            _loadStatus,
                            style: const TextStyle(fontSize: 13),
                            maxLines: 2,
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                        if (_isFromCache)
                          IconButton(
                            icon: const Icon(Icons.refresh, size: 20),
                            onPressed: _loadData,
                            tooltip: '刷新',
                          ),
                      ],
                    ),
                  ),
                Expanded(
                  child: _data.isEmpty
                      ? const Center(
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Icon(Icons.pets, size: 64, color: Colors.grey),
                              SizedBox(height: 16),
                              Text('牛羊管理', style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                              SizedBox(height: 8),
                              Text('暂无数据', style: TextStyle(color: Colors.grey)),
                            ],
                          ),
                        )
                      : Column(
                          children: [
                            // 牛羊列表
                            Expanded(
                              child: RefreshIndicator(
                                onRefresh: _handleRefresh,
                                child: ListView.builder(
                                padding: const EdgeInsets.all(12),
                                itemCount: _data.length,
                                itemBuilder: (context, index) {
                                  final item = _data[index];
                                  
                                  // 调试：打印第一条记录的字段名
                                  if (index == 0) {
                                    debugPrint('牛羊表字段: ${item.keys.toList()}');
                                    debugPrint('完整数据: $item');
                                  }
                                  
                                  final cowsheepId = _str(item, 'cowsheep_id');
                                  final birthday = _str(item, 'birthday');
                                  final gender = item['gender'] == true ? '公' : (item['gender'] == false ? '母' : '—');
                                  final avatar = _str(item, 'avatar');
                                  final rename = _str(item, 'rename');
                                  final hasImage = avatar != '—' && avatar.isNotEmpty;

                                  return Card(
                                    margin: const EdgeInsets.only(bottom: 10),
                                    elevation: 2,
                                    shape: RoundedRectangleBorder(
                                      borderRadius: BorderRadius.circular(12),
                                    ),
                                    child: Padding(
                                      padding: const EdgeInsets.all(12),
                                      child: Row(
                                        crossAxisAlignment: CrossAxisAlignment.center,
                                        children: [
                                          // 第一列：图片（圆角）
                                          ClipRRect(
                                            borderRadius: BorderRadius.circular(10),
                                            child: hasImage
                                                ? Image.network(
                                                    avatar,
                                                    key: ValueKey('livestock_${cowsheepId}_$_refreshCounter'),
                                                    width: 80,
                                                    height: 80,
                                                    fit: BoxFit.cover,
                                                    errorBuilder: (context, error, stackTrace) {
                                                      return _buildImagePlaceholderSmall();
                                                    },
                                                  )
                                                : _buildImagePlaceholderSmall(),
                                          ),
                                          
                                          const SizedBox(width: 12),
                                          
                                          // 第二列：三行信息
                                          Expanded(
                                            child: Column(
                                              crossAxisAlignment: CrossAxisAlignment.start,
                                              mainAxisAlignment: MainAxisAlignment.center,
                                              children: [
                                                // 第一行：牛羊名称或ID
                                                Text(
                                                  rename != '—' ? rename : cowsheepId,
                                                  style: const TextStyle(
                                                    fontSize: 15,
                                                    fontWeight: FontWeight.bold,
                                                  ),
                                                  maxLines: 1,
                                                  overflow: TextOverflow.ellipsis,
                                                ),
                                                const SizedBox(height: 6),
                                                // 第二行：生日
                                                Text(
                                                  '生日: $birthday',
                                                  style: TextStyle(
                                                    fontSize: 13,
                                                    color: Colors.grey[700],
                                                  ),
                                                  maxLines: 1,
                                                  overflow: TextOverflow.ellipsis,
                                                ),
                                                const SizedBox(height: 6),
                                                // 第三行：性别
                                                Row(
                                                  children: [
                                                    Icon(
                                                      gender == '公' ? Icons.male : (gender == '母' ? Icons.female : Icons.help_outline),
                                                      size: 14,
                                                      color: gender == '公' ? Colors.blue : (gender == '母' ? Colors.pink : Colors.grey),
                                                    ),
                                                    const SizedBox(width: 4),
                                                    Text(
                                                      gender != '—' ? '性别: $gender' : '性别未知',
                                                      style: TextStyle(
                                                        fontSize: 13,
                                                        color: gender != '—' ? Colors.green[700] : Colors.grey,
                                                      ),
                                                    ),
                                                  ],
                                                ),
                                              ],
                                            ),
                                          ),
                                        ],
                                      ),
                                    ),
                                  );
                                },
                              ),
                            ),
                          ),
                          ],
                        ),
                ),
              ],
            ),
    );
  }
}