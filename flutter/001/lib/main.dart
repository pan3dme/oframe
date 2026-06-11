import 'package:flutter/material.dart';
import 'pages/device_manage_page.dart';
import 'pages/livestock_manage_page.dart';
import 'pages/function_list_page.dart';
import 'pages/map_center_page.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: '牛羊助手',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue),
        useMaterial3: true,
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  int _currentIndex = 0;

  final List<Widget> _tabPages = const [
    DeviceManagePage(),
    LivestockManagePage(),
    FunctionListPage(),
    MapCenterPage(),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: _tabPages[_currentIndex],
      bottomNavigationBar: BottomNavigationBar(
        currentIndex: _currentIndex,
        onTap: (index) {
          setState(() {
            _currentIndex = index;
          });
        },
        type: BottomNavigationBarType.fixed,
        selectedItemColor: Colors.blue,
        unselectedItemColor: Colors.grey,
        items: const [
          BottomNavigationBarItem(
            icon: Icon(Icons.devices),
            label: '设备管理',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.pets),
            label: '牛羊管理',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.apps),
            label: '功能列表',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.map),
            label: '地图中心',
          ),
        ],
      ),
    );
  }
}
