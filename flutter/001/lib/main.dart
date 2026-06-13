import 'package:flutter/material.dart';
import 'package:flutter_map_tile_caching/flutter_map_tile_caching.dart';
import 'pages/device_manage_page.dart';
import 'pages/livestock_manage_page.dart';
import 'pages/function_list_page.dart';
import 'pages/map_center_page.dart';

// 阿里云 FC 函数地址（HTTPS 公网接口）
const deviceFcUrl = 'https://gpsmoveinfo.cn/fc/device';
const cowSheepFcUrl = 'https://gpsmoveinfo.cn/fc/cowsheep';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  
  // 初始化地图瓦片缓存后端
  try {
    await FMTCObjectBoxBackend().initialise();
    debugPrint('[FMTC] 初始化成功');
  } catch (err) {
    debugPrint('[FMTC] 初始化失败: $err');
  }
  
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

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: IndexedStack(
        index: _currentIndex,
        children: const [
          DeviceManagePage(),
          LivestockManagePage(),
          FunctionListPage(),
          MapCenterPage(),
        ],
      ),
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
