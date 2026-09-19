import 'package:flutter/material.dart';
import 'package:flutter_map_tile_caching/flutter_map_tile_caching.dart';
import 'pages/login_page.dart';
import 'pages/device_manage_page.dart';
import 'pages/device_detail_page.dart';
import 'pages/map_center_page.dart';
import 'pages/function_list_page.dart';
import 'utils/db_helper.dart';

// 全局路由观察者，用于监听页面可见性
final RouteObserver<PageRoute> routeObserver = RouteObserver<PageRoute>();

// 全局 wechatid（登录成功后由服务器返回，所有FC请求的info中携带）
String globalWechatId = '';

// 全局选中的设备数据（设备管理页点击设备时设置，设备详情TAB监听）
Map<String, dynamic>? globalSelectedDevice;
Map<String, dynamic>? globalSelectedDeviceLot;

// 设备变更通知器（设备管理页点击设备时 notify，设备详情TAB监听刷新）
final ValueNotifier<int> deviceSelectedNotifier = ValueNotifier<int>(0);

// 设备列表页 GlobalKey（用于TAB重入时触发无感刷新）
final GlobalKey<DeviceManagePageState> deviceListKey = GlobalKey<DeviceManagePageState>();

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
      navigatorObservers: [routeObserver], // 添加路由观察者
      initialRoute: '/',
      routes: {
        '/': (context) => const LoginPage(),
        '/home': (context) => const HomePage(),
      },
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
  void initState() {
    super.initState();
    _loadLastSelectedDevice();
  }

  /// 启动时加载最后选中的设备
  Future<void> _loadLastSelectedDevice() async {
    try {
      final deviceId = await DBHelper().getSetting('last_selected_device_id');
      if (deviceId != null && deviceId.isNotEmpty) {
        // 从缓存中查找设备和LOT数据
        final devices = await DBHelper().getDevices();
        final device = devices.cast<Map<String, dynamic>>().firstWhere(
          (d) => d['deviceId']?.toString() == deviceId,
          orElse: () => <String, dynamic>{},
        );
        if (device.isNotEmpty) {
          final lot = await DBHelper().getDeviceLotByDeviceId(deviceId);
          globalSelectedDevice = device;
          globalSelectedDeviceLot = lot;
          // 通知设备详情TAB刷新
          deviceSelectedNotifier.value++;
          debugPrint('[主页] 恢复上次选中设备: deviceId=$deviceId');
        }
      }
    } catch (e) {
      debugPrint('[主页] 加载最后选中设备失败: $e');
    }
  }

  /// 切换到设备详情TAB
  void switchToDeviceDetailTab() {
    setState(() {
      _currentIndex = 0;
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: IndexedStack(
        index: _currentIndex,
        children: [
          DeviceDetailTabPage(onSwitchTab: switchToDeviceDetailTab),
          DeviceManagePage(key: deviceListKey, onDeviceTap: _onDeviceTap),
          FunctionListPage(),
          MapCenterPage(),
        ],
      ),
      bottomNavigationBar: BottomNavigationBar(
        currentIndex: _currentIndex,
        onTap: (index) {
          // 重复点击当前TAB时触发无感刷新
          if (index == _currentIndex && index == 1) {
            deviceListKey.currentState?.silentRefresh();
          }
          setState(() {
            _currentIndex = index;
          });
        },
        type: BottomNavigationBarType.fixed,
        selectedItemColor: Colors.blue,
        unselectedItemColor: Colors.grey,
        items: const [
          BottomNavigationBarItem(
            icon: Icon(Icons.info_outline),
            label: '设备详情',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.devices),
            label: '设备列表',
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

  /// 设备管理页点击设备回调
  void _onDeviceTap(Map<String, dynamic> device, Map<String, dynamic>? deviceLot) {
    globalSelectedDevice = device;
    globalSelectedDeviceLot = deviceLot;
    // 保存最后选中的设备ID
    final deviceId = device['deviceId']?.toString() ?? '';
    if (deviceId.isNotEmpty) {
      DBHelper().saveSetting('last_selected_device_id', deviceId);
    }
    // 通知设备详情TAB刷新
    deviceSelectedNotifier.value++;
    // 切换到设备详情TAB
    switchToDeviceDetailTab();
  }
}
