import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:sqflite/sqflite.dart';
import 'package:path/path.dart';

/// 数据库帮助类 - 用于离线缓存设备和牛羊数据
class DBHelper {
  static final DBHelper _instance = DBHelper._internal();
  factory DBHelper() => _instance;
  DBHelper._internal();

  Database? _database;

  /// 获取数据库实例
  Future<Database> get database async {
    if (_database != null) return _database!;
    _database = await _initDatabase();
    return _database!;
  }

  /// 初始化数据库
  Future<Database> _initDatabase() async {
    final dbPath = await getDatabasesPath();
    final path = join(dbPath, 'app_cache.db');

    return await openDatabase(
      path,
      version: 4, // 升级到版本4，添加地图道路和地名表
      onCreate: _onCreate,
      onUpgrade: _onUpgrade,
      onDowngrade: _onUpgrade, // 也处理降级情况
    );
  }

  /// 创建表
  Future<void> _onCreate(Database db, int version) async {
    // 设备表
    await db.execute('''
      CREATE TABLE devices (
        deviceId TEXT PRIMARY KEY,
        device_key TEXT,
        link_cowsheep_id TEXT,
        rename TEXT,
        picurl TEXT,
        cached_at TEXT
      )
    ''');

    // 牛羊表
    await db.execute('''
      CREATE TABLE livestock (
        cowsheep_id TEXT PRIMARY KEY,
        birthday TEXT,
        gender INTEGER,
        avatar TEXT,
        rename TEXT,
        cached_at TEXT
      )
    ''');

    // 日志表
    await db.execute('''
      CREATE TABLE logs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        deviceId TEXT,
        auto_id TEXT,
        lorastr TEXT,
        time TEXT,
        upDateDevice TEXT,
        picurl TEXT,
        cached_at TEXT
      )
    ''');

    // 设备LOT表
    await db.execute('''
      CREATE TABLE device_lot (
        deviceId TEXT PRIMARY KEY,
        lorastr TEXT,
        time TEXT,
        gps TEXT,
        upDateDevice TEXT,
        cached_at TEXT
      )
    ''');

    // 蓝牙数据表
    await db.execute('''
      CREATE TABLE bluetooth_data (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        device_name TEXT,
        device_id TEXT,
        data TEXT,
        time TEXT,
        cached_at TEXT
      )
    ''');

    // 道路数据表
    await db.execute('''
      CREATE TABLE map_routes (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        route_data TEXT,
        level INTEGER DEFAULT 1,
        cached_at TEXT
      )
    ''');

    // 地名数据表
    await db.execute('''
      CREATE TABLE map_places (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        place_data TEXT,
        level INTEGER DEFAULT 1,
        cached_at TEXT
      )
    ''');
  }

  /// 数据库升级
  Future<void> _onUpgrade(Database db, int oldVersion, int newVersion) async {
    print('数据库升级: 从版本 $oldVersion 到 $newVersion');
    
    if (oldVersion < 2) {
      // 版本2：添加设备LOT表
      try {
        await db.execute('''
          CREATE TABLE IF NOT EXISTS device_lot (
            deviceId TEXT PRIMARY KEY,
            lorastr TEXT,
            time TEXT,
            gps TEXT,
            upDateDevice TEXT,
            cached_at TEXT
          )
        ''');
        print('数据库升级: 已创建 device_lot 表');
      } catch (e) {
        print('数据库升级: 创建 device_lot 表时出错: $e');
      }
    }
    
    if (oldVersion < 3) {
      // 版本3：添加蓝牙数据表
      try {
        await db.execute('''
          CREATE TABLE IF NOT EXISTS bluetooth_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_name TEXT,
            device_id TEXT,
            data TEXT,
            time TEXT,
            cached_at TEXT
          )
        ''');
        print('数据库升级: 已创建 bluetooth_data 表');
      } catch (e) {
        print('数据库升级: 创建 bluetooth_data 表时出错: $e');
      }
    }
    
    if (oldVersion < 4) {
      // 版本4：添加地图道路和地名表
      try {
        await db.execute('''
          CREATE TABLE IF NOT EXISTS map_routes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            route_data TEXT,
            level INTEGER DEFAULT 1,
            cached_at TEXT
          )
        ''');
        print('数据库升级: 已创建 map_routes 表');
        
        await db.execute('''
          CREATE TABLE IF NOT EXISTS map_places (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            place_data TEXT,
            level INTEGER DEFAULT 1,
            cached_at TEXT
          )
        ''');
        print('数据库升级: 已创建 map_places 表');
      } catch (e) {
        print('数据库升级: 创建地图表时出错: $e');
      }
    }
  }

  /// 保存设备数据（覆盖式）
  Future<void> saveDevices(List<Map<String, dynamic>> devices) async {
    final db = await database;
    final batch = db.batch();

    // 清空旧数据
    batch.delete('devices');

    // 插入新数据
    for (final device in devices) {
      batch.insert('devices', {
        'deviceId': device['deviceId'],
        'device_key': device['device_key'],
        'link_cowsheep_id': device['link_cowsheep_id'],
        'rename': device['rename'],
        'picurl': device['picurl'],
        'cached_at': DateTime.now().toIso8601String(),
      });
    }

    await batch.commit();
    print('保存设备数据: ${devices.length} 条');
  }

