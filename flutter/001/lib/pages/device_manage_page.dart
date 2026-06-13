import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';

/// FC 地址常量
const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

class DeviceManagePage extends StatefulWidget {
  const DeviceManagePage({super.key});

  @override
  State<DeviceManagePage> createState() => _DeviceManagePageState();
}

class _DeviceManagePageState extends State<DeviceManagePage> {
  List<Map<String, dynamic>> _data = [];
  Map<String, Map<String, dynamic>> _deviceLotMap = {}; // 设备LOT数据映射
  String _loadStatus = '';
  bool _isLoading = true;
  bool _isFromCache = false; // 标记是否使用缓存数据
  int _refreshCounter = 0; // 刷新计数器，用于强制重新渲染图片
  
  // 编辑表单控制器
  final TextEditingController _deviceKeyController = TextEditingController();
  final TextEditingController _renameController = TextEditingController();

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
    
    // 加载设备LOT数据
    await _loadDeviceLotData();
  }

  /// 从缓存加载数据
  Future<void> _loadFromCache() async {
    try {
      final cachedData = await DBHelper().getDevices();
      if (cachedData.isNotEmpty) {
        setState(() {
          _data = cachedData;
          _isLoading = false;
          // 不设置 _isFromCache 和 _loadStatus，等网络请求结果再决定
        });
        debugPrint('从缓存加载设备数据: ${cachedData.length} 条');
      }
    } catch (e) {
      debugPrint('加载缓存失败: $e');
    }
  }

  /// 加载设备LOT数据
  Future<void> _loadDeviceLotData() async {
    try {
      // 从缓存加载LOT数据
      final cachedLotData = await DBHelper().getDeviceLot();
      if (cachedLotData.isNotEmpty) {
        final lotMap = <String, Map<String, dynamic>>{};
        for (final lot in cachedLotData) {
          final deviceId = lot['deviceId'] as String;
          lotMap[deviceId] = lot;
        }
        setState(() {
          _deviceLotMap = lotMap;
        });
        debugPrint('从缓存加载设备LOT数据: ${cachedLotData.length} 条');
      }
      
      // 从网络加载LOT数据
      await _loadDeviceLotFromNetwork();
    } catch (e) {
      debugPrint('加载设备LOT数据失败: $e');
    }
  }

  /// 从网络加载设备LOT数据
  Future<void> _loadDeviceLotFromNetwork() async {
    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getDeviceLotRefreshAll'}),
      );

      debugPrint('设备LOT FC 响应状态: ${resp.statusCode}');
      debugPrint('设备LOT FC 响应体: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>;
          final parsedData = rawRows.map(_parseOtsRow).toList();
          
          // 保存到缓存
          await DBHelper().saveDeviceLot(parsedData);
          
          // 构建LOT数据映射
          final lotMap = <String, Map<String, dynamic>>{};
          for (final lot in parsedData) {
            final deviceId = lot['deviceId'] as String;
            lotMap[deviceId] = lot;
          }
          
          setState(() {
            _deviceLotMap = lotMap;
          });
          
          debugPrint('从网络加载设备LOT数据: ${parsedData.length} 条，已缓存');
        } else {
          debugPrint('设备LOT请求错误: ${json['msg']} ${json['error'] ?? ''}');
        }
      } else {
        debugPrint('设备LOT HTTP ${resp.statusCode}');
      }
    } catch (e) {
      debugPrint('设备LOT表请求失败: $e');
    }
  }

  /// 从网络加载数据
  Future<void> _loadFromNetwork() async {
    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({'action': 'getDeviceTaleAll'}),
      );

      debugPrint('FC 响应状态: ${resp.statusCode}');
      debugPrint('FC 响应体: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>;
          final parsedData = rawRows.map(_parseOtsRow).toList();
          
          // 保存到缓存
          await DBHelper().saveDevices(parsedData);
          
          setState(() {
            _data = parsedData;
            _isLoading = false;
            _isFromCache = false;
            _loadStatus = ''; // 清除所有提示
            _refreshCounter++; // 增加刷新计数，强制重新渲染图片
          });
          
          debugPrint('从网络加载设备数据: ${parsedData.length} 条，已缓存');
        } else {
          // 网络请求返回错误
          setState(() {
            _loadStatus = '请求错误: ${json['msg']} ${json['error'] ?? ''}';
            _isLoading = false;
            // 保持_isFromCache状态不变
          });
        }
      } else {
        setState(() {
          _loadStatus = 'HTTP ${resp.statusCode}';
          _isLoading = false;
        });
      }
    } catch (e) {
      debugPrint('设备表请求失败: $e');
      // 网络请求失败，如果有缓存数据则不显示错误
      if (_data.isEmpty) {
        setState(() {
          _loadStatus = '连接失败: $e';
          _isLoading = false;
        });
      } else {
        // 有缓存数据，网络失败，显示缓存模式
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

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('设备管理'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : Column(
              children: [
                // 状态提示条
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
                              Icon(Icons.devices, size: 64, color: Colors.grey),
                              SizedBox(height: 16),
                              Text('设备管理', style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                              SizedBox(height: 8),
                              Text('暂无数据', style: TextStyle(color: Colors.grey)),
                            ],
                          ),
                        )
                      : Column(
                          children: [
                            // 设备列表
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
                                    debugPrint('设备表字段: ${item.keys.toList()}');
                                    debugPrint('完整数据: $item');
                                  }
                                  
                                  final deviceId = _str(item, 'deviceId');
                                  final rename = _str(item, 'rename');
                                  final deviceKey = _str(item, 'device_key');
                                  final linkCowSheepId = _str(item, 'link_cowsheep_id');
                                  final picurl = _str(item, 'picurl');
                                  final hasImage = picurl != '—' && picurl.isNotEmpty;
                                  
                                  // 获取设备LOT数据
                                  final deviceLot = _deviceLotMap[deviceId];
                                  final timeRaw = deviceLot != null ? _str(deviceLot, 'time') : '—';
                                  
                                  // 计算相对时间显示
                                  String timeDisplay = timeRaw;
                                  String timeAgo = '';
                                  
                                  if (timeRaw != '—' && timeRaw.isNotEmpty) {
                                    // 计算时间差，显示相对时间
                                    try {
                                      // 支持格式: "2026/6/12 12:30:45" 或 "2026-06-12 12:30:45"
                                      if (timeRaw.contains(' ')) {
                                        final parts = timeRaw.split(' ');
                                        if (parts.length >= 2) {
                                          final datePart = parts[0]; // "2026/6/12" 或 "2026-06-12"
                                          final timePart = parts[1]; // "12:30:45"
                                          
                                          // 手动解析日期时间，避免DateTime.parse的格式限制
                                          final dateParts = datePart.replaceAll('/', '-').split('-');
                                          final timeParts = timePart.split(':');
                                          
                                          if (dateParts.length >= 3 && timeParts.length >= 2) {
                                            final year = int.parse(dateParts[0]);
                                            final month = int.parse(dateParts[1]);
                                            final day = int.parse(dateParts[2]);
                                            final hour = int.parse(timeParts[0]);
                                            final minute = int.parse(timeParts[1]);
                                            final second = timeParts.length >= 3 ? int.parse(timeParts[2]) : 0;
                                            
                                            final deviceTime = DateTime(year, month, day, hour, minute, second);
                                            final now = DateTime.now();
                                            final difference = now.difference(deviceTime);
                                            
                                            if (difference.inSeconds < 10) {
                                              timeAgo = '（刚刚）';
                                            } else if (difference.inSeconds < 60) {
                                              timeAgo = '（${difference.inSeconds}秒前）';
                                            } else if (difference.inMinutes < 60) {
                                              timeAgo = '（${difference.inMinutes}分钟前）';
                                            } else if (difference.inHours < 24) {
                                              timeAgo = '（${difference.inHours}小时前）';
                                            } else {
                                              timeAgo = '（${difference.inDays}天前）';
                                            }
                                          }
                                        }
                                      }
                                    } catch (e) {
                                      debugPrint('解析时间失败: $e, 原始时间: $timeRaw');
                                    }
                                  }
                                  
                                  // 构建显示名称：deviceId 或 deviceId (rename)
                                  final displayName = rename != '—' ? '$deviceId ($rename)' : deviceId;

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
                                              picurl,
                                              key: ValueKey('device_${deviceId}_$_refreshCounter'),
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
                                          // 第一行：deviceId (rename)
                                          Text(
                                            displayName,
                                            style: const TextStyle(
                                              fontSize: 15,
                                              fontWeight: FontWeight.bold,
                                            ),
                                            maxLines: 1,
                                            overflow: TextOverflow.ellipsis,
                                          ),
                                          const SizedBox(height: 6),
                                          // 第二行：time (从设备LOT表获取，显示完整时间)
                                          Text(
                                            '$timeDisplay$timeAgo',
                                            style: TextStyle(
                                              fontSize: 11,
                                              color: Colors.grey[700],
                                            ),
                                            maxLines: 1,
                                            overflow: TextOverflow.ellipsis,
                                          ),
                                          const SizedBox(height: 6),
                                          // 第三行：link_cowsheep_id
                                          Row(
                                            children: [
                                              Icon(
                                                Icons.pets,
                                                size: 14,
                                                color: linkCowSheepId != '—' ? Colors.green : Colors.grey,
                                              ),
                                              const SizedBox(width: 4),
                                              Expanded(
                                                child: Text(
                                                  linkCowSheepId != '—' ? linkCowSheepId : '未绑定',
                                                  style: TextStyle(
                                                    fontSize: 13,
                                                    color: linkCowSheepId != '—' ? Colors.green[700] : Colors.grey,
                                                  ),
                                                  maxLines: 1,
                                                  overflow: TextOverflow.ellipsis,
                                                ),
                                              ),
                                            ],
                                          ),
                                        ],
                                      ),
                                    ),
                                    
                                    const SizedBox(width: 1),
                                    
                                    // 第三列：两个图标（编辑 + 连接）
                                    Column(
                                      mainAxisSize: MainAxisSize.min,
                                      children: [
                                        IconButton(
                                          icon: const Icon(Icons.edit, color: Colors.blue, size: 24),
                                          padding: EdgeInsets.zero,
                                          constraints: const BoxConstraints(),
                                          onPressed: () {
                                            _showEditDialog(item);
                                          },
                                        ),
                                        const SizedBox(height: 8),
                                        IconButton(
                                          icon: Icon(
                                            Icons.link,
                                            color: linkCowSheepId != '—' ? Colors.green : Colors.orange,
                                            size: 24,
                                          ),
                                          padding: EdgeInsets.zero,
                                          constraints: const BoxConstraints(),
                                          onPressed: () {
                                            // 连接功能
                                          },
                                        ),
                                      ],
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

  /// 处理下拉刷新
  Future<void> _handleRefresh() async {
    await _loadData();
  }

  /// 信息行组件
  Widget _infoRow(IconData icon, String label, String value) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 6),
      child: Row(
        children: [
          Icon(icon, size: 16, color: Colors.grey[500]),
          const SizedBox(width: 8),
          Text('$label: ', style: TextStyle(fontSize: 14, color: Colors.grey[700])),
          Expanded(
            child: Text(value, style: const TextStyle(fontSize: 14), overflow: TextOverflow.ellipsis),
          ),
        ],
      ),
    );
  }

  /// 图片占位符（小尺寸，80x80）
  Widget _buildImagePlaceholderSmall() {
    return Container(
      width: 80,
      height: 80,
      decoration: BoxDecoration(
        color: Colors.grey.shade200,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Icon(Icons.image_not_supported, size: 32, color: Colors.grey[400]),
    );
  }

  /// 显示编辑对话框
  void _showEditDialog(Map<String, dynamic> item) {
    final deviceId = _str(item, 'deviceId');
    final picurl = _str(item, 'picurl');
    final hasImage = picurl != '—' && picurl.isNotEmpty;
    
    // 初始化表单数据
    _deviceKeyController.text = _str(item, 'device_key');
    _renameController.text = _str(item, 'rename');
    
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('编辑设备'),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // 图片显示
              Center(
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(10),
                  child: hasImage
                      ? Image.network(
                          picurl,
                          width: 150,
                          height: 150,
                          fit: BoxFit.cover,
                          errorBuilder: (context, error, stackTrace) {
                            return _buildImagePlaceholderLarge();
                          },
                        )
                      : _buildImagePlaceholderLarge(),
                ),
              ),
              const SizedBox(height: 16),
              
              // deviceId（不可编辑）
              TextField(
                controller: TextEditingController(text: deviceId),
                enabled: false,
                decoration: const InputDecoration(
                  labelText: '设备ID',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.devices),
                ),
              ),
              const SizedBox(height: 12),
              
              // device_key（可编辑）
              TextField(
                controller: _deviceKeyController,
                decoration: const InputDecoration(
                  labelText: '设备编号 (device_key)',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.key),
                ),
                keyboardType: TextInputType.number,
              ),
              const SizedBox(height: 12),
              
              // rename（可编辑）
              TextField(
                controller: _renameController,
                decoration: const InputDecoration(
                  labelText: '别名 (rename)',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.edit_note),
                ),
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
              _saveDevice(item);
              Navigator.pop(context);
            },
            child: const Text('保存'),
          ),
        ],
      ),
    );
  }

  /// 保存图片占位符
  Widget _buildImagePlaceholderLarge() {
    return Container(
      width: 150,
      height: 150,
      decoration: BoxDecoration(
        color: Colors.grey.shade200,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Icon(Icons.image_not_supported, size: 64, color: Colors.grey[400]),
    );
  }

  /// 保存设备信息
  Future<void> _saveDevice(Map<String, dynamic> item) async {
    final deviceId = item['deviceId'];
    final picurl = item['picurl'];
    final newDeviceKey = _deviceKeyController.text;
    final newRename = _renameController.text;
    
    debugPrint('保存设备: deviceId=$deviceId, deviceKey=$newDeviceKey, rename=$newRename, picurl=$picurl');
    
    try {
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'updateDevice',
          'info': {
            'deviceId': deviceId,
            'device_key': newDeviceKey,
            'rename': newRename,
            'picurl': picurl,
          },
        }),
      );
      
      debugPrint('保存响应: ${resp.statusCode} - ${resp.body}');
      
      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          debugPrint('保存成功');
          // 保存成功后刷新列表
          _loadData();
        } else {
          debugPrint('保存失败: ${json['msg']}');
        }
      }
    } catch (e) {
      debugPrint('保存设备失败: $e');
    }
  }
}