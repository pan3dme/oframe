import 'package:flutter/material.dart';

/// 设备详情页面
class DeviceDetailPage extends StatelessWidget {
  final Map<String, dynamic> device;
  final Map<String, dynamic>? deviceLot;

  const DeviceDetailPage({
    super.key,
    required this.device,
    this.deviceLot,
  });

  /// 从记录中安全取值，空值显示占位文字
  String _str(dynamic value) {
    if (value == null || value.toString().isEmpty) return '—';
    return value.toString();
  }

  @override
  Widget build(BuildContext context) {
    final deviceId = _str(device['deviceId']);
    final deviceKey = _str(device['device_key']);
    final rename = _str(device['rename']);
    final linkCowSheepId = _str(device['link_cowsheep_id']);
    final picurl = _str(device['picurl']);
    final hasImage = picurl != '—' && picurl.isNotEmpty;
    
    // 获取LOT数据
    final timeRaw = deviceLot != null ? _str(deviceLot!['time']) : '—';
    final gpsRaw = deviceLot != null ? _str(deviceLot!['gps']) : '—';
    final lorastr = deviceLot != null ? _str(deviceLot!['lorastr']) : '—';
    final upDateDevice = deviceLot != null ? _str(deviceLot!['upDateDevice']) : '—';

    return Scaffold(
      appBar: AppBar(
        title: Text('设备详情 - $deviceId'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 设备图片
            Card(
              elevation: 2,
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(12),
              ),
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  children: [
                    ClipRRect(
                      borderRadius: BorderRadius.circular(10),
                      child: hasImage
                          ? Image.network(
                              picurl,
                              width: 200,
                              height: 200,
                              fit: BoxFit.cover,
                              errorBuilder: (context, error, stackTrace) {
                                return _buildImagePlaceholder();
                              },
                            )
                          : _buildImagePlaceholder(),
                    ),
                    const SizedBox(height: 12),
                    Text(
                      rename != '—' ? rename : deviceId,
                      style: const TextStyle(
                        fontSize: 20,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ],
                ),
              ),
            ),
            
            const SizedBox(height: 16),
            
            // 基本信息
            Card(
              elevation: 2,
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(12),
              ),
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      '基本信息',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const Divider(height: 24),
                    _infoRow(Icons.devices, '设备ID', deviceId),
                    _infoRow(Icons.key, '设备密钥', deviceKey),
                    _infoRow(Icons.label, '重命名', rename),
                    _infoRow(
                      Icons.pets,
                      '绑定牛羊',
                      linkCowSheepId,
                      valueColor: linkCowSheepId != '—' ? Colors.green : null,
                    ),
                  ],
                ),
              ),
            ),
            
            const SizedBox(height: 16),
            
            // LOT数据（如果有）
            if (deviceLot != null)
              Card(
                elevation: 2,
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        '位置信息',
                        style: TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const Divider(height: 24),
                      _infoRow(Icons.access_time, '更新时间', timeRaw),
                      _infoRow(Icons.location_on, 'GPS坐标', gpsRaw),
                      _infoRow(Icons.signal_cellular_alt, 'LoRa数据', lorastr),
                      _infoRow(Icons.router, '更新设备', upDateDevice),
                    ],
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }

  /// 信息行组件
  Widget _infoRow(
    IconData icon,
    String label,
    String value, {
    Color? valueColor,
  }) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 20, color: Colors.grey[600]),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label,
                  style: TextStyle(
                    fontSize: 12,
                    color: Colors.grey[600],
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  value,
                  style: TextStyle(
                    fontSize: 15,
                    color: valueColor ?? Colors.black87,
                    fontWeight: valueColor != null ? FontWeight.w600 : FontWeight.normal,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  /// 图片占位符
  Widget _buildImagePlaceholder() {
    return Container(
      width: 200,
      height: 200,
      decoration: BoxDecoration(
        color: Colors.grey.shade200,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Icon(Icons.image_not_supported, size: 64, color: Colors.grey[400]),
    );
  }
}
