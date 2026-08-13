import 'package:flutter/material.dart';
import '../utils/db_helper.dart';
import '../utils/map_tile_config.dart';

/// 设置页面
class SettingsPage extends StatefulWidget {
  const SettingsPage({super.key});

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  final DBHelper _dbHelper = DBHelper();

  bool _isAdmin = false; // 是否为管理员模式
  bool _bluetoothSoundEnabled = false; // 是否开启蓝牙接收声音
  final TextEditingController _tokenController = TextEditingController();
  bool _isLoading = true;

  @override
  void initState() {
    super.initState();
    _loadSettings();
  }

  /// 加载设置
  Future<void> _loadSettings() async {
    try {
      final isAdmin = await _dbHelper.getBoolSetting(
        'is_admin_mode',
        defaultValue: false,
      );

      final bluetoothSound = await _dbHelper.getBoolSetting(
        'bluetooth_sound_enabled',
        defaultValue: false,
      );

      final savedToken = await _dbHelper.getSetting('tianditu_token');

      setState(() {
        _isAdmin = isAdmin;
        _bluetoothSoundEnabled = bluetoothSound;
        _tokenController.text = savedToken ?? MapTileConfig.tiandituToken;
        _isLoading = false;
      });
    } catch (e) {
      debugPrint('[设置] 加载设置失败: $e');
      setState(() {
        _isLoading = false;
      });
    }
  }

  /// 保存管理员模式设置
  Future<void> _saveAdminMode(bool value) async {
    try {
      await _dbHelper.saveSetting('is_admin_mode', value.toString());

      setState(() {
        _isAdmin = value;
      });

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('已${value ? '开启' : '关闭'}管理员模式'),
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

  /// 保存天地图 token
  Future<void> _saveTiandituToken(String token) async {
    if (token.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Token 不能为空'), backgroundColor: Colors.red),
      );
      return;
    }
    try {
      await _dbHelper.saveSetting('tianditu_token', token);
      MapTileConfig.tiandituToken = token;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('天地图 Token 已保存，重新打开地图页面生效'),
          backgroundColor: Colors.green,
          duration: Duration(seconds: 2),
        ),
      );
    } catch (e) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('保存失败: $e'), backgroundColor: Colors.red),
      );
    }
  }

  /// 保存蓝牙声音设置
  Future<void> _saveBluetoothSoundEnabled(bool value) async {
    try {
      await _dbHelper.saveSetting(
        'bluetooth_sound_enabled',
        value.toString(),
      );

      setState(() {
        _bluetoothSoundEnabled = value;
      });

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('已${value ? '开启' : '关闭'}蓝牙接收声音'),
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
                // 权限设置分组
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Icon(Icons.admin_panel_settings,
                                color: Colors.orange.shade700),
                            const SizedBox(width: 8),
                            const Text(
                              '权限设置',
                              style: TextStyle(
                                fontSize: 18,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ],
                        ),
                        const Divider(height: 24),

                        // 管理员模式开关
                        SwitchListTile(
                          title: const Text(
                            '管理员模式',
                            style: TextStyle(fontSize: 16),
                          ),
                          subtitle: const Text(
                            '开启后可对道路、地名进行新增、编辑、删除操作',
                            style: TextStyle(fontSize: 12, color: Colors.grey),
                          ),
                          value: _isAdmin,
                          onChanged: (value) {
                            _saveAdminMode(value);
                          },
                          activeColor: Colors.orange,
                        ),
                      ],
                    ),
                  ),
                ),

                const SizedBox(height: 16),

                // 功能设置分组
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Icon(Icons.display_settings,
                                color: Colors.blue.shade700),
                            const SizedBox(width: 8),
                            const Text(
                              '功能设置',
                              style: TextStyle(
                                fontSize: 18,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ],
                        ),
                        const Divider(height: 24),

                        // 蓝牙接收声音开关
                        SwitchListTile(
                          title: const Text(
                            '蓝牙接收声音',
                            style: TextStyle(fontSize: 16),
                          ),
                          subtitle: const Text(
                            '开启后，接收到蓝牙数据时会播放提示音',
                            style: TextStyle(fontSize: 12, color: Colors.grey),
                          ),
                          value: _bluetoothSoundEnabled,
                          onChanged: (value) {
                            _saveBluetoothSoundEnabled(value);
                          },
                          activeColor: Colors.blue,
                        ),
                      ],
                    ),
                  ),
                ),

                const SizedBox(height: 16),

                // 地图瓦片设置分组
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Icon(Icons.map, color: Colors.green.shade700),
                            const SizedBox(width: 8),
                            const Text(
                              '地图设置',
                              style: TextStyle(
                                fontSize: 18,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ],
                        ),
                        const Divider(height: 24),
                        const Text(
                          '天地图 API Token',
                          style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
                        ),
                        const SizedBox(height: 4),
                        const Text(
                          '用于加载天地图瓦片，请在天地图官网申请 token',
                          style: TextStyle(fontSize: 11, color: Colors.grey),
                        ),
                        const SizedBox(height: 8),
                        TextField(
                          controller: _tokenController,
                          decoration: InputDecoration(
                            labelText: '天地图 Token',
                            border: const OutlineInputBorder(),
                            isDense: true,
                            suffixIcon: IconButton(
                              icon: const Icon(Icons.save, color: Colors.blue),
                              onPressed: () => _saveTiandituToken(_tokenController.text.trim()),
                            ),
                          ),
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
                            Icon(Icons.info_outline,
                                color: Colors.blue.shade700),
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
                          '• 管理员模式为全局设置，控制道路和地名管理页面的操作权限\n'
                          '• 非管理员模式下只能查看道路和地名列表，无法新增、编辑或删除\n'
                          '• 设置会自动保存，下次打开APP时仍然有效',
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
