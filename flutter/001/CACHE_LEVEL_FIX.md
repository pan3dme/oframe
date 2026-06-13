# 缓存数据Level过滤问题修复

## 问题描述

当使用缓存的道路和地名数据时,**level=2的数据没有显示到地图上**,即使已经切换到level≤2的显示模式。

## 问题原因

### 原始代码逻辑

```dart
// ❌ 错误的做法：从缓存加载时就进行level过滤
Future<void> _loadFromCache() async {
  final cachedRoutes = await DBHelper().getRoutes(maxLevel: _currentLevel); // _currentLevel初始为1
  final cachedPlaces = await DBHelper().getPlaces(maxLevel: _currentLevel);
  
  setState(() {
    _allRouteData = cachedRoutes;  // 只包含level≤1的数据
    _allPlaceData = cachedPlaces;
  });
}
```

**问题分析:**
1. `_currentLevel`初始值为1
2. 从缓存加载时使用`maxLevel: _currentLevel`过滤
3. **结果**:只获取了level≤1的数据,level=2的数据被数据库查询过滤掉了
4. 即使后来切换到level≤2,`_allRouteData`中也没有level=2的数据

### 数据流向

```
网络加载 → 保存所有数据到DB ✅
   ↓
从DB读取 → 按level过滤 ❌ (只读了level≤1)
   ↓
内存存储 → 只有level≤1的数据
   ↓
切换level → 无法显示level=2 (因为内存中没有)
```

## 解决方案

### 核心思路

**缓存加载时获取所有数据,在内存中根据当前level过滤显示**

```
网络加载 → 保存所有数据到DB ✅
   ↓
从DB读取 → 获取所有数据(不过滤) ✅
   ↓
内存存储 → 包含所有level的数据
   ↓
切换level → 从内存中过滤显示 ✅
```

### 修改内容

#### 1. 添加获取所有数据的方法 (db_helper.dart)

```dart
/// 读取所有道路数据（不过滤level）
Future<List<Map<String, dynamic>>> getAllRoutes() async {
  final db = await database;
  final List<Map<String, dynamic>> maps = await db.query(
    'map_routes',
    orderBy: 'cached_at DESC',
  );
  
  return maps.map((map) {
    final routeData = map['route_data'] as String;
    return jsonDecode(routeData) as Map<String, dynamic>;
  }).toList();
}

/// 读取所有地名数据（不过滤level）
Future<List<Map<String, dynamic>>> getAllPlaces() async {
  final db = await database;
  final List<Map<String, dynamic>> maps = await db.query(
    'map_places',
    orderBy: 'cached_at DESC',
  );
  
  return maps.map((map) {
    final placeData = map['place_data'] as String;
    return jsonDecode(placeData) as Map<String, dynamic>;
  }).toList();
}
```

#### 2. 修改缓存加载逻辑 (map_center_page.dart)

```dart
// ✅ 正确的做法：从缓存加载所有数据
Future<void> _loadFromCache() async {
  try {
    // 从缓存加载所有数据（不过滤level）
    final cachedRoutes = await DBHelper().getAllRoutes();
    final cachedPlaces = await DBHelper().getAllPlaces();
    
    if (cachedRoutes.isNotEmpty || cachedPlaces.isNotEmpty) {
      setState(() {
        _allRouteData = cachedRoutes;  // 包含所有level的数据
        _allPlaceData = cachedPlaces;
        _filterDataByLevel(); // 根据当前level过滤显示
      });
      debugPrint('[道路地名] 从缓存加载: 道路${cachedRoutes.length}条, 地名${cachedPlaces.length}条');
    }
  } catch (e) {
    debugPrint('[道路地名] 缓存加载失败: $e');
  }
}
```

## 修复效果

### 修复前
- ❌ 从缓存只加载level≤1的数据
- ❌ 切换到level≤2时无法显示level=2的数据
- ❌ 离线模式下功能受限

### 修复后
- ✅ 从缓存加载所有level的数据
- ✅ 切换到level≤2时正确显示level=2的数据
- ✅ 离线模式下完整可用
- ✅ level切换逻辑正常工作

## 测试步骤

1. **在线模式测试**
   ```
   - 打开地图中心
   - 点击"显示道路和地名"按钮
   - 观察控制台输出:应该显示所有数据已加载
   - 多次点击按钮切换level: 1 → 2 → 3 → 0 → 1
   - 确认每个level都正确显示对应数据
   ```

2. **离线模式测试**
   ```
   - 先在线加载一次数据
   - 关闭网络
   - 重新打开地图中心
   - 点击"显示道路和地名"按钮
   - 应该从缓存加载所有数据
   - 切换level,确认level=2的数据能正常显示
   ```

3. **验证日志**
   ```
   查看控制台输出:
   [道路地名] 从缓存加载: 道路XX条, 地名XX条
   [Level过滤] 当前level=2, 显示道路XX条, 地名XX条
   ```

## 相关文件

- [lib/utils/db_helper.dart](file:///Users/pan3dme/Desktop/oframe2025/flutter/001/lib/utils/db_helper.dart)
  - 新增`getAllRoutes()`方法
  - 新增`getAllPlaces()`方法
  
- [lib/pages/map_center_page.dart](file:///Users/pan3dme/Desktop/oframe2025/flutter/001/lib/pages/map_center_page.dart)
  - 修改`_loadFromCache()`方法使用新的getAll方法

## 技术要点

### 两层过滤机制

1. **数据库层** - 不再过滤,返回所有数据
   - `getAllRoutes()` - 无WHERE条件
   - `getAllPlaces()` - 无WHERE条件

2. **内存层** - 根据当前level动态过滤
   - `_filterDataByLevel()` - 遍历_allRouteData和_allPlaceData
   - 根据每个对象的attributes.level字段判断
   - 将符合条件的数据放入_displayedRouteData和_displayedPlaceData

### 优势

- ✅ **灵活性高** - 切换level无需重新查询数据库
- ✅ **性能好** - 内存过滤比数据库查询快
- ✅ **数据完整** - 缓存中包含所有数据
- ✅ **离线友好** - 离线时也能完整使用level切换功能
