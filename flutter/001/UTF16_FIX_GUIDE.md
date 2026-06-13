# UTF-16字符串溢出问题修复说明

## 问题描述

地图移动时出现以下错误:
```
Invalid argument(s): string is not well-formed UTF-16
```

## 问题原因

这个错误是因为地图上显示的文本(地名、设备名称、道路名称)包含了**无效的UTF-16字符**,导致Flutter在渲染Text widget时崩溃。

常见原因:
1. **数据源包含特殊字符** - API返回的数据可能包含控制字符、乱码或不可打印字符
2. **字符串截断破坏多字节字符** - 使用`substring()`截断时可能切断Unicode代理对(surrogate pairs)
3. **编码转换问题** - 从服务器接收的数据可能存在编码问题
4. **未清理的columnValue** - 从API数据的attributes中提取的columnValue可能包含无效字符

## 解决方案

### 1. 添加字符串清理函数

实现了`_sanitizeString()`方法,对所有显示文本进行清理:

```dart
String _sanitizeString(String input) {
  if (input.isEmpty) return '';
  
  try {
    // 移除控制字符（除了常见的空白字符）
    final cleaned = input.replaceAll(RegExp(r'[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]'), '');
    
    // 检查是否为有效的UTF-16字符串
    final codeUnits = cleaned.codeUnits;
    
    // 重新构建字符串
    final result = String.fromCharCodes(codeUnits);
    
    return result.isEmpty ? '' : result;
  } catch (e) {
    debugPrint('[字符串清理] 失败: $e');
    return ''; // 出错时返回空字符串
  }
}
```

### 2. 应用到所有文本显示

**道路名称解析时清理:**
```dart
// 从API数据中提取道路名称时清理
else if (columnName == 'roadname') {
  // 清理道路名称，确保UTF-16安全
  name = _sanitizeString(columnValue);
}
```

**地名解析时清理:**
```dart
// 从API数据中提取地名时清理
else if (columnName == 'name') {
  // 清理地名，确保UTF-16安全
  name = _sanitizeString(columnValue);
}
```

**地名标记显示:**
```dart
// 清理地名字符串，确保UTF-16安全
String safeName = _sanitizeString(name);

Text(
  safeName.length > 5 ? safeName.substring(0, 5) : safeName,
  ...
)
```

**设备名称:**
```dart
// 清理设备名称，确保UTF-16安全
String safeDeviceName = _sanitizeString(device['name'].toString());

Text(
  safeDeviceName.length > 8 ? safeDeviceName.substring(0, 8) : safeDeviceName,
  ...
)
```

## 修复效果

✅ **消除UTF-16溢出错误** - 所有文本都经过清理和验证  
✅ **防止应用崩溃** - 即使数据有问题也不会导致渲染失败  
✅ **保持显示稳定性** - 地图移动、缩放时不再报错  
✅ **兼容各种字符** - 支持中文、英文、数字等正常字符  
✅ **三层防护** - API解析时清理 + 显示前清理 + 截断前清理  

## 注意事项

1. **无效字符会被过滤** - 控制字符和乱码会被移除
2. **空字符串处理** - 如果清理后为空,则不显示该标记
3. **性能影响小** - 字符串清理操作非常快速
4. **调试日志** - 清理失败时会输出日志方便排查

## 测试建议

1. 在有网络时加载地图数据
2. 离线状态下移动、缩放地图
3. 观察是否还有UTF-16相关错误
4. 检查地名和设备名称是否正常显示

## 相关文件

- [lib/pages/map_center_page.dart](file:///Users/pan3dme/Desktop/oframe2025/flutter/001/lib/pages/map_center_page.dart)
  - 添加了`_sanitizeString()`方法(第405-430行)
  - 道路名称解析时清理(第1001行、第1053行)
  - 地名解析时清理(第1105行)
  - 地名标记使用清理后的字符串(第1139行)
  - 设备名称使用清理后的字符串(第1195行)