  /// 读取设备数据
  Future<List<Map<String, dynamic>>> getDevices() async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query('devices');
    return maps;
  }

  /// 保存牛羊数据（覆盖式）
  Future<void> saveLivestock(List<Map<String, dynamic>> livestock) async {
    print('[DB] 开始保存牛羊数据: ${livestock.length} 条');
    
    if (livestock.isEmpty) {
      print('[DB] 警告: 牛羊数据为空，跳过保存');
      return;
    }
    
    try {
      final db = await database;
      final batch = db.batch();

      // 清空旧数据
      batch.delete('livestock');
      print('[DB] 已清空旧牛羊数据');

      // 插入新数据
      int successCount = 0;
      for (final item in livestock) {
        try {
          batch.insert('livestock', {
            'cowsheep_id': item['cowsheep_id'],
            'birthday': item['birthday'],
            'gender': item['gender'] == true ? 1 : (item['gender'] == false ? 0 : null),
            'avatar': item['avatar'],
            'rename': item['rename'],
            'cached_at': DateTime.now().toIso8601String(),
          });
          successCount++;
        } catch (e) {
          print('[DB] 插入牛羊数据失败: $e, 数据: $item');
        }
      }
      
      print('[DB] 准备提交 ${successCount}/${livestock.length} 条牛羊数据');
      await batch.commit();
      print('[DB] ✓ 保存牛羊数据成功: ${livestock.length} 条');
    } catch (e) {
      print('[DB] ✗ 保存牛羊数据异常: $e');
      rethrow;
    }
  }

  /// 读取牛羊数据
  Future<List<Map<String, dynamic>>> getLivestock() async {
    try {
      final db = await database;
      final List<Map<String, dynamic>> maps = await db.query('livestock');
      print('[DB] 读取牛羊数据: ${maps.length} 条');
      
      if (maps.isNotEmpty) {
        print('[DB] 第一条牛羊数据: ${maps[0]}');
      }
      
      // 转换 gender 字段 - 创建新Map避免修改只读对象
      return maps.map((item) {
        final gender = item['gender'];
        // 创建新Map，而不是修改原有的只读Map
        return <String, dynamic>{
          'cowsheep_id': item['cowsheep_id'],
          'birthday': item['birthday'],
          'gender': gender == 1 ? true : (gender == 0 ? false : null),
          'avatar': item['avatar'],
          'rename': item['rename'],
          'cached_at': item['cached_at'],
        };
      }).toList();
    } catch (e, stackTrace) {
      print('[DB] ✗ 读取牛羊数据失败: $e');
      print('[DB] 堆栈: $stackTrace');
      return [];
    }
  }

  /// 保存日志数据（覆盖式）
  Future<void> saveLogs(List<Map<String, dynamic>> logs) async {
    final db = await database;
    final batch = db.batch();

    // 清空旧数据
    batch.delete('logs');

    // 插入新数据
    for (final log in logs) {
      batch.insert('logs', {
        'deviceId': log['deviceId'],
        'auto_id': log['auto_id'],
        'lorastr': log['lorastr'],
        'time': log['time'],
        'upDateDevice': log['upDateDevice'],
        'picurl': log['picurl'],
        'cached_at': DateTime.now().toIso8601String(),
      });
    }

    await batch.commit();
    print('保存日志数据: ${logs.length} 条');
  }

  /// 读取日志数据
  Future<List<Map<String, dynamic>>> getLogs() async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'logs',
      orderBy: 'cached_at DESC',
    );
    return maps;
  }

  /// 保存设备LOT数据（覆盖式）
  Future<void> saveDeviceLot(List<Map<String, dynamic>> deviceLotList) async {
    final db = await database;
    final batch = db.batch();

    // 清空旧数据
    batch.delete('device_lot');

    // 插入新数据
    for (final item in deviceLotList) {
      batch.insert('device_lot', {
        'deviceId': item['deviceId'],
        'lorastr': item['lorastr'],
        'time': item['time'],
        'gps': item['gps'],
        'upDateDevice': item['upDateDevice'],
        'cached_at': DateTime.now().toIso8601String(),
      });
    }

    await batch.commit();
    print('保存设备LOT数据: ${deviceLotList.length} 条');
  }

  /// 读取设备LOT数据
  Future<List<Map<String, dynamic>>> getDeviceLot() async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query('device_lot');
    return maps;
  }

  /// 根据deviceId读取设备LOT数据
  Future<Map<String, dynamic>?> getDeviceLotByDeviceId(String deviceId) async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'device_lot',
      where: 'deviceId = ?',
      whereArgs: [deviceId],
    );
    if (maps.isNotEmpty) {
      return maps.first;
    }
    return null;
  }

  /// 保存蓝牙数据
  Future<void> saveBluetoothData(String deviceName, String deviceId, String data, String time) async {
    final db = await database;
    await db.insert('bluetooth_data', {
      'device_name': deviceName,
      'device_id': deviceId,
      'data': data,
      'time': time,
      'cached_at': DateTime.now().toIso8601String(),
    });
    print('保存蓝牙数据: device=$deviceName, data=$data');
  }

  /// 读取所有蓝牙数据
  Future<List<Map<String, dynamic>>> getBluetoothData() async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'bluetooth_data',
      orderBy: 'cached_at DESC',
    );
    return maps;
  }

  /// 清空蓝牙数据
  Future<void> clearBluetoothData() async {
    final db = await database;
    await db.delete('bluetooth_data');
    print('已清空蓝牙数据');
  }

  /// 删除单条蓝牙数据（根据id）
  Future<void> deleteBluetoothDataById(int id) async {
    final db = await database;
    await db.delete(
      'bluetooth_data',
      where: 'id = ?',
      whereArgs: [id],
    );
    print('已删除蓝牙数据: id=$id');
  }

  /// 获取缓存时间
  Future<String?> getCacheTime(String tableName) async {
    final db = await database;
    final List<Map<String, dynamic>> result = await db.query(
      tableName,
      columns: ['cached_at'],
      orderBy: 'cached_at DESC',
      limit: 1,
    );
    
    if (result.isNotEmpty) {
      return result.first['cached_at'] as String?;
    }
    return null;
  }

  /// 保存道路数据（覆盖式）
  Future<void> saveRoutes(List<Map<String, dynamic>> routes) async {
    final db = await database;
    final batch = db.batch();

    // 清空旧数据
    batch.delete('map_routes');

    // 插入新数据
    for (final route in routes) {
      // 提取level字段，如果为空则默认为1
      int level = 1;
      try {
        final attributes = route['attributes'] as List<dynamic>?;
        if (attributes != null) {
          for (final attr in attributes) {
            final attrMap = attr as Map<String, dynamic>;
            final columnName = attrMap['columnName']?.toString() ?? '';
            if (columnName == 'level') {
              final levelValue = attrMap['columnValue'];
              if (levelValue != null && levelValue.toString().isNotEmpty) {
                level = int.tryParse(levelValue.toString()) ?? 1;
              }
              break;
            }
          }
        }
      } catch (e) {
        debugPrint('[DB] 解析道路level失败: $e');
      }

      batch.insert('map_routes', {
        'route_data': jsonEncode(route),
        'level': level,
        'cached_at': DateTime.now().toIso8601String(),
      });
    }

    await batch.commit();
    print('保存道路数据: ${routes.length} 条');
  }

  /// 读取道路数据（根据level过滤）
  Future<List<Map<String, dynamic>>> getRoutes({int maxLevel = 1}) async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'map_routes',
      where: 'level <= ?',
      whereArgs: [maxLevel],
      orderBy: 'cached_at DESC',
    );
    
    // 解析JSON数据
    return maps.map((map) {
      final routeData = map['route_data'] as String;
      return jsonDecode(routeData) as Map<String, dynamic>;
    }).toList();
  }

  /// 保存地名数据（覆盖式）
  Future<void> savePlaces(List<Map<String, dynamic>> places) async {
    final db = await database;
    final batch = db.batch();

    // 清空旧数据
    batch.delete('map_places');

    // 插入新数据
    for (final place in places) {
      // 提取level字段，如果为空则默认为1
      int level = 1;
      try {
        final attributes = place['attributes'] as List<dynamic>?;
        if (attributes != null) {
          for (final attr in attributes) {
            final attrMap = attr as Map<String, dynamic>;
            final columnName = attrMap['columnName']?.toString() ?? '';
            if (columnName == 'level') {
              final levelValue = attrMap['columnValue'];
              if (levelValue != null && levelValue.toString().isNotEmpty) {
                level = int.tryParse(levelValue.toString()) ?? 1;
              }
              break;
            }
          }
        }
      } catch (e) {
        debugPrint('[DB] 解析地名level失败: $e');
      }

      batch.insert('map_places', {
        'place_data': jsonEncode(place),
        'level': level,
        'cached_at': DateTime.now().toIso8601String(),
      });
    }

    await batch.commit();
    print('保存地名数据: ${places.length} 条');
  }

  /// 读取地名数据（根据level过滤）
  Future<List<Map<String, dynamic>>> getPlaces({int maxLevel = 1}) async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'map_places',
      where: 'level <= ?',
      whereArgs: [maxLevel],
      orderBy: 'cached_at DESC',
    );
    
    // 解析JSON数据
    return maps.map((map) {
      final placeData = map['place_data'] as String;
      return jsonDecode(placeData) as Map<String, dynamic>;
    }).toList();
  }

  /// 关闭数据库
  Future<void> close() async {
    final db = await database;
    db.close();
  }
}
