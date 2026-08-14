import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';

/// 设备DTU指令页面
/// 当设备非断网状态时，通过此页面下发指令
class DeviceDtuCommandPage extends StatefulWidget {
  final String deviceId;
  final String deviceName;
  final String deviceKey;

  const DeviceDtuCommandPage({
    super.key,
    required this.deviceId,
    required this.deviceName,
    required this.deviceKey,
  });

  @override
  State<DeviceDtuCommandPage> createState() => _DeviceDtuCommandPageState();
}

class _DeviceDtuCommandPageState extends State<DeviceDtuCommandPage> {
  final TextEditingController _commandController = TextEditingController();
  String _selectedDevice = '';
  String _selectedDeviceId = '';
  Map<String, dynamic> _selectedTargetDevice = {};
  String _relayMode = '自动';
  String _selectedRelayDeviceId = '';
  bool _isSending = false;

  // 设备列表
  List<Map<String, dynamic>> _targetDevices = []; // ProductKey为空的设备
  List<Map<String, dynamic>> _relayDevices = []; // 有ProductKey的设备

  @override
  void initState() {
    super.initState();
    _loadDevices();
  }

  /// 加载设备列表
  Future<void> _loadDevices() async {
    try {
      final allDevices = await DBHelper().getDevices();
      final targetDevices = <Map<String, dynamic>>[];
      final relayDevices = <Map<String, dynamic>>[];

      for (final device in allDevices) {
        final productKey = device['ProductKey']?.toString() ?? '';
        final deviceId = device['deviceId']?.toString() ?? '';
        if (deviceId.isEmpty) continue;

        if (productKey.isEmpty) {
          targetDevices.add(device);
        } else {
          relayDevices.add(device);
        }
      }

      // 默认选中当前设备
      final currentDevice = allDevices.firstWhere(
        (d) => d['deviceId']?.toString() == widget.deviceId,
        orElse: () => <String, dynamic>{},
      );

      setState(() {
        _targetDevices = targetDevices;
        _relayDevices = relayDevices;
        _selectedDeviceId = widget.deviceId;
        _selectedTargetDevice = currentDevice;
        _selectedDevice = _formatDeviceName(currentDevice);
      });
    } catch (e) {
      debugPrint('[DTU指令] 加载设备列表失败: $e');
    }
  }

  /// 格式化设备名称
  String _formatDeviceName(Map<String, dynamic> device) {
    if (device.isEmpty) return '—';
    final deviceId = device['deviceId']?.toString() ?? '';
    final rename = device['rename']?.toString() ?? '';
    final productKey = device['ProductKey']?.toString() ?? '';
    String name = deviceId;
    if (rename.isNotEmpty) name += '($rename)';
    name += productKey.isEmpty ? ' [无密钥]' : ' [$productKey]';
    return name;
  }

  @override
  void dispose() {
    _commandController.dispose();
    super.dispose();
  }

  /// 自动模式：获取RSSI最优的中继转发设备
  Future<Map<String, dynamic>?> _getBestRelayDevice(String targetDeviceId) async {
    try {
      final resp = await http.post(
        Uri.parse('https://gpsmoveinfo.cn/fc/device'),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getDeviceBestRssibyId',
          'info': {'limit': 3, 'deviceId': targetDeviceId},
        }),
      );

