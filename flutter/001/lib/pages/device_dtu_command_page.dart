import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';
import '../main.dart'; // 全局 globalWechatId

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
  String _currentConfigValue = ''; // 当前设备配置值（从缓存读取的lorastr配置段）

  // 配置弹框状态
  int _configReportInterval = 30; // 上报周期（分钟）
  int _configBootStart = 0;       // 开机开始时间
  int _configBootEnd = 24;        // 开机结束时间
  int _configGpsStart = 12;       // GPS开始时间
  int _configGpsEnd = 6;          // GPS结束时间
  int _configMainCycle = 60;      // 主周期（分钟）

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
      // 加载当前设备的配置
      _loadCurrentConfig(widget.deviceId);
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

  /// 从本地缓存加载设备当前配置值
  Future<void> _loadCurrentConfig(String deviceId) async {
    try {
      final config = await DBHelper().getDeviceConfig(deviceId);
      if (config == null) {
        debugPrint('[DTU] 本地无设备配置缓存，使用默认配置');
        return;
      }
      final lorastr = config['lorastr']?.toString() ?? '';
      if (lorastr.isEmpty) return;

      // lorastr格式: type|deviceId|30,12-6,12-4|... 或纯配置段 "10,0-24,12-6"
      String configStr = lorastr;
      if (lorastr.contains('|')) {
        final parts = lorastr.split('|');
        if (parts.length >= 3) {
          configStr = parts[2]; // 取第三段配置值
        }
      }
      setState(() {
        _currentConfigValue = configStr;
      });
      debugPrint('[DTU] 加载设备当前配置: deviceId=$deviceId, config=$configStr');
    } catch (e) {
      debugPrint('[DTU] 加载设备配置失败: $e');
    }
  }

  // --- 配置弹框辅助方法 ---

  static const String _timeDict = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';

  /// 将2字符编码转换为索引
  int _twoCharToIndex(String str) {
    if (str.length != 2) return -1;
    final h = _timeDict.indexOf(str[0]);
    final l = _timeDict.indexOf(str[1]);
    if (h == -1 || l == -1) return -1;
    return h * 62 + l;
  }

  /// 将索引转换为时间窗口 [startHour, endHour]
  List<int>? _indexToTimeWindow(int idx) {
    if (idx < 0) return null;
    int sum = 0;
    for (int s = 0; s <= 23; s++) {
      int valid = 23 - (s + 1) + 1;
      if (valid <= 0) continue;
      if (idx < sum + valid) {
        int off = idx - sum;
        int endHour = s + 1 + off;
        return [s, endHour];
      }
      sum += valid;
    }
    return null;
  }

  /// 将时间窗口转换为索引
  int _timeWindowToIndex(int startHour, int endHour) {
    int idx = 0;
    for (int s = 0; s < startHour; s++) {
      idx += 23 - (s + 1) + 1;
    }
    idx += endHour - (startHour + 1);
    return idx;
  }

  /// 将索引转换为2字符编码
  String _indexToTwoChar(int idx) {
    final h = idx ~/ 62;
    final l = idx % 62;
    return '${_timeDict[h]}${_timeDict[l]}';
  }

  /// 解析配置字符串到弹框状态
  void _parseConfigToDialog(String configStr) {
    final configs = configStr.split(',');
    if (configs.isEmpty) return;

    // 上报周期
    final interval = int.tryParse(configs[0]) ?? 30;

    // 开机时间
    int bootStart = 0, bootEnd = 24;
    if (configs.length >= 2) {
      final bootIndex = _twoCharToIndex(configs[1]);
      final tw = _indexToTimeWindow(bootIndex);
      if (tw != null) {
        bootStart = tw[0];
        bootEnd = tw[1];
      }
    }

    // GPS时间
    int gpsStart = 12, gpsEnd = 6;
    if (configs.length >= 3) {
      final gpsIndex = _twoCharToIndex(configs[2]);
      final tw = _indexToTimeWindow(gpsIndex);
      if (tw != null) {
        gpsStart = tw[0];
        gpsEnd = tw[1];
      }
    }

    // 主周期
    int mainCycle = 60;
    if (configs.length >= 4) {
      final bigCycle = int.tryParse(configs[3]);
      if (bigCycle != null) {
        mainCycle = bigCycle * 10;
      }
    }

    setState(() {
      _configReportInterval = interval;
      _configBootStart = bootStart;
      _configBootEnd = bootEnd;
      _configGpsStart = gpsStart;
      _configGpsEnd = gpsEnd;
      _configMainCycle = mainCycle;
    });
  }

  /// 从弹框状态生成配置字符串
  String _generateConfigString() {
    final bootIndex = _timeWindowToIndex(_configBootStart, _configBootEnd);
    final gpsIndex = _timeWindowToIndex(_configGpsStart, _configGpsEnd);
    final bootCode = _indexToTwoChar(bootIndex);
    final gpsCode = _indexToTwoChar(gpsIndex);
    final mainCycleUnit = _configMainCycle ~/ 10;
    return '${_configReportInterval},$bootCode,$gpsCode,$mainCycleUnit';
  }

  /// 显示配置下发弹框
  void _showConfigDialog() {
    // 用当前配置值初始化弹框
    if (_currentConfigValue.isNotEmpty) {
      _parseConfigToDialog(_currentConfigValue);
    } else {
      // 默认值
      setState(() {
        _configReportInterval = 30;
        _configBootStart = 0;
        _configBootEnd = 24;
        _configGpsStart = 12;
        _configGpsEnd = 6;
        _configMainCycle = 60;
      });
    }

    // 上报周期输入框controller（局部变量，确定时读取当前值）
    final reportIntervalController = TextEditingController(
      text: _configReportInterval.toString(),
    );

    showDialog(
      context: context,
      builder: (dialogContext) => StatefulBuilder(
        builder: (context, setDialogState) {
          return AlertDialog(
            title: const Row(
              children: [
                Icon(Icons.settings, size: 20),
                SizedBox(width: 8),
                Text('配置下发', style: TextStyle(fontSize: 16)),
              ],
            ),
            content: SingleChildScrollView(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  // ① 上报周期
                  const Text(
                    '① 上报周期（分钟）',
                    style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
                  ),
                  const SizedBox(height: 4),
                  Row(
                    children: [
                      Expanded(
                        child: TextField(
                          keyboardType: TextInputType.number,
                          decoration: const InputDecoration(
                            hintText: '5-60',
                            border: OutlineInputBorder(),
                            contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                          ),
                          controller: reportIntervalController,
                          onSubmitted: (v) {
                            final val = int.tryParse(v) ?? _configReportInterval;
                            setDialogState(() {
                              _configReportInterval = val.clamp(5, 60);
                            });
                          },
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),

                  // ② 开机时间
                  const Text(
                    '② 开机时间（小时）',
                    style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
                  ),
                  const SizedBox(height: 4),
                  Row(
                    children: [
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            const Text('开始时间', style: TextStyle(fontSize: 12, color: Colors.grey)),
                            const SizedBox(height: 4),
                            _buildTimeDropdown(
                              _configBootStart,
                              setDialogState,
                              min: 0,
                              max: 23,
                              onChanged: (v) {
                                _configBootStart = v;
                                if (_configBootEnd <= v) _configBootEnd = v + 1;
                              },
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(width: 16),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            const Text('结束时间', style: TextStyle(fontSize: 12, color: Colors.grey)),
                            const SizedBox(height: 4),
                            _buildTimeDropdown(
                              _configBootEnd,
                              setDialogState,
                              min: _configBootStart + 1,
                              max: 24,
                              onChanged: (v) => _configBootEnd = v,
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),

                  // ③ GPS工作时间
                  const Text(
                    '③ GPS工作时间（小时）',
                    style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
                  ),
                  const SizedBox(height: 4),
                  Row(
                    children: [
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            const Text('开始时间', style: TextStyle(fontSize: 12, color: Colors.grey)),
                            const SizedBox(height: 4),
                            _buildTimeDropdown(
                              _configGpsStart,
                              setDialogState,
                              min: 0,
                              max: 23,
                              onChanged: (v) {
                                _configGpsStart = v;
                                if (_configGpsEnd <= v) _configGpsEnd = v + 1;
                              },
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(width: 16),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            const Text('结束时间', style: TextStyle(fontSize: 12, color: Colors.grey)),
                            const SizedBox(height: 4),
                            _buildTimeDropdown(
                              _configGpsEnd,
                              setDialogState,
                              min: _configGpsStart + 1,
                              max: 24,
                              onChanged: (v) => _configGpsEnd = v,
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),

                  // ④ 主周期
                  const Text(
                    '④ 主周期（分钟）',
                    style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
                  ),
                  const SizedBox(height: 4),
                  Row(
                    children: [
                      const Expanded(
                        child: Text('主周期', style: TextStyle(fontSize: 13)),
                      ),
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                        decoration: BoxDecoration(
                          color: Colors.blue[50],
                          borderRadius: BorderRadius.circular(8),
                          border: Border.all(color: Colors.blue[200]!),
                        ),
                        child: Text(
                          '$_configMainCycle 分钟',
                          style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: Colors.blue),
                        ),
                      ),
                    ],
                  ),
                  Slider(
                    value: _configMainCycle.toDouble(),
                    min: 10,
                    max: 100,
                    divisions: 9,
                    label: '$_configMainCycle 分钟',
                    activeColor: Colors.blue,
                    onChanged: (v) {
                      setDialogState(() {
                        _configMainCycle = v.round();
                      });
                    },
                  ),
                ],
              ),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(dialogContext),
                child: const Text('取消'),
              ),
              TextButton(
                onPressed: () {
                  // 确定前先同步上报周期TextField的当前输入值
                  final inputVal = int.tryParse(reportIntervalController.text.trim());
                  if (inputVal != null) {
                    _configReportInterval = inputVal.clamp(5, 60);
                  }
                  final configStr = _generateConfigString();
                  _commandController.text = '{"cmd":"A","value":"$configStr"}';
                  Navigator.pop(dialogContext);
                },
                style: TextButton.styleFrom(foregroundColor: Colors.blue),
                child: const Text('确定'),
              ),
            ],
          );
        },
      ),
    );
  }

  /// 构建时间选择下拉框
  Widget _buildTimeDropdown(
    int value,
    StateSetter setDialogState, {
    required void Function(int val) onChanged,
    required int min,
    required int max,
  }) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8),
      decoration: BoxDecoration(
        color: Colors.blue[50],
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.blue[200]!),
      ),
      child: DropdownButton<int>(
        value: value,
        isExpanded: true,
        underline: const SizedBox(),
        items: List.generate(max - min + 1, (i) {
          final hour = min + i;
          return DropdownMenuItem(
            value: hour,
            child: Text('$hour 时', style: const TextStyle(fontSize: 14)),
          );
        }),
        onChanged: (v) {
          if (v != null) {
            setDialogState(() {
              onChanged(v);
            });
          }
        },
      ),
    );
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
          'info': {'limit': 3, 'deviceId': targetDeviceId, 'wechatid': globalWechatId},
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
        'wechatid': globalWechatId,
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
                      _loadCurrentConfig(deviceId);
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
                    _commandController.text = '{"cmd":"upgps","value":"0"}';
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
                    _commandController.text = '{"cmd":"power","value":"20"}';
                  },
                ),
                const SizedBox(width: 8),
                _buildQuickCommandButton(
                  icon: Icons.battery_alert,
                  label: '最低电量',
                  iconColor: Colors.green,
                  onTap: () {
                    _commandController.text = '{"cmd":"minBattery","value":"50"}';
                  },
                ),
                const SizedBox(width: 8),
                _buildQuickCommandButton(
                  icon: Icons.settings,
                  label: '配置下发',
                  iconColor: Colors.green,
                  onTap: _showConfigDialog,
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
