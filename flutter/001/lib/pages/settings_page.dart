import 'package:flutter/material.dart';
import '../utils/db_helper.dart';

/// 设置页面
class SettingsPage extends StatefulWidget {
  const SettingsPage({super.key});

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  final DBHelper _dbHelper = DBHelper();
  
  // 是否单行显示记录（全局缓存变量）
  bool _singleLineDisplay = false;
  bool _isLoading = true;

  @override
  void initState() {
    super.initState();
    _loadSettings();
  }

  /// 加载设置
  Future<void> _loadSettings() async {
    try {
      final singleLine = await _dbHelper.getBoolSetting(
        'single_line_display',
        defaultValue: false,
      );
      
      setState(() {
        _singleLineDisplay = singleLine;
        _isLoading = false;
      });
    } catch (e) {
      debugPrint('[设置] 加载设置失败: $e');
      setState(() {
        _isLoading = false;
      });
    }
  }

  /// 保存设置
  Future<void> _saveSingleLineDisplay(bool value) async {
    try {
      await _dbHelper.saveSetting(
        'single_line_display',
        value.toString(),
      );
      
      setState(() {
        _singleLineDisplay = value;
      });
      
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('已${value ? '开启' : '关闭'}单行显示'),
            duration: const Duration(seconds: 1),
          ),
        );
      }
    } catch (e) {
      debugPrint('[设置] 保存设置失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('保存设置失败'),
            backgroundColor: Colors.red,
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('设置'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                // 显示设置分组
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Icon(Icons.display_settings, color: Colors.blue.shade700),
                            const SizedBox(width: 8),
                            const Text(
                              '显示设置',
                              style: TextStyle(
                                fontSize: 18,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ],
                        ),
                        const Divider(height: 24),
                        
                        // 单行显示记录开关
                        SwitchListTile(
                          title: const Text(
                            '单行显示记录',
                            style: TextStyle(fontSize: 16),
                          ),
                          subtitle: const Text(
                            '开启后，日志记录将使用单行紧凑显示',
                            style: TextStyle(fontSize: 12, color: Colors.grey),
                          ),
                          value: _singleLineDisplay,
                          onChanged: (value) {
                            _saveSingleLineDisplay(value);
                          },
                          activeColor: Colors.blue,
                        ),
                      ],
                    ),
                  ),
                ),
                
                const SizedBox(height: 16),
                
                // 说明卡片
                Card(
                  color: Colors.blue.shade50,
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Icon(Icons.info_outline, color: Colors.blue.shade700),
                            const SizedBox(width: 8),
                            Text(
                              '说明',
                              style: TextStyle(
                                fontSize: 16,
                                fontWeight: FontWeight.bold,
                                color: Colors.blue.shade700,
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 12),
                        const Text(
                          '• 此设置为全局设置，会影响所有页面的记录显示样式\n'
                          '• 设置会自动保存，下次打开APP时仍然有效\n'
                          '• 单行显示可以让列表更加紧凑，显示更多内容',
                          style: TextStyle(fontSize: 13, color: Colors.black87),
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
    );
  }
}
