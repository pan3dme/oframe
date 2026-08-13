/// 天地图瓦片配置
/// 所有地图页面共享此配置，切换瓦片源时只需修改此处
import 'db_helper.dart';

class MapTileConfig {
  /// 天地图 API token（用户可在设置页面修改）
  static String tiandituToken = '77d9737d1e8b6f1ea2b8b8a6c3c56e89';

  /// 天地图卫星影像瓦片 URL
  static String get tiandituImgUrl =>
      'https://t{s}.tianditu.gov.cn/DataServer?T=img_w&x={x}&y={y}&l={z}&tk=$tiandituToken';

  /// 天地图影像注记瓦片 URL（叠加在卫星图上显示地名标注）
  static String get tiandituAnnotationUrl =>
      'https://t{s}.tianditu.gov.cn/DataServer?T=cia_w&x={x}&y={y}&l={z}&tk=$tiandituToken';

  /// 天地图矢量地图瓦片 URL
  static String get tiandituVecUrl =>
      'https://t{s}.tianditu.gov.cn/DataServer?T=vec_w&x={x}&y={y}&l={z}&tk=$tiandituToken';

  /// 天地图矢量注记瓦片 URL
  static String get tiandituVecAnnotationUrl =>
      'https://t{s}.tianditu.gov.cn/DataServer?T=cva_w&x={x}&y={y}&l={z}&tk=$tiandituToken';

  /// 天地图子域名
  static const List<String> subdomains = ['1', '2', '3', '4', '5', '6', '7'];

  /// 最大原生缩放级别
  static const int maxNativeZoom = 18;

  /// 最小缩放级别
  static const double minZoom = 3.0;

  /// 最大缩放级别
  static const double maxZoom = 19.0;

  /// 从数据库加载 token（每个地图页面 initState 时调用）
  static Future<void> loadToken() async {
    try {
      final saved = await DBHelper().getSetting('tianditu_token');
      if (saved != null && saved.isNotEmpty) {
        tiandituToken = saved;
      }
    } catch (_) {}
  }
}