      debugPrint('[DTU] getDeviceBestRssibyId 响应: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        if (json['status'] == 'success') {
          final data = json['data'];
          if (data is List && data.isNotEmpty) {
            // 解析OTS格式：从attributes中提取rssi和upDateDevice
            Map<String, dynamic>? bestRelayDevice;
            double bestAbsRssi = double.infinity;

            for (final item in data) {
              final attrs = item['attributes'] as List<dynamic>? ?? [];
              String? rssiStr;
              String? upDateDeviceStr;
              for (final attr in attrs) {
                final name = attr['columnName']?.toString() ?? '';
                final value = attr['columnValue'];
                if (name == 'rssi') rssiStr = value?.toString();
                if (name == 'upDateDevice') upDateDeviceStr = value?.toString();
              }

              final rssi = double.tryParse(rssiStr ?? '') ?? double.infinity;
              final absRssi = rssi.abs();
              if (absRssi < bestAbsRssi && upDateDeviceStr != null && upDateDeviceStr.isNotEmpty) {
                bestAbsRssi = absRssi;
                // 用upDateDevice匹配本地中继设备
                final matched = _relayDevices.firstWhere(
                  (d) => d['deviceId']?.toString() == upDateDeviceStr,
                  orElse: () => <String, dynamic>{},
                );
                bestRelayDevice = matched.isNotEmpty ? matched : null;
                debugPrint('[DTU] 候选中继: $upDateDeviceStr, rssi=$rssi, abs=$absRssi, 匹配=${matched.isNotEmpty}');
              }
            }
            return bestRelayDevice;
          }
        }
      }
    } catch (e) {
      debugPrint('[DTU] 获取最优中继设备失败: $e');
    }
    return null;
  }

  /// 发送指令（通过 sendtodtucmd 接口）
  Future<void> _sendCommand() async {
    var command = _commandController.text.trim();
    if (command.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('请输入指令内容')),
      );
      return;
    }

    if (_selectedDeviceId.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('请选择目标设备')),
      );
      return;
    }

    // 如果是JSON格式，自动添加deviceId
    if (command.startsWith('{') && command.endsWith('}')) {
      try {
        final json = jsonDecode(command) as Map<String, dynamic>;
        json['deviceId'] = _selectedDeviceId;
        command = jsonEncode(json);
      } catch (_) {}
    }

    setState(() {
      _isSending = true;
    });

    try {
      String deviceName = '';
      String productKey = '';

      if (_relayMode == '自动') {
        // 自动模式：通过接口获取RSSI最优的中继设备
        final bestRelay = await _getBestRelayDevice(_selectedDeviceId);
        if (bestRelay == null) {
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('未找到可用的中继转发设备')),
            );
          }
          return;
        }
        deviceName = bestRelay['DeviceName']?.toString() ?? '';
        productKey = bestRelay['ProductKey']?.toString() ?? '';
        debugPrint('[DTU] 自动模式选中继: deviceName=$deviceName, productKey=$productKey');
      } else {
        // 手动模式：从选中的中继设备获取
        if (_selectedRelayDeviceId.isNotEmpty) {
          final relayDevice = _relayDevices.firstWhere(
            (d) => d['deviceId']?.toString() == _selectedRelayDeviceId,
            orElse: () => <String, dynamic>{},
          );
          deviceName = relayDevice['DeviceName']?.toString() ?? '';
          productKey = relayDevice['ProductKey']?.toString() ?? '';
        }
      }

      final body = <String, dynamic>{
        'action': 'com',
        'deviceName': deviceName,
        'productKey': productKey,
        'msg': command,
        'timestamp': DateTime.now().millisecondsSinceEpoch,
      };

      debugPrint('[DTU] 请求body: ${jsonEncode(body)}');

      final resp = await http.post(
        Uri.parse('https://gpsmoveinfo.cn/fc/sendtodtucmd'),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode(body),
      );

      debugPrint('[DTU] 响应状态: ${resp.statusCode}');
      debugPrint('[DTU] 响应body: ${resp.body}');

      if (resp.statusCode == 200) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('指令发送成功')),
          );
        }
      } else {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('发送失败，请检查网络')),
          );
        }
      }
    } catch (e) {
      debugPrint('发送DTU指令失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('发送指令失败')),
        );
      }
    } finally {
      if (mounted) {
        setState(() {
          _isSending = false;
        });
      }
    }
  }

  /// 快捷指令按钮
  Widget _buildQuickCommandButton({
    required IconData icon,
    required String label,
    required Color iconColor,
    required VoidCallback onTap,
  }) {
    return Expanded(
      child: InkWell(
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 10),
          decoration: BoxDecoration(
            color: Colors.grey[100],
            borderRadius: BorderRadius.circular(8),
            border: Border.all(color: Colors.grey[300]!),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon, size: 14, color: iconColor),
              const SizedBox(width: 4),
              Text(
                label,
                style: const TextStyle(fontSize: 12),
              ),
            ],
          ),
        ),
      ),
    );
  }

  /// 显示目标设备选择器（ProductKey为空的设备）
  void _showTargetDevicePicker() {
    showModalBottomSheet(
      context: context,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(16)),
      ),
      builder: (context) => Container(
        padding: const EdgeInsets.symmetric(vertical: 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 40,
              height: 4,
              decoration: BoxDecoration(
                color: Colors.grey[300],
                borderRadius: BorderRadius.circular(2),
              ),
            ),
            const SizedBox(height: 12),
            const Text(
              '选择目标设备',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
            ),
            const SizedBox(height: 8),
            Text(
              'ProductKey为空的设备',
              style: TextStyle(fontSize: 12, color: Colors.grey[600]),
            ),
            const SizedBox(height: 12),
            Flexible(
              child: ListView.builder(
                shrinkWrap: true,
                itemCount: _targetDevices.length,
                itemBuilder: (context, index) {
                  final device = _targetDevices[index];
                  final deviceId = device['deviceId']?.toString() ?? '';
                  final isSelected = deviceId == _selectedDeviceId;
                  return InkWell(
                    onTap: () {
                      setState(() {
                        _selectedDeviceId = deviceId;
                        _selectedTargetDevice = device;
                        _selectedDevice = _formatDeviceName(device);
                      });
                      Navigator.pop(context);
                    },
                    child: Container(
                      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
                      decoration: BoxDecoration(
                        color: isSelected ? Colors.blue[50] : Colors.transparent,
                      ),
                      child: Row(
                        children: [
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  deviceId,
                                  style: TextStyle(
                                    fontSize: 14,
                                    fontWeight: isSelected ? FontWeight.w600 : FontWeight.normal,
                                    color: isSelected ? Colors.blue : Colors.black87,
                                  ),
                                ),
                                if (device['rename']?.toString().isNotEmpty == true)
                                  Text(
                                    device['rename']!.toString(),
                                    style: TextStyle(fontSize: 12, color: Colors.grey[600]),
                                  ),
                              ],
                            ),
                          ),
                          if (isSelected)
                            const Icon(Icons.check_circle, color: Colors.blue, size: 20),
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
    );
  }

  /// 显示中继转发设备选择器（有ProductKey的设备）
  void _showRelayDevicePicker() {
    showModalBottomSheet(
      context: context,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(16)),
      ),
      builder: (context) => Container(
        padding: const EdgeInsets.symmetric(vertical: 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 40,
              height: 4,
              decoration: BoxDecoration(
                color: Colors.grey[300],
                borderRadius: BorderRadius.circular(2),
              ),
            ),
            const SizedBox(height: 12),
            const Text(
              '选择中继转发设备',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
            ),
            const SizedBox(height: 8),
            Text(
              '有ProductKey的设备',
              style: TextStyle(fontSize: 12, color: Colors.grey[600]),
            ),
            const SizedBox(height: 12),
            Flexible(
              child: ListView(
                shrinkWrap: true,
                children: [
                  // 自动选项
                  InkWell(
                    onTap: () {
                      setState(() {
                        _relayMode = '自动';
                        _selectedRelayDeviceId = '';
                      });
                      Navigator.pop(context);
                    },
                    child: Container(
                      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
                      decoration: BoxDecoration(
                        color: _relayMode == '自动' ? Colors.orange[50] : Colors.transparent,
                      ),
                      child: Row(
                        children: [
                          const Icon(Icons.auto_awesome, color: Colors.orange, size: 20),
                          const SizedBox(width: 12),
                          Expanded(
                            child: Text(
                              '自动',
                              style: TextStyle(
                                fontSize: 14,
                                fontWeight: _relayMode == '自动' ? FontWeight.w600 : FontWeight.normal,
                                color: _relayMode == '自动' ? Colors.orange : Colors.black87,
                              ),
                            ),
                          ),
                          if (_relayMode == '自动')
                            const Icon(Icons.check_circle, color: Colors.orange, size: 20),
                        ],
                      ),
                    ),
                  ),
                  const Divider(height: 1),
                  // 有ProductKey的设备列表
                  ..._relayDevices.map((device) {
                    final deviceId = device['deviceId']?.toString() ?? '';
                    final productKey = device['ProductKey']?.toString() ?? '';
                    final isSelected = deviceId == _selectedRelayDeviceId;
                    return InkWell(
                      onTap: () {
                        setState(() {
                          _relayMode = '$deviceId [$productKey]';
                          _selectedRelayDeviceId = deviceId;
                        });
                        Navigator.pop(context);
                      },
                      child: Container(
                        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
                        decoration: BoxDecoration(
                          color: isSelected ? Colors.orange[50] : Colors.transparent,
                        ),
                        child: Row(
                          children: [
                            const Icon(Icons.devices, color: Colors.blue, size: 20),
                            const SizedBox(width: 12),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text(
                                    deviceId,
                                    style: TextStyle(
                                      fontSize: 14,
                                      fontWeight: isSelected ? FontWeight.w600 : FontWeight.normal,
                                      color: isSelected ? Colors.orange : Colors.black87,
                                    ),
                                  ),
                                  Text(
                                    productKey,
                                    style: TextStyle(fontSize: 12, color: Colors.grey[600]),
                                  ),
                                ],
                              ),
                            ),
                            if (isSelected)
                              const Icon(Icons.check_circle, color: Colors.orange, size: 20),
                          ],
                        ),
                      ),
                    );
                  }),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFFF5F5F5),
      appBar: AppBar(
        title: const Text('设备DTU指令'),
        centerTitle: true,
        backgroundColor: Colors.white,
        foregroundColor: Colors.black,
        elevation: 0,
      ),
      body: GestureDetector(
        onTap: () => FocusScope.of(context).unfocus(),
        behavior: HitTestBehavior.translucent,
        child: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 目标设备（可下拉选择ProductKey为空的设备）
            _buildSectionCard(
              icon: Icons.devices,
              iconColor: const Color(0xFF2196F3),
              title: '目标设备',
              child: InkWell(
                onTap: _targetDevices.isEmpty ? null : _showTargetDevicePicker,
                child: Container(
                  width: double.infinity,
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 14),
                  decoration: BoxDecoration(
                    color: Colors.white,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: Colors.grey[300]!),
                  ),
                  child: Row(
                    children: [
                      Expanded(
                        child: Text(
                          _selectedDevice,
                          style: TextStyle(
                            fontSize: 15,
                            color: _targetDevices.isEmpty ? Colors.grey : Colors.black87,
                          ),
                        ),
                      ),
                      Icon(
                        Icons.arrow_drop_down,
                        color: _targetDevices.isEmpty ? Colors.grey[300] : Colors.grey,
                      ),
                    ],
                  ),
                ),
              ),
            ),

            const SizedBox(height: 8),

            // 中继转发设备（默认自动，可选有ProductKey的设备）
            _buildSectionCard(
              icon: Icons.swap_horiz,
              iconColor: const Color(0xFF2196F3),
              title: '中继转发设备',
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  InkWell(
                    onTap: _relayDevices.isEmpty ? null : _showRelayDevicePicker,
                    child: Container(
                      width: double.infinity,
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 14),
                      decoration: BoxDecoration(
                        color: _relayMode == '自动' ? const Color(0xFFFFF8E1) : Colors.white,
                        borderRadius: BorderRadius.circular(8),
                        border: Border.all(color: const Color(0xFFFFB74D)),
                      ),
                      child: Row(
                        children: [
                          Expanded(
                            child: Text(
                              _relayMode,
                              style: const TextStyle(fontSize: 15),
                            ),
                          ),
                          Icon(
                            Icons.arrow_drop_down,
                            color: _relayDevices.isEmpty ? Colors.grey[300] : Colors.grey,
                          ),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(height: 6),
                  Text(
                    _relayMode == '自动'
                        ? '自动模式下将根据RSSI信号自动选择最佳转发设备'
                        : '将指令通过选中设备转发到目标设备',
                    style: TextStyle(fontSize: 11, color: Colors.grey[600]),
                  ),
                ],
              ),
            ),

            const SizedBox(height: 8),

            // 指令内容
            _buildSectionCard(
              icon: Icons.bolt,
              iconColor: const Color(0xFFFF9800),
              title: '指令内容',
              child: TextField(
                controller: _commandController,
                decoration: const InputDecoration(
                  hintText: '输入指令JSON或文本...',
                  border: OutlineInputBorder(),
                  contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                ),
                maxLines: 2,
                style: const TextStyle(fontFamily: 'monospace', fontSize: 14),
              ),
            ),

            const SizedBox(height: 12),

            // 快捷指令
            const Text(
              '快捷指令',
              style: TextStyle(fontSize: 14, color: Colors.grey),
            ),
            const SizedBox(height: 6),

            // 第一行：上报GPS、上报模式、位置跟踪
            Row(
              children: [
                _buildQuickCommandButton(
                  icon: Icons.location_on,
                  label: '上报GPS',
                  iconColor: Colors.red,
                  onTap: () {
                    _commandController.text = '{"cmd":"gps","value":"1"}';
                  },
                ),
                const SizedBox(width: 8),
                _buildQuickCommandButton(
                  icon: Icons.upload_file,
                  label: '上报模式',
                  iconColor: Colors.blue,
                  onTap: () {
                    _commandController.text = '{"cmd":"mode","value":"1"}';
                  },
                ),
                const SizedBox(width: 8),
                _buildQuickCommandButton(
                  icon: Icons.location_searching,
                  label: '位置跟踪',
                  iconColor: Colors.red,
                  onTap: () {
                    _commandController.text = '{"cmd":"follow","value":"30,5"}';
                  },
                ),
              ],
            ),
            const SizedBox(height: 8),
            // 第二行：发射功率、最低电量、配置下发
            Row(
              children: [
                _buildQuickCommandButton(
                  icon: Icons.cell_tower,
                  label: '发射功率',
                  iconColor: Colors.purple,
                  onTap: () {
                    _commandController.text = '{"cmd":"power","value":"5"}';
                  },
                ),
                const SizedBox(width: 8),
                _buildQuickCommandButton(
                  icon: Icons.battery_alert,
                  label: '最低电量',
                  iconColor: Colors.green,
                  onTap: () {
                    _commandController.text = '{"cmd":"minBattery","value":"20"}';
                  },
                ),
                const SizedBox(width: 8),
                _buildQuickCommandButton(
                  icon: Icons.settings,
                  label: '配置下发',
                  iconColor: Colors.green,
                  onTap: () {
                    _commandController.text = '{"cmd":"config","value":"10,0-24,12-6"}';
                  },
                ),
              ],
            ),

            const SizedBox(height: 16),

            // 发送指令按钮
            SizedBox(
              width: double.infinity,
              height: 48,
              child: ElevatedButton(
                onPressed: _isSending ? null : _sendCommand,
                style: ElevatedButton.styleFrom(
                  backgroundColor: const Color(0xFF2ECC71),
                  foregroundColor: Colors.white,
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(8),
                  ),
                  elevation: 2,
                ),
                child: _isSending
                    ? const SizedBox(
                        width: 20,
                        height: 20,
                        child: CircularProgressIndicator(
                          strokeWidth: 2,
                          color: Colors.white,
                        ),
                      )
                    : const Text(
                        '发送指令',
                        style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                      ),
              ),
            ),
          ],
        ),
      ),
      ),
    );
  }

  /// 构建分区卡片（带左侧图标标题栏）
  Widget _buildSectionCard({
    required IconData icon,
    required Color iconColor,
    required String title,
    required Widget child,
  }) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [
          BoxShadow(
            color: Colors.grey.withOpacity(0.08),
            blurRadius: 4,
            offset: const Offset(0, 1),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 3,
                height: 16,
                color: iconColor,
              ),
              const SizedBox(width: 6),
              Icon(icon, size: 18, color: iconColor),
              const SizedBox(width: 6),
              Text(
                title,
                style: const TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          child,
        ],
      ),
    );
  }
}
