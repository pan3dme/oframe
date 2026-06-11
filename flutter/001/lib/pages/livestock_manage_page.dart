import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

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

  @override
  void initState() {
    super.initState();
    _loadData();
  }

  Future<void> _loadData() async {
    setState(() {
      _isLoading = true;
      _loadStatus = '';
    });

    try {
      final resp = await http.post(
        Uri.parse(_cowSheepFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getLivestockList'}),
      );

      // 调试：打印完整响应
      debugPrint('FC 响应状态: ${resp.statusCode}');
      debugPrint('FC 响应体: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        print(json);
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>;
          
          // 调试：打印第一条原始数据
          if (rawRows.isNotEmpty) {
            debugPrint('第一条原始数据: ${rawRows[0]}');
          }
          
          setState(() {
            _data = rawRows.map(_parseOtsRow).toList();
            _isLoading = false;
          });
          debugPrint('牛羊表加载完成，共 ${_data.length} 条');
          
          // 调试：打印解析后的第一条数据
          if (_data.isNotEmpty) {
            debugPrint('解析后第一条: ${_data[0]}');
          }
        } else {
          setState(() {
            _loadStatus = '请求错误: ${json['msg']} ${json['error'] ?? ''}';
            _isLoading = false;
          });
        }
      } else {
        setState(() {
          _loadStatus = 'HTTP ${resp.statusCode}';
          _isLoading = false;
        });
      }
    } catch (e) {
      setState(() {
        _loadStatus = '连接失败: $e';
        _isLoading = false;
      });
      debugPrint('牛羊表请求失败: $e');
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
                    color: Colors.red.shade100,
                    child: Row(
                      children: [
                        const Icon(Icons.error_outline, color: Colors.red, size: 20),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(_loadStatus, style: const TextStyle(fontSize: 13), maxLines: 2, overflow: TextOverflow.ellipsis),
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
                            // 数据统计栏
                            Container(
                              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                              color: Theme.of(context).colorScheme.primaryContainer.withOpacity(0.3),
                              child: Row(
                                children: [
                                  const Icon(Icons.pets, size: 18),
                                  const SizedBox(width: 8),
                                  Text('共 ${_data.length} 头牛羊',
                                      style: const TextStyle(fontWeight: FontWeight.bold)),
                                ],
                              ),
                            ),
                            // 牛羊列表
                            Expanded(
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
                          ],
                        ),
                ),
              ],
            ),
    );
  }
}