import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

/// 设备详情页面
class DeviceDetailPage extends StatefulWidget {
  final Map<String, dynamic> device;
  final Map<String, dynamic>? deviceLot;

  const DeviceDetailPage({
    super.key,
    required this.device,
    this.deviceLot,
  });

  @override
  State<DeviceDetailPage> createState() => _DeviceDetailPageState();
}

class _DeviceDetailPageState extends State<DeviceDetailPage> {
  List<Map<String, dynamic>> _logs = [];
  bool _isLoadingLogs = false;
  bool _isLoadingMore = false;
  bool _hasMore = true;
  int _logOffset = 0;
  static const int _logLimit = 10;

  // FC地址
  static const String _deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';

  @override
  void initState() {
    super.initState();
    _loadLogs(reset: true);
  }

  /// 加载设备日志记录
  /// [reset] true=刷新（从头加载），false=加载更多（追加）
  Future<void> _loadLogs({bool reset = false}) async {
    if (reset) {
      if (_isLoadingLogs) return;
      setState(() {
        _isLoadingLogs = true;
        _logOffset = 0;
        _hasMore = true;
      });
    } else {
      if (_isLoadingMore || !_hasMore) return;
      setState(() {
        _isLoadingMore = true;
      });
    }

    try {
      final deviceId = widget.device['deviceId']?.toString() ?? '';
      
      final resp = await http.post(
        Uri.parse(_deviceFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getDeviceLogbyId',
          'info': {
            'deviceId': deviceId,
            'limit': _logLimit,
            'offset': _logOffset,
          },
        }),
      );

      debugPrint('设备日志响应: ${resp.statusCode}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>? ?? [];
          final parsedLogs = rawRows.map((row) {
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
            
            return result;
          }).toList();

          setState(() {
            if (reset) {
              _logs = parsedLogs;
            } else {
              _logs.addAll(parsedLogs);
            }
            _logOffset = _logs.length;
            _hasMore = parsedLogs.length >= _logLimit;
            _isLoadingLogs = false;
            _isLoadingMore = false;
          });
          
          debugPrint('加载日志: ${parsedLogs.length} 条，总计 ${_logs.length} 条');
        } else {
          debugPrint('日志请求错误: ${json['msg']}');
          setState(() {
            _isLoadingLogs = false;
            _isLoadingMore = false;
          });
        }
      } else {
        debugPrint('HTTP错误: ${resp.statusCode}');
        setState(() {
          _isLoadingLogs = false;
          _isLoadingMore = false;
        });
      }
    } catch (e) {
      debugPrint('加载日志失败: $e');
      setState(() {
        _isLoadingLogs = false;
        _isLoadingMore = false;
      });
    }
  }

  /// 获取设备ID显示颜色
  Color _getDeviceIdColor(String deviceId) {
    final hash = deviceId.hashCode.abs();
    final colors = [
      const Color(0xFF00BCD4),
      const Color(0xFFFF9800),
      const Color(0xFFE91E63),
      const Color(0xFF4CAF50),
      const Color(0xFF9C27B0),
      const Color(0xFF2196F3),
    ];
    return colors[hash % colors.length];
  }

  String _str(dynamic value) {
    if (value == null || value.toString().isEmpty) return '—';
    return value.toString();
  }

  @override
  Widget build(BuildContext context) {
    final deviceId = _str(widget.device['deviceId']);
    final rename = _str(widget.device['rename']);
    final deviceKey = _str(widget.device['device_key']);
    final linkCowSheepId = _str(widget.device['link_cowsheep_id']);
    final productKey = _str(widget.device['ProductKey']);
    
    // LOT数据
    final timeRaw = widget.deviceLot != null ? _str(widget.deviceLot!['time']) : '—';

    // 构建显示名称
    String displayName = deviceId;
    if (rename != '—') displayName += '($rename)';

    return Scaffold(
      backgroundColor: const Color(0xFFF5F5F5),
      appBar: AppBar(
        title: const Text('设备详情'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: Column(
        children: [
          // 固定头部区域
          Container(
            margin: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: const Color(0xFF1976D2),
              borderRadius: BorderRadius.circular(16),
            ),
            child: Stack(
              children: [
                Padding(
                  padding: const EdgeInsets.all(16),
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      // 左侧图片上传区域
                      Container(
                        width: 80,
                        height: 80,
                        decoration: BoxDecoration(
                          color: Colors.white.withOpacity(0.2),
                          borderRadius: BorderRadius.circular(12),
                          border: Border.all(
                            color: Colors.white.withOpacity(0.4),
                            width: 2,
                          ),
                        ),
                        child: Column(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            Icon(
                              Icons.satellite_alt,
                              size: 32,
                              color: Colors.white.withOpacity(0.8),
                            ),
                            const SizedBox(height: 4),
                            Text(
                              '点击上传',
                              style: TextStyle(
                                fontSize: 10,
                                color: Colors.white.withOpacity(0.8),
                              ),
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(width: 16),
                      // 右侧信息区域
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              'id: $displayName',
                              style: const TextStyle(
                                fontSize: 18,
                                fontWeight: FontWeight.bold,
                                color: Colors.white,
                              ),
                            ),
                            const SizedBox(height: 8),
                            _buildInfoRowWhite('设备编码', deviceKey),
                            _buildInfoRowWhite(
                              '绑定牛羊',
                              linkCowSheepId == '—' ? '未绑定' : linkCowSheepId,
                              valueColor: linkCowSheepId == '—' 
                                  ? const Color(0xFF66BB6A) 
                                  : Colors.white,
                            ),
                            _buildInfoRowWhite('上次换电', '—'),
                            const SizedBox(height: 8),
                            Row(
                              mainAxisAlignment: MainAxisAlignment.end,
                              children: [
                                _buildSmallIcon(Icons.cloud_upload, () {}),
                                const SizedBox(width: 8),
                                _buildSmallIcon(Icons.location_on, () {}),
                                const SizedBox(width: 8),
                                _buildSmallIcon(Icons.map, () {}),
                              ],
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
                // 右上角操作按钮
                Positioned(
                  top: 8,
                  right: 8,
                  child: Row(
                    children: [
                      _buildActionButton(Icons.edit, Colors.amber, () {}),
                      const SizedBox(width: 8),
                      _buildActionButton(Icons.link, Colors.white, () {}),
                    ],
                  ),
                ),
              ],
            ),
          ),

          // 数据记录区域（可滚动 + 下拉刷新 + 上拉加载）
          Expanded(
            child: Container(
              margin: const EdgeInsets.symmetric(horizontal: 16),
              decoration: BoxDecoration(
                color: Colors.white,
                borderRadius: BorderRadius.circular(12),
              ),
              child: RefreshIndicator(
                onRefresh: () => _loadLogs(reset: true),
                child: _buildLogList(),
              ),
            ),
          ),
        ],
      ),
    );
  }

  /// 构建日志列表
  Widget _buildLogList() {
    if (_isLoadingLogs && _logs.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }
    
    if (_logs.isEmpty) {
      return const Center(child: Text('暂无数据记录'));
    }

    return NotificationListener<ScrollNotification>(
      onNotification: (scrollInfo) {
        // 滚动到底部时触发加载更多
        if (scrollInfo.metrics.pixels >= scrollInfo.metrics.maxScrollExtent - 100) {
          if (_hasMore && !_isLoadingMore && !_isLoadingLogs) {
            _loadLogs(reset: false);
          }
        }
        return false;
      },
      child: ListView.builder(
        itemCount: _logs.length + (_hasMore ? 1 : 0),
        itemBuilder: (context, index) {
          // 加载更多指示器
          if (index >= _logs.length) {
            return _buildLoadingMoreIndicator();
          }
          
          final log = _logs[index];
          final isEven = index % 2 == 0;
          return _buildLogCard(log, isEven);
        },
      ),
    );
  }

  /// 加载更多指示器
  Widget _buildLoadingMoreIndicator() {
    return Container(
      padding: const EdgeInsets.all(16),
      child: Center(
        child: _isLoadingMore
            ? const SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            : const Text('没有更多数据', style: TextStyle(color: Colors.grey, fontSize: 13)),
      ),
    );
  }

  /// 白色文字信息行
  Widget _buildInfoRowWhite(String label, String value, {Color? valueColor}) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 4),
      child: Row(
        children: [
          Text(
            '$label  ',
            style: TextStyle(
              fontSize: 14,
              color: Colors.white.withOpacity(0.8),
            ),
          ),
          Text(
            value,
            style: TextStyle(
              fontSize: 14,
              color: valueColor ?? Colors.white,
              fontWeight: FontWeight.w500,
            ),
          ),
        ],
      ),
    );
  }

  /// 操作按钮
  Widget _buildActionButton(IconData icon, Color color, VoidCallback onTap) {
    return InkWell(
      onTap: onTap,
      child: Container(
        width: 36,
        height: 36,
        decoration: BoxDecoration(
          color: color.withOpacity(0.2),
          shape: BoxShape.circle,
        ),
        child: Icon(icon, size: 20, color: color),
      ),
    );
  }

  /// 底部小图标
  Widget _buildSmallIcon(IconData icon, VoidCallback onTap) {
    return InkWell(
      onTap: onTap,
      child: Container(
        width: 32,
        height: 32,
        decoration: BoxDecoration(
          color: Colors.white.withOpacity(0.2),
          shape: BoxShape.circle,
        ),
        child: Icon(icon, size: 18, color: Colors.white),
      ),
    );
  }

  /// 根据type获取标签文字和颜色
  ({String label, Color color}) _getTypeInfo(dynamic typeVal) {
    final type = typeVal is int ? typeVal : int.tryParse(typeVal?.toString() ?? '') ?? 0;
    switch (type) {
      case 1:
        return (label: '定位', color: const Color(0xFF1976D2));
      case 2:
        return (label: '对时', color: const Color(0xFF4CAF50));
      case 5:
        return (label: '跟踪', color: const Color(0xFFFF9800));
      default:
        return (label: type.toString(), color: Colors.grey);
    }
  }

  /// 日志卡片
  Widget _buildLogCard(Map<String, dynamic> log, bool isEven) {
    final time = _str(log['time']);
    final logDeviceId = _str(log['deviceId']);
    final lorastr = _str(log['lorastr']);
    final upDateDevice = _str(log['upDateDevice']);
    final typeInfo = _getTypeInfo(log['type']);
    
    // 直接取 rssi 和 snr 字段
    final rssiVal = log['rssi'];
    final snrVal = log['snr'];
    final rssi = rssiVal != null ? '${rssiVal}dBm' : '—';
    final snr = snrVal != null ? snrVal.toString() : '—';

    return Container(
      color: isEven ? const Color(0xFFE8F5E9) : const Color(0xFFE3F2FD),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 第一行：时间 | upDateDevice
          Row(
            children: [
              Text(
                time,
                style: const TextStyle(
                  fontSize: 14,
                  color: Colors.black87,
                ),
              ),
              const Spacer(),
              Text(
                '| $upDateDevice',
                style: TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.bold,
                  color: _getDeviceIdColor(upDateDevice),
                ),
              ),
              
            ],
          ),
          const SizedBox(height: 8),
          // 第二行：类型标签 + lorastr + 箭头
          Row(
            children: [
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                decoration: BoxDecoration(
                  color: typeInfo.color,
                  borderRadius: BorderRadius.circular(4),
                ),
                child: Text(
                  typeInfo.label,
                  style: const TextStyle(
                    fontSize: 12,
                    color: Colors.white,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  lorastr,
                  style: const TextStyle(
                    fontSize: 14,
                    color: Colors.black87,
                  ),
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
              const SizedBox(width: 8),
              const Icon(
                Icons.arrow_forward_ios,
                size: 14,
                color: Colors.grey,
              ),
            ],
          ),
          const SizedBox(height: 8),
          // 第三行：RSSI和SNR
          Row(
            children: [
              Text(
                'RSSI: $rssi',
                style: const TextStyle(
                  fontSize: 14,
                  color: Colors.black87,
                ),
              ),
              const Spacer(),
              Text(
                'SNR: $snr',
                style: const TextStyle(
                  fontSize: 14,
                  color: Colors.black87,
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}