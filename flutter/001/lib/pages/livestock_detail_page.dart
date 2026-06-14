import 'package:flutter/material.dart';

/// 牛羊详情页面
class LivestockDetailPage extends StatelessWidget {
  final Map<String, dynamic> livestock;

  const LivestockDetailPage({
    super.key,
    required this.livestock,
  });

  /// 从记录中安全取值，空值显示占位文字
  String _str(dynamic value) {
    if (value == null || value.toString().isEmpty) return '—';
    return value.toString();
  }

  @override
  Widget build(BuildContext context) {
    final cowsheepId = _str(livestock['cowsheep_id']);
    final birthday = _str(livestock['birthday']);
    final gender = livestock['gender'] == true ? '公' : (livestock['gender'] == false ? '母' : '—');
    final avatar = _str(livestock['avatar']);
    final rename = _str(livestock['rename']);
    final hasImage = avatar != '—' && avatar.isNotEmpty;

    return Scaffold(
      appBar: AppBar(
        title: Text('牛羊详情 - $cowsheepId'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 牛羊图片
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
                              avatar,
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
                      rename != '—' ? rename : cowsheepId,
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
                    _infoRow(Icons.tag, '牛羊ID', cowsheepId),
                    _infoRow(Icons.edit_note, '重命名', rename),
                    _infoRow(
                      Icons.cake,
                      '生日',
                      birthday,
                    ),
                    _infoRow(
                      gender == '公' ? Icons.male : (gender == '母' ? Icons.female : Icons.help_outline),
                      '性别',
                      gender,
                      valueColor: gender != '—' ? (gender == '公' ? Colors.blue : Colors.pink) : null,
                    ),
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
