import 'dart:math';

/// 坐标系转换工具类
/// 
/// WGS-84: GPS原始坐标（国际标准）
/// GCJ-02: 火星坐标（中国国家测绘局标准，高德、腾讯地图使用）
/// BD-09: 百度坐标（百度地图使用）
class CoordTransform {
  static const double pi = 3.1415926535897932384626;
  static const double a = 6378245.0; // 长半轴
  static const double ee = 0.00669342162296594323; // 偏心率平方

  /// WGS-84 转 GCJ-02(火星坐标)
  /// 用于:GPS坐标 → 高德/腾讯地图
  /// 返回: [latitude, longitude]
  static List<double> wgs84ToGcj02(double lat, double lon) {
    if (_outOfChina(lat, lon)) {
      return [lat, lon];
    }
      
    double dLat = _transformLat(lon - 105.0, lat - 35.0);
    double dLon = _transformLon(lon - 105.0, lat - 35.0);
    double radLat = lat / 180.0 * pi;
    double magic = sin(radLat);
    magic = 1 - ee * magic * magic;
    double sqrtMagic = sqrt(magic);
    dLat = (dLat * 180.0) / ((a * (1 - ee)) / (magic * sqrtMagic) * pi);
    dLon = (dLon * 180.0) / (a / sqrtMagic * cos(radLat) * pi);
      
    return [lat + dLat, lon + dLon];
  }
  
  /// GCJ-02 转 WGS-84
  /// 用于:高德/腾讯地图 → GPS坐标
  /// 返回: [latitude, longitude]
  static List<double> gcj02ToWgs84(double lat, double lon) {
    if (_outOfChina(lat, lon)) {
      return [lat, lon];
    }
      
    double dLat = _transformLat(lon - 105.0, lat - 35.0);
    double dLon = _transformLon(lon - 105.0, lat - 35.0);
    double radLat = lat / 180.0 * pi;
    double magic = sin(radLat);
    magic = 1 - ee * magic * magic;
    double sqrtMagic = sqrt(magic);
    dLat = (dLat * 180.0) / ((a * (1 - ee)) / (magic * sqrtMagic) * pi);
    dLon = (dLon * 180.0) / (a / sqrtMagic * cos(radLat) * pi);
      
    return [lat - dLat, lon - dLon];
  }
  
  /// GCJ-02 转 BD-09(百度坐标)
  /// 返回: [latitude, longitude]
  static List<double> gcj02ToBd09(double lat, double lon) {
    double x = lon;
    double y = lat;
    double z = sqrt(x * x + y * y) + 0.00002 * sin(y * pi * 3000.0 / 180.0);
    double theta = atan2(y, x) + 0.000003 * cos(x * pi * 3000.0 / 180.0);
    double bdLon = z * cos(theta) + 0.0065;
    double bdLat = z * sin(theta) + 0.006;
    return [bdLat, bdLon];
  }
  
  /// BD-09 转 GCJ-02
  /// 返回: [latitude, longitude]
  static List<double> bd09ToGcj02(double lat, double lon) {
    double x = lon - 0.0065;
    double y = lat - 0.006;
    double z = sqrt(x * x + y * y) - 0.00002 * sin(y * pi * 3000.0 / 180.0);
    double theta = atan2(y, x) - 0.000003 * cos(x * pi * 3000.0 / 180.0);
    double gcjLon = z * cos(theta);
    double gcjLat = z * sin(theta);
    return [gcjLat, gcjLon];
  }
  
  /// WGS-84 转 BD-09
  /// 返回: [latitude, longitude]
  static List<double> wgs84ToBd09(double lat, double lon) {
    final gcj02 = wgs84ToGcj02(lat, lon);
    return gcj02ToBd09(gcj02[0], gcj02[1]);
  }
  
  /// BD-09 转 WGS-84
  /// 返回: [latitude, longitude]
  static List<double> bd09ToWgs84(double lat, double lon) {
    final gcj02 = bd09ToGcj02(lat, lon);
    return gcj02ToWgs84(gcj02[0], gcj02[1]);
  }

  /// 判断是否在中国境内
  static bool _outOfChina(double lat, double lon) {
    if (lon < 72.004 || lon > 137.8347) return true;
    if (lat < 0.8293 || lat > 55.8271) return true;
    return false;
  }

  /// 转换纬度
  static double _transformLat(double x, double y) {
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 
        0.1 * x * y + 0.2 * sqrt(x.abs());
    ret += (20.0 * sin(6.0 * x * pi) + 20.0 * sin(2.0 * x * pi)) * 2.0 / 3.0;
    ret += (20.0 * sin(y * pi) + 40.0 * sin(y / 3.0 * pi)) * 2.0 / 3.0;
    ret += (160.0 * sin(y / 12.0 * pi) + 320 * sin(y * pi / 30.0)) * 2.0 / 3.0;
    return ret;
  }

  /// 转换经度
  static double _transformLon(double x, double y) {
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 
        0.1 * x * y + 0.1 * sqrt(x.abs());
    ret += (20.0 * sin(6.0 * x * pi) + 20.0 * sin(2.0 * x * pi)) * 2.0 / 3.0;
    ret += (20.0 * sin(x * pi) + 40.0 * sin(x / 3.0 * pi)) * 2.0 / 3.0;
    ret += (150.0 * sin(x / 12.0 * pi) + 300.0 * sin(x / 30.0 * pi)) * 2.0 / 3.0;
    return ret;
  }
}


