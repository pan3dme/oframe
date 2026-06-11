import 'package:flutter/material.dart';

class LivestockManagePage extends StatelessWidget {
  const LivestockManagePage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('牛羊管理'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: const Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.pets, size: 64, color: Colors.grey),
            SizedBox(height: 16),
            Text('牛羊管理', style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
            SizedBox(height: 8),
            Text('功能开发中...', style: TextStyle(color: Colors.grey)),
          ],
        ),
      ),
    );
  }
}