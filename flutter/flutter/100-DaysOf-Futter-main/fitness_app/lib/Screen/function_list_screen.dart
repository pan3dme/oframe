import 'package:flutter/material.dart';
import 'package:fitness_app/Screen/bluetooth_screen.dart';

class FunctionListScreen extends StatelessWidget {
  const FunctionListScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('功能列表'),
        centerTitle: true,
        elevation: 0,
      ),
      body: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          children: [
            // 功能按钮网格 - 2行3列
            GridView.count(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              crossAxisCount: 3,
              crossAxisSpacing: 15,
              mainAxisSpacing: 15,
              childAspectRatio: 0.85,
              children: [
                _buildFunctionItem(
                  icon: Icons.history,
                  label: '最近10条记录',
                  color: Colors.blue,
                  onTap: () {
                    print('点击最近10条记录');
                    // TODO: 导航到最近记录页面
                  },
                ),
                _buildFunctionItem(
                  icon: Icons.lan,
                  label: '上报设备LORA',
                  color: Colors.indigo,
                  onTap: () {
                    print('点击上报设备LORA');
                    // TODO: 实现LORA设备上报功能
                  },
                ),
                _buildFunctionItem(
                  icon: Icons.settings_input_component,
                  label: '管理设备',
                  color: Colors.purple,
                  onTap: () {
                    print('点击管理设备');
                    // TODO: 导航到设备管理页面
                  },
                ),
                _buildFunctionItem(
                  icon: Icons.pets,
                  label: '管理牛羊',
                  color: Colors.green,
                  onTap: () {
                    print('点击管理牛羊');
                    // TODO: 导航到牛羊管理页面
                  },
                ),
                _buildFunctionItem(
                  icon: Icons.route,
                  label: '查看设备轨迹',
                  color: Colors.orange,
                  onTap: () {
                    print('点击查看设备轨迹');
                    // TODO: 导航到设备轨迹页面
                  },
                ),
                _buildFunctionItem(
                  icon: Icons.bluetooth_connected,
                  label: '链接蓝牙',
                  color: Colors.teal,
                  onTap: () {
                    Navigator.push(
                      context,
                      MaterialPageRoute(
                        builder: (context) => const BluetoothScreen(),
                      ),
                    );
                  },
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildFunctionItem({
    required IconData icon,
    required String label,
    required Color color,
    required VoidCallback onTap,
  }) {
    return InkWell(
      onTap: onTap,
      child: Container(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            colors: [color, color.withOpacity(0.7)],
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
          ),
          borderRadius: BorderRadius.circular(16),
          boxShadow: [
            BoxShadow(
              color: color.withOpacity(0.3),
              blurRadius: 8,
              offset: const Offset(0, 4),
            ),
          ],
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(
              icon,
              size: 40,
              color: Colors.white,
            ),
            const SizedBox(height: 10),
            Text(
              label,
              textAlign: TextAlign.center,
              style: const TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w600,
                color: Colors.white,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
