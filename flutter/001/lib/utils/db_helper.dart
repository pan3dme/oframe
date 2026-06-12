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
      version: 1,
      onCreate: _onCreate,
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

  /// 关闭数据库
  Future<void> close() async {
    final db = await database;
    db.close();
  }
}
