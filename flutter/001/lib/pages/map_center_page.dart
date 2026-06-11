import 'package:flutter/material.dart';

class MapCenterPage extends StatelessWidget {
  const MapCenterPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('地图中心'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: const Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.map, size: 64, color: Colors.grey),
            SizedBox(height: 16),
            Text('地图中心', style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
            SizedBox(height: 8),
            Text('功能开发中...', style: TextStyle(color: Colors.grey)),
          ],
        ),
      ),
    );
  }
}