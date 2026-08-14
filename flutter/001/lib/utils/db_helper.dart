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
      version: 11, // 升级到版本11，添加DeviceName列
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
        ProductKey TEXT,
        DeviceName TEXT,
        visible INTEGER,
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

    // 设置表
    await db.execute('''
      CREATE TABLE settings (
        key TEXT PRIMARY KEY,
        value TEXT,
        updated_at TEXT
      )
    ''');

    // 设备对时表
    await db.execute('''
      CREATE TABLE device_sync (
        deviceId TEXT PRIMARY KEY,
        lorastr TEXT,
        time TEXT,
        upDateDevice TEXT,
        cached_at TEXT
      )
    ''');

    // 设备配置缓存表
    await db.execute('''
      CREATE TABLE device_config (
        deviceId TEXT PRIMARY KEY,
        config_data TEXT,
        cached_at TEXT
      )
    ''');

    // 待提交操作表（离线操作队列）
    await db.execute('''
      CREATE TABLE pending_operations (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        operation_type TEXT NOT NULL,
        operation_data TEXT NOT NULL,
        created_at TEXT NOT NULL,
        retry_count INTEGER DEFAULT 0
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
    
    if (oldVersion < 5) {
      // 版本5：添加设置表
      try {
        await db.execute('''
          CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT,
            updated_at TEXT
          )
        ''');
        print('数据库升级: 已创建 settings 表');
      } catch (e) {
        print('数据库升级: 创建 settings 表时出错: $e');
      }
    }
    
    if (oldVersion < 6) {
      // 版本6：添加设备对时表
      try {
        await db.execute('''
          CREATE TABLE IF NOT EXISTS device_sync (
            deviceId TEXT PRIMARY KEY,
            lorastr TEXT,
            time TEXT,
            upDateDevice TEXT,
            cached_at TEXT
          )
        ''');
        print('数据库升级: 已创建 device_sync 表');
      } catch (e) {
        print('数据库升级: 创建 device_sync 表时出错: $e');
      }
    }
    
    if (oldVersion < 7) {
      // 版本7：设备表添加ProductKey列
      try {
        await db.execute('ALTER TABLE devices ADD COLUMN ProductKey TEXT');
        print('数据库升级: 已添加 ProductKey 列');
      } catch (e) {
        print('数据库升级: 添加 ProductKey 列时出错: $e');
      }
    }
    
    if (oldVersion < 8) {
      // 版本8：设备表添加visible列
      try {
        await db.execute('ALTER TABLE devices ADD COLUMN visible INTEGER');
        print('数据库升级: 已添加 visible 列');
      } catch (e) {
        print('数据库升级: 添加 visible 列时出错: $e');
      }
    }
    
    if (oldVersion < 9) {
      // 版本9：添加设备配置缓存表
      try {
        await db.execute('''
          CREATE TABLE IF NOT EXISTS device_config (
            deviceId TEXT PRIMARY KEY,
            config_data TEXT,
            cached_at TEXT
          )
        ''');
        print('数据库升级: 已创建 device_config 表');
      } catch (e) {
        print('数据库升级: 创建 device_config 表时出错: $e');
      }
    }

    if (oldVersion < 10) {
      // 版本10：添加待提交操作表
      try {
        await db.execute('''
          CREATE TABLE IF NOT EXISTS pending_operations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            operation_type TEXT NOT NULL,
            operation_data TEXT NOT NULL,
            created_at TEXT NOT NULL,
            retry_count INTEGER DEFAULT 0
          )
        ''');
        print('数据库升级: 已创建 pending_operations 表');
      } catch (e) {
        print('数据库升级: 创建 pending_operations 表时出错: $e');
      }
    }

    if (oldVersion < 11) {
      // 版本11：设备表添加DeviceName列
      try {
        await db.execute('ALTER TABLE devices ADD COLUMN DeviceName TEXT');
        print('数据库升级: 已添加 DeviceName 列');
      } catch (e) {
        print('数据库升级: 添加 DeviceName 列时出错: $e');
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
        'ProductKey': device['ProductKey'],
        'DeviceName': device['DeviceName'],
        'visible': device['visible'] == true ? 1 : (device['visible'] == 'true' ? 1 : 0),
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

  /// 保存设备对时数据（覆盖式）
  Future<void> saveDeviceSync(List<Map<String, dynamic>> deviceSyncList) async {
    final db = await database;
    final batch = db.batch();

    // 清空旧数据
    batch.delete('device_sync');

    // 插入新数据
    for (final item in deviceSyncList) {
      batch.insert('device_sync', {
        'deviceId': item['deviceId'],
        'lorastr': item['lorastr'],
        'time': item['time'],
        'upDateDevice': item['upDateDevice'],
        'cached_at': DateTime.now().toIso8601String(),
      });
    }

    await batch.commit();
    print('保存设备对时数据: ${deviceSyncList.length} 条');
  }

  /// 读取设备对时数据
  Future<List<Map<String, dynamic>>> getDeviceSync() async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query('device_sync');
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

  /// 根据deviceId读取日志数据
  Future<List<Map<String, dynamic>>> getLogsByDeviceId(String deviceId) async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'logs',
      where: 'deviceId = ?',
      whereArgs: [deviceId],
      orderBy: 'time ASC',
    );
    return maps;
  }

  /// 根据deviceId和日期读取日志数据（用于轨迹缓存）
  Future<List<Map<String, dynamic>>> getLogsByDeviceIdAndDate(String deviceId, String date) async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'logs',
      where: 'deviceId = ? AND time LIKE ?',
      whereArgs: [deviceId, '$date%'],
      orderBy: 'time ASC',
    );
    return maps;
  }

  /// 保存设备轨迹日志缓存（按设备+日期覆盖）
  Future<void> saveTrajectoryCache(String deviceId, String date, List<Map<String, dynamic>> logs) async {
    final db = await database;
    final batch = db.batch();

    // 删除该设备当天的旧缓存
    batch.delete(
      'logs',
      where: 'deviceId = ? AND time LIKE ?',
      whereArgs: [deviceId, '$date%'],
    );

    // 插入新数据
    for (final log in logs) {
      batch.insert('logs', {
        'deviceId': deviceId,
        'auto_id': log['auto_id']?.toString() ?? '',
        'lorastr': log['lorastr']?.toString() ?? '',
        'time': log['time']?.toString() ?? '',
        'upDateDevice': log['upDateDevice']?.toString() ?? '',
        'picurl': log['picurl']?.toString() ?? '',
        'cached_at': DateTime.now().toIso8601String(),
      });
    }

    await batch.commit();
    debugPrint('[DB] 保存轨迹缓存: deviceId=$deviceId, date=$date, count=${logs.length}');
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

  /// 从蓝牙缓存获取指定设备指定日期的GPS轨迹点（type 1和5）
  /// 用于断网时轨迹回退
  Future<List<Map<String, dynamic>>> getBluetoothGpsForTrajectory(String deviceMarker, String date) async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'bluetooth_data',
      orderBy: 'cached_at ASC',
    );

    debugPrint('[轨迹蓝牙] 查询参数: deviceMarker=$deviceMarker, date=$date');
    debugPrint('[轨迹蓝牙] bluetooth_data表总记录数: ${maps.length}');

    // 生成多种日期格式用于匹配蓝牙数据中的time字段
    // date格式: "2026-08-13"（横线补零）
    // 蓝牙time格式: "2026/8/13 13:12:44"（斜线不补零）
    final dateParts = date.split('-');
    final year = dateParts[0];
    final month = int.tryParse(dateParts[1])?.toString() ?? dateParts[1];
    final day = int.tryParse(dateParts[2])?.toString() ?? dateParts[2];
    final slashDate = '$year/$month/$day'; // "2026/8/13"
    debugPrint('[轨迹蓝牙] 匹配日期: dash=$date, slash=$slashDate');

    // 统计各阶段过滤数量
    int skipEmpty = 0, skipNoPipe = 0, skipShort = 0;
    int skipType = 0, skipDevice = 0, skipDate = 0;
    int matchType15 = 0;

    final result = <Map<String, dynamic>>[];
    for (final item in maps) {
      final dataStr = item['data'] as String?;
      if (dataStr == null || dataStr.isEmpty) { skipEmpty++; continue; }

      try {
        final jsonData = jsonDecode(dataStr) as Map<String, dynamic>;
        final info = jsonData['info'] as String? ?? '';
        if (!info.contains('|')) { skipNoPipe++; continue; }

        final parts = info.split('|');
        if (parts.length < 3) { skipShort++; continue; }

        final type = parts[0];
        final marker = parts[1];

        // 记录type 1/5的设备标记，用于调试
        if (type == '1' || type == '5') {
          matchType15++;
          if (result.length < 3) {
            debugPrint('[轨迹蓝牙] type=$type, marker=$marker(目标=$deviceMarker), time=${item['time']}, info=$info');
          }
        }

        // 只取type 1(GPS)和5(跟踪)，且匹配设备
        if (type != '1' && type != '5') { skipType++; continue; }
        if (marker != deviceMarker) { skipDevice++; continue; }

        final timeStr = item['time'] as String? ?? '';
        // 兼容两种日期格式：横线格式和斜线格式
        if (!timeStr.startsWith(date) && !timeStr.startsWith(slashDate)) { skipDate++; continue; }

        result.add({
          'lorastr': info,
          'time': timeStr,
        });
      } catch (e) {
        debugPrint('[轨迹蓝牙] 解析异常: $e');
        continue;
      }
    }

    debugPrint('[轨迹蓝牙] 过滤统计: 空数据=$skipEmpty, 无分隔符=$skipNoPipe, 字段不足=$skipShort, '
        '类型不符=$skipType, 设备不符=$skipDevice, 日期不符=$skipDate');
    debugPrint('[轨迹蓝牙] type1/5总数=$matchType15, 最终匹配=${result.length}条');
    return result;
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

  /// 读取所有道路数据（不过滤level）
  Future<List<Map<String, dynamic>>> getAllRoutes() async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'map_routes',
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

  /// 读取所有地名数据（不过滤level）
  Future<List<Map<String, dynamic>>> getAllPlaces() async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'map_places',
      orderBy: 'cached_at DESC',
    );
    
    // 解析JSON数据
    return maps.map((map) {
      final placeData = map['place_data'] as String;
      return jsonDecode(placeData) as Map<String, dynamic>;
    }).toList();
  }

  /// 保存设置值
  Future<void> saveSetting(String key, String value) async {
    final db = await database;
    await db.insert(
      'settings',
      {
        'key': key,
        'value': value,
        'updated_at': DateTime.now().toIso8601String(),
      },
      conflictAlgorithm: ConflictAlgorithm.replace,
    );
    debugPrint('[Settings] 保存设置: $key = $value');
  }

  /// 读取设置值
  Future<String?> getSetting(String key) async {
    final db = await database;
    final List<Map<String, dynamic>> result = await db.query(
      'settings',
      where: 'key = ?',
      whereArgs: [key],
    );
    
    if (result.isNotEmpty) {
      return result.first['value'] as String?;
    }
    return null;
  }

  /// 读取布尔设置值
  Future<bool> getBoolSetting(String key, {bool defaultValue = false}) async {
    final value = await getSetting(key);
    if (value == null) return defaultValue;
    return value.toLowerCase() == 'true';
  }

  /// 保存设备配置数据（覆盖式）
  Future<void> saveDeviceConfig(String deviceId, Map<String, dynamic> configData) async {
    final db = await database;
    await db.insert('device_config', {
      'deviceId': deviceId,
      'config_data': jsonEncode(configData),
      'cached_at': DateTime.now().toIso8601String(),
    }, conflictAlgorithm: ConflictAlgorithm.replace);
    print('保存设备配置: deviceId=$deviceId');
  }

  /// 获取设备配置数据
  Future<Map<String, dynamic>?> getDeviceConfig(String deviceId) async {
    final db = await database;
    final List<Map<String, dynamic>> result = await db.query(
      'device_config',
      where: 'deviceId = ?',
      whereArgs: [deviceId],
    );
    
    if (result.isNotEmpty) {
      final configData = result.first['config_data'] as String?;
      if (configData != null) {
        return jsonDecode(configData) as Map<String, dynamic>;
      }
    }
    return null;
  }

  /// 获取设备配置的缓存时间
  Future<String?> getDeviceConfigCachedAt(String deviceId) async {
    final db = await database;
    final List<Map<String, dynamic>> result = await db.query(
      'device_config',
      columns: ['cached_at'],
      where: 'deviceId = ?',
      whereArgs: [deviceId],
    );
    if (result.isNotEmpty) {
      return result.first['cached_at'] as String?;
    }
    return null;
  }

  /// 获取所有设备配置数据
  Future<List<Map<String, dynamic>>> getAllDeviceConfig() async {
    final db = await database;
    final List<Map<String, dynamic>> result = await db.query('device_config');
    final List<Map<String, dynamic>> configs = [];
    for (final row in result) {
      final configData = row['config_data'] as String?;
      final cachedAt = row['cached_at'] as String?;
      if (configData != null) {
        final parsed = jsonDecode(configData) as Map<String, dynamic>;
        parsed['_cached_at'] = cachedAt ?? ''; // 附带缓存时间用于比较
        configs.add(parsed);
      }
    }
    return configs;
  }

  // ==================== 待提交操作（离线操作队列）====================

  /// 添加待提交操作
  Future<void> addPendingOperation(String operationType, Map<String, dynamic> operationData) async {
    final db = await database;
    await db.insert('pending_operations', {
      'operation_type': operationType,
      'operation_data': jsonEncode(operationData),
      'created_at': DateTime.now().toIso8601String(),
      'retry_count': 0,
    });
    debugPrint('[DB] 添加待提交操作: type=$operationType');
  }

  /// 获取所有待提交操作（按创建时间升序）
  Future<List<Map<String, dynamic>>> getPendingOperations() async {
    final db = await database;
    final List<Map<String, dynamic>> result = await db.query(
      'pending_operations',
      orderBy: 'created_at ASC',
    );
    return result.map((row) => {
      'id': row['id'],
      'operation_type': row['operation_type'],
      'operation_data': jsonDecode(row['operation_data'] as String) as Map<String, dynamic>,
      'created_at': row['created_at'],
      'retry_count': row['retry_count'],
    }).toList();
  }

  /// 删除待提交操作
  Future<void> deletePendingOperation(int id) async {
    final db = await database;
    await db.delete('pending_operations', where: 'id = ?', whereArgs: [id]);
    debugPrint('[DB] 删除待提交操作: id=$id');
  }

  /// 更新待提交操作的重试次数
  Future<void> updatePendingOperationRetryCount(int id, int retryCount) async {
    final db = await database;
    await db.update('pending_operations', {'retry_count': retryCount}, where: 'id = ?', whereArgs: [id]);
  }

  /// 关闭数据库
  Future<void> close() async {
    final db = await database;
    db.close();
  }
}
