import 'package:flutter/material.dart';
import 'bluetooth_page.dart';

class FunctionListPage extends StatelessWidget {
  const FunctionListPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('功能列表'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Card(
            child: ListTile(
              leading: const Icon(Icons.bluetooth, color: Colors.blue, size: 32),
              title: const Text('连接蓝牙', style: TextStyle(fontSize: 16)),
              subtitle: const Text('搜索并连接牛羊蓝牙设备'),
              trailing: const Icon(Icons.chevron_right),
              onTap: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (context) => const BluetoothPage(),
                  ),
                );
              },
            ),
          ),
          const SizedBox(height: 8),
          Card(
            child: ListTile(
              leading: const Icon(Icons.sync, color: Colors.orange, size: 32),
              title: const Text('数据同步', style: TextStyle(fontSize: 16)),
              subtitle: const Text('同步设备数据到本地'),
              trailing: const Icon(Icons.chevron_right),
              onTap: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (context) => const BluetoothPage(),
                  ),
                );
              },
            ),
          ),
          const SizedBox(height: 8),
          Card(
            child: ListTile(
              leading: const Icon(Icons.settings, color: Colors.grey, size: 32),
              title: const Text('设备设置', style: TextStyle(fontSize: 16)),
              subtitle: const Text('配置蓝牙设备参数'),
              trailing: const Icon(Icons.chevron_right),
              onTap: () {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('功能开发中...')),
                );
              },
            ),
          ),
        ],
      ),
    );
  }
}