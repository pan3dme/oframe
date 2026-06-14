import 'dart:convert';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:photo_view/photo_view.dart';
import 'package:video_player/video_player.dart';
import 'package:image_picker/image_picker.dart';
import 'package:image/image.dart' as img;
import 'package:dio/dio.dart';

/// FC 地址常量
const String _cowSheepVideoFcUrl = 'https://gpsmoveinfo.cn/fc/cowsheep';

/// 牛羊详情页面
class LivestockDetailPage extends StatefulWidget {
  final Map<String, dynamic> livestock;

  const LivestockDetailPage({
    super.key,
    required this.livestock,
  });

  @override
  State<LivestockDetailPage> createState() => _LivestockDetailPageState();
}

class _LivestockDetailPageState extends State<LivestockDetailPage> {
  List<Map<String, dynamic>> _videoList = [];
  bool _isLoadingVideos = false;
  String _videoErrorMessage = '';
  bool _isUploading = false;

  @override
  void initState() {
    super.initState();
    _loadVideoList();
  }

  /// 加载视频图片列表
  Future<void> _loadVideoList() async {
    final cowsheepId = widget.livestock['cowsheep_id'];
    if (cowsheepId == null || cowsheepId.toString().isEmpty) {
      return;
    }

    setState(() {
      _isLoadingVideos = true;
      _videoErrorMessage = '';
    });

    try {
      debugPrint('[视频列表] 开始请求: cowsheep_id=$cowsheepId');
      
      final resp = await http.post(
        Uri.parse(_cowSheepVideoFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'getcowsheepvideo',
          'info': {
            'cowsheep_id': cowsheepId.toString(),
          },
        }),
      );

      debugPrint('[视频列表] 响应状态: ${resp.statusCode}');
      debugPrint('[视频列表] 响应体: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;
        
        if (json['status'] == 'success') {
          final rawRows = json['data'] as List<dynamic>;
          // 解析数据结构：primaryKey包含ossurl，attributes包含cowsheep_id和time
          final parsedData = rawRows.map((item) {
            final itemMap = item as Map<String, dynamic>;
            
            // 提取ossurl（从primaryKey数组中）
            String ossurl = '';
            if (itemMap.containsKey('primaryKey')) {
              final primaryKeyList = itemMap['primaryKey'] as List<dynamic>;
              for (final pk in primaryKeyList) {
                final pkMap = pk as Map<String, dynamic>;
                if (pkMap['name'] == 'ossurl') {
                  ossurl = pkMap['value'] ?? '';
                  break;
                }
              }
            }
            
            // 提取cowsheep_id和time（从attributes数组中）
            String cowsheepId = '';
            String time = '';
            if (itemMap.containsKey('attributes')) {
              final attributesList = itemMap['attributes'] as List<dynamic>;
              for (final attr in attributesList) {
                final attrMap = attr as Map<String, dynamic>;
                if (attrMap['columnName'] == 'cowsheep_id') {
                  cowsheepId = attrMap['columnValue'] ?? '';
                } else if (attrMap['columnName'] == 'time') {
                  time = attrMap['columnValue'] ?? '';
                }
              }
            }
            
            return {
              'ossurl': ossurl,
              'cowsheep_id': cowsheepId,
              'time': time,
            };
          }).toList();
          
          setState(() {
            _videoList = parsedData;
            _isLoadingVideos = false;
            _videoErrorMessage = '';
          });
          
          debugPrint('[视频列表] ✓ 加载成功: ${parsedData.length} 条');
          if (parsedData.isNotEmpty) {
            debugPrint('[视频列表] 第一条数据: ${parsedData[0]}');
          }
        } else {
          debugPrint('[视频列表] 请求错误: ${json['msg']} ${json['error'] ?? ''}');
          setState(() {
            _videoErrorMessage = '请求错误: ${json['msg']}';
            _isLoadingVideos = false;
          });
        }
      } else {
        setState(() {
          _videoErrorMessage = 'HTTP ${resp.statusCode}';
          _isLoadingVideos = false;
        });
      }
    } catch (e, stackTrace) {
      debugPrint('[视频列表] 请求异常: $e');
      debugPrint('[视频列表] 堆栈: $stackTrace');
      setState(() {
        _videoErrorMessage = '连接失败: $e';
        _isLoadingVideos = false;
      });
    }
  }

  /// 从记录中安全取值，空值显示占位文字
  String _str(dynamic value) {
    if (value == null || value.toString().isEmpty) return '—';
    return value.toString();
  }

  /// 根据生日计算年龄
  String _calculateAge(String birthdayStr) {
    if (birthdayStr == '—' || birthdayStr.isEmpty) {
      return '—';
    }
    
    try {
      // 解析生日字符串，支持多种格式
      DateTime birthday;
      
      // 尝试解析常见格式
      if (birthdayStr.contains('/')) {
        // 格式: 2025/6/8 或 2025/06/08
        final parts = birthdayStr.split(' ')[0].split('/');
        if (parts.length >= 3) {
          birthday = DateTime(
            int.parse(parts[0]),
            int.parse(parts[1]),
            int.parse(parts[2]),
          );
        } else {
          return '—';
        }
      } else if (birthdayStr.contains('-')) {
        // 格式: 2025-06-08
        final parts = birthdayStr.split(' ')[0].split('-');
        if (parts.length >= 3) {
          birthday = DateTime(
            int.parse(parts[0]),
            int.parse(parts[1]),
            int.parse(parts[2]),
          );
        } else {
          return '—';
        }
      } else {
        return '—';
      }
      
      final now = DateTime.now();
      final difference = now.difference(birthday);
      final days = difference.inDays;
      
      if (days < 0) {
        return '未出生';
      }
      
      // 计算年龄
      if (days < 30) {
        // 小于1个月，显示天数
        return '${days}天';
      } else if (days < 365) {
        // 小于1年，显示月份
        final months = (days / 30).floor();
        return '${months}个月';
      } else {
        // 大于等于1年，显示年份
        final years = days ~/ 365;
        final remainingDays = days % 365;
        final months = (remainingDays / 30).floor();
        
        if (months == 0) {
          return '${years}年';
        } else if (months == 6) {
          return '${years}年半';
        } else {
          return '${years}年${months}个月';
        }
      }
    } catch (e) {
      debugPrint('[年龄计算] 解析失败: $e, 生日: $birthdayStr');
      return '—';
    }
  }

  @override
  Widget build(BuildContext context) {
    final cowsheepId = _str(widget.livestock['cowsheep_id']);
    final birthday = _str(widget.livestock['birthday']);
    final age = _calculateAge(birthday);
    final gender = widget.livestock['gender'] == true ? '公' : (widget.livestock['gender'] == false ? '母' : '—');
    final avatar = _str(widget.livestock['avatar']);
    final rename = _str(widget.livestock['rename']);
    final hasImage = avatar != '—' && avatar.isNotEmpty;

    return Scaffold(
      appBar: AppBar(
        title: Text('牛羊详情 - $cowsheepId'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 上半部分：左侧图像 + 右侧基本信息
            Card(
              elevation: 2,
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(12),
              ),
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    // 左侧：图像
                    GestureDetector(
                      onTap: hasImage ? () => _showImagePreview(avatar) : null,
                      child: ClipRRect(
                        borderRadius: BorderRadius.circular(10),
                        child: hasImage
                            ? Image.network(
                                avatar,
                                width: 150,
                                height: 150,
                                fit: BoxFit.cover,
                                errorBuilder: (context, error, stackTrace) {
                                  return _buildImagePlaceholder();
                                },
                              )
                            : _buildImagePlaceholder(),
                      ),
                    ),
                    const SizedBox(width: 16),
                    // 右侧：基本信息
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            rename != '—' ? rename : cowsheepId,
                            style: const TextStyle(
                              fontSize: 16,
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                          const SizedBox(height: 8),
                          const Divider(height: 16),
                          _infoRow(Icons.tag, 'ID', cowsheepId),
                          _infoRow(Icons.edit_note, '名称', rename),
                          _infoRow(Icons.timeline, '年龄', age),
                          _infoRow(
                            gender == '公' ? Icons.male : (gender == '母' ? Icons.female : Icons.help_outline),
                            '性别',
                            gender,
                            valueColor: gender != '—' ? (gender == '公' ? Colors.blue : Colors.pink) : null,
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ),
            
            const SizedBox(height: 16),
            
            // 下半部分：照片视频资料
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text(
                  '照片视频资料',
                  style: TextStyle(
                    fontSize: 18,
                    fontWeight: FontWeight.bold,
                  ),
                ),
                IconButton(
                  icon: _isUploading
                      ? const SizedBox(
                          width: 20,
                          height: 20,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.add_photo_alternate),
                  onPressed: _isUploading ? null : _uploadMedia,
                  tooltip: '上传图片/视频',
                ),
              ],
            ),
            const SizedBox(height: 12),
            _buildVideoSection(),
          ],
        ),
      ),
    );
  }

  /// 信息行组件（标签和值在同一行）
  Widget _infoRow(
    IconData icon,
    String label,
    String value, {
    Color? valueColor,
  }) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Row(
        children: [
          Icon(icon, size: 16, color: Colors.grey[600]),
          const SizedBox(width: 8),
          Text(
            '$label: ',
            style: TextStyle(
              fontSize: 13,
              color: Colors.grey[700],
              fontWeight: FontWeight.w500,
            ),
          ),
          Expanded(
            child: Text(
              value,
              style: TextStyle(
                fontSize: 13,
                color: valueColor ?? Colors.black87,
                fontWeight: valueColor != null ? FontWeight.w600 : FontWeight.normal,
              ),
            ),
          ),
        ],
      ),
    );
  }

  /// 图片占位符
  Widget _buildImagePlaceholder() {
    return Container(
      width: 150,
      height: 150,
      decoration: BoxDecoration(
        color: Colors.grey.shade200,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Icon(Icons.image_not_supported, size: 48, color: Colors.grey[400]),
    );
  }

  /// 构建视频图片区域
  Widget _buildVideoSection() {
    if (_isLoadingVideos) {
      return const Center(
        child: Padding(
          padding: EdgeInsets.all(32),
          child: CircularProgressIndicator(),
        ),
      );
    }

    if (_videoErrorMessage.isNotEmpty) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(
            children: [
              Icon(Icons.error_outline, size: 48, color: Colors.red[300]),
              const SizedBox(height: 8),
              Text(
                _videoErrorMessage,
                style: TextStyle(color: Colors.grey[600]),
              ),
            ],
          ),
        ),
      );
    }

    if (_videoList.isEmpty) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(
            children: [
              Icon(Icons.photo_library_outlined, size: 48, color: Colors.grey[400]),
              const SizedBox(height: 8),
              Text(
                '暂无照片视频资料',
                style: TextStyle(color: Colors.grey[600]),
              ),
            ],
          ),
        ),
      );
    }

    // 显示图片网格（最新在上）
    return GridView.builder(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 3,
        crossAxisSpacing: 8,
        mainAxisSpacing: 8,
        childAspectRatio: 1.0,
      ),
      itemCount: _videoList.length,
      itemBuilder: (context, index) {
        final item = _videoList[index];
        final ossurl = item['ossurl'] ?? '';
        final time = item['time'] ?? '';
        final isVideo = ossurl.toLowerCase().endsWith('.mp4') || 
                        ossurl.toLowerCase().endsWith('.avi') || 
                        ossurl.toLowerCase().endsWith('.mov');
        
        return GestureDetector(
          onTap: () {
            if (isVideo) {
              _playVideo(ossurl);
            } else {
              _showImagePreview(ossurl);
            }
          },
          child: Card(
            elevation: 2,
            clipBehavior: Clip.antiAlias,
            child: Stack(
              fit: StackFit.expand,
              children: [
                ossurl.isNotEmpty
                    ? isVideo
                        ? _buildVideoThumbnail(ossurl)
                        : Image.network(
                            ossurl,
                            fit: BoxFit.cover,
                            loadingBuilder: (context, child, loadingProgress) {
                              if (loadingProgress == null) return child;
                              return Container(
                                color: Colors.grey.shade200,
                                child: Center(
                                  child: CircularProgressIndicator(
                                    value: loadingProgress.expectedTotalBytes != null
                                        ? loadingProgress.cumulativeBytesLoaded /
                                            loadingProgress.expectedTotalBytes!
                                        : null,
                                  ),
                                ),
                              );
                            },
                            errorBuilder: (context, error, stackTrace) {
                              return Container(
                                color: Colors.grey.shade200,
                                child: Icon(Icons.broken_image, color: Colors.grey[400]),
                              );
                            },
                          )
                    : Container(
                        color: Colors.grey.shade200,
                        child: Icon(Icons.image_not_supported, color: Colors.grey[400]),
                      ),
                // 视频图标标识
                if (isVideo)
                  Positioned(
                    top: 4,
                    right: 4,
                    child: Container(
                      padding: const EdgeInsets.all(4),
                      decoration: BoxDecoration(
                        color: Colors.black.withOpacity(0.6),
                        shape: BoxShape.circle,
                      ),
                      child: const Icon(
                        Icons.play_circle_fill,
                        color: Colors.white,
                        size: 20,
                      ),
                    ),
                  ),
                // 底部显示时间
                Positioned(
                  bottom: 0,
                  left: 0,
                  right: 0,
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 2),
                    color: Colors.black.withOpacity(0.6),
                    child: Text(
                      time,
                      style: const TextStyle(
                        color: Colors.white,
                        fontSize: 10,
                      ),
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }

  /// 显示图片预览（可放大）
  void _showImagePreview(String imageUrl) {
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (context) => Scaffold(
          backgroundColor: Colors.black,
          appBar: AppBar(
            backgroundColor: Colors.transparent,
            elevation: 0,
            iconTheme: const IconThemeData(color: Colors.white),
          ),
          body: PhotoView(
            imageProvider: NetworkImage(imageUrl),
            minScale: PhotoViewComputedScale.contained,
            maxScale: PhotoViewComputedScale.covered * 2,
            initialScale: PhotoViewComputedScale.contained,
            backgroundDecoration: const BoxDecoration(color: Colors.black),
          ),
        ),
      ),
    );
  }

  /// 播放视频
  void _playVideo(String videoUrl) {
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (context) => VideoPlayerPage(videoUrl: videoUrl),
      ),
    );
  }

  /// 构建视频缩略图（显示播放图标和渐变背景）
  Widget _buildVideoThumbnail(String videoUrl) {
    return Container(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            Colors.grey.shade700,
            Colors.grey.shade900,
          ],
        ),
      ),
      child: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(
              Icons.videocam,
              size: 48,
              color: Colors.white.withOpacity(0.8),
            ),
            const SizedBox(height: 8),
            Text(
              '视频',
              style: TextStyle(
                color: Colors.white.withOpacity(0.8),
                fontSize: 12,
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// 上传图片/视频
  Future<void> _uploadMedia() async {
    debugPrint('[上传] 开始选择图片来源');
    
    // 显示选择对话框
    final source = await showDialog<ImageSource>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('选择图片来源'),
        content: const Text('请选择拍照或从相册选择'),
        actions: [
          TextButton(
            onPressed: () {
              debugPrint('[上传] 用户选择拍照');
              Navigator.pop(context, ImageSource.camera);
            },
            child: const Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(Icons.camera_alt),
                SizedBox(width: 8),
                Text('拍照'),
              ],
            ),
          ),
          TextButton(
            onPressed: () {
              debugPrint('[上传] 用户选择相册');
              Navigator.pop(context, ImageSource.gallery);
            },
            child: const Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(Icons.photo_library),
                SizedBox(width: 8),
                Text('相册'),
              ],
            ),
          ),
          TextButton(
            onPressed: () {
              debugPrint('[上传] 用户取消');
              Navigator.pop(context);
            },
            child: const Text('取消'),
          ),
        ],
      ),
    );

    if (source == null) {
      debugPrint('[上传] 用户取消了选择');
      return;
    }

    try {
      debugPrint('[上传] 开始选择图片，来源: $source');

      // 选择图片/视频
      final picker = ImagePicker();
      final XFile? pickedFile = await picker.pickImage(
        source: source,
        maxWidth: 512,
        maxHeight: 512,
        imageQuality: 85,
        preferredCameraDevice: CameraDevice.rear, // 使用后置摄像头
      );

      if (pickedFile == null) {
        debugPrint('[上传] 用户取消了选择');
        return;
      }

      debugPrint('[上传] 文件路径: ${pickedFile.path}');

      setState(() {
        _isUploading = true;
      });

      // 压缩图片到 512x512
      final compressedFile = await _compressImage(pickedFile.path);
      
      if (compressedFile == null) {
        throw Exception('图片压缩失败');
      }

      debugPrint('[上传] 压缩完成，开始上传到OSS');

      // 第一步：上传到OSS获取URL
      final ossUrl = await _uploadToOSS(compressedFile);
      
      if (ossUrl.isEmpty) {
        throw Exception('OSS上传失败，未返回URL');
      }

      debugPrint('[上传] OSS上传成功: $ossUrl');

      // 第二步：提交到服务器
      await _submitToServer(ossUrl);

      // 刷新列表
      await _loadVideoList();

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('上传成功'),
            backgroundColor: Colors.green,
          ),
        );
      }
    } catch (e) {
      debugPrint('[上传] 失败: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('上传失败: $e'),
            backgroundColor: Colors.red,
          ),
        );
      }
    } finally {
      if (mounted) {
        setState(() {
          _isUploading = false;
        });
      }
    }
  }

  /// 压缩图片到 512x512
  Future<File?> _compressImage(String imagePath) async {
    try {
      // 读取图片
      final imageFile = File(imagePath);
      final bytes = await imageFile.readAsBytes();
      
      // 解码图片
      img.Image? image = img.decodeImage(bytes);
      if (image == null) return null;

      // 调整尺寸到 512x512
      img.Image resized = img.copyResize(
        image,
        width: 512,
        height: 512,
        interpolation: img.Interpolation.linear,
      );

      // 编码为 JPEG
      final compressedBytes = img.encodeJpg(resized, quality: 85);

      // 保存到临时文件
      final tempDir = await Directory.systemTemp.createTemp('upload_');
      final tempFile = File('${tempDir.path}/compressed.jpg');
      await tempFile.writeAsBytes(compressedBytes);

      debugPrint('[压缩] 原始大小: ${bytes.length}, 压缩后: ${compressedBytes.length}');

      return tempFile;
    } catch (e) {
      debugPrint('[压缩] 失败: $e');
      return null;
    }
  }

  /// 上传到OSS
  Future<String> _uploadToOSS(File file) async {
    try {
      final cowsheepId = widget.livestock['cowsheep_id'];
      if (cowsheepId == null || cowsheepId.toString().isEmpty) {
        throw Exception('牛羊ID为空');
      }

      // 生成文件名
      final timestamp = DateTime.now().millisecondsSinceEpoch;
      final fileName = 'livestock/${cowsheepId}_$timestamp.jpg';
      
      debugPrint('[OSS上传] 文件名: $fileName');
      debugPrint('[OSS上传] 文件大小: ${file.lengthSync()} bytes');

      // TODO: 这里需要调用后端接口获取 OSS 上传凭证
      // 因为直接在前端使用 AccessKey 不安全
      // 建议后端提供一个接口返回 STS 临时凭证或签名 URL
      
      // 方案1: 通过后端获取签名 URL（推荐）
      final signedUrl = await _getOssSignedUrl(fileName);
      
      if (signedUrl.isEmpty) {
        throw Exception('获取OSS签名URL失败');
      }
      
      debugPrint('[OSS上传] 签名URL: $signedUrl');
      
      // 使用签名 URL 直接上传到 OSS
      final dio = Dio();
      final response = await dio.put(
        signedUrl,
        data: await file.readAsBytes(),
        options: Options(
          headers: {
            'Content-Type': 'image/jpeg',
          },
        ),
      );
      
      debugPrint('[OSS上传] 响应: ${response.statusCode}');
      
      if (response.statusCode != 200) {
        throw Exception('OSS上传失败: HTTP ${response.statusCode}');
      }
      
      // 构造完整的 OSS URL
      final ossUrl = 'https://cowsheep-resource.oss-cn-shanghai.aliyuncs.com/$fileName';
      
      debugPrint('[OSS上传] 成功: $ossUrl');
      return ossUrl;
      
    } catch (e) {
      debugPrint('[OSS上传] 错误: $e');
      rethrow;
    }
  }
  
  /// 从后端获取 OSS 签名 URL
  Future<String> _getOssSignedUrl(String fileName) async {
    try {
      debugPrint('[获取签名] 开始请求，文件名: $fileName');
      
      final dio = Dio();
      final response = await dio.post(
        'https://gpsmoveinfo.cn/fc/signature',
        data: jsonEncode({
          'action': 'getOssSignature',
          'fileName': fileName,
        }),
        options: Options(
          headers: {'Content-Type': 'application/json'},
        ),
      );
      
      debugPrint('[获取签名] 响应状态码: ${response.statusCode}');
      debugPrint('[获取签名] 响应数据: ${response.data}');
      
      if (response.statusCode == 200) {
        final data = response.data as Map<String, dynamic>;
        debugPrint('[获取签名] 解析后的数据: $data');
        
        if (data['status'] == 'success') {
          // 根据实际后端返回格式调整
          final signedUrl = data['data']?['signedUrl'] ?? data['signedUrl'] ?? '';
          debugPrint('[获取签名] 提取的签名URL: $signedUrl');
          return signedUrl;
        } else {
          debugPrint('[获取签名] 状态不是success: ${data['msg']}');
        }
      } else {
        debugPrint('[获取签名] HTTP状态码异常: ${response.statusCode}');
      }
      
      return '';
    } catch (e, stackTrace) {
      debugPrint('[获取签名] 异常: $e');
      debugPrint('[获取签名] 堆栈: $stackTrace');
      return '';
    }
  }

  /// 提交到服务器
  Future<void> _submitToServer(String ossUrl) async {
    try {
      final cowsheepId = widget.livestock['cowsheep_id'];
      if (cowsheepId == null || cowsheepId.toString().isEmpty) {
        throw Exception('牛羊ID为空');
      }

      final now = DateTime.now();
      final timeStr = '${now.year}/${now.month}/${now.day} ${now.hour}:${now.minute}:${now.second}';

      final dio = Dio();
      final response = await dio.post(
        _cowSheepVideoFcUrl,
        data: jsonEncode({
          'action': 'uploadLivestockPhoto',
          'info': {
            'ossUrl': ossUrl,
            'cowsheep_id': cowsheepId.toString(),
            'time': timeStr,
          },
        }),
        options: Options(
          headers: {'Content-Type': 'application/json'},
        ),
      );

      debugPrint('[提交] 响应: ${response.statusCode}, ${response.data}');

      if (response.statusCode != 200) {
        throw Exception('提交失败: HTTP ${response.statusCode}');
      }

      final responseData = response.data as Map<String, dynamic>;
      if (responseData['status'] != 'success') {
        throw Exception(responseData['msg'] ?? '提交失败');
      }
    } catch (e) {
      debugPrint('[提交] 错误: $e');
      rethrow;
    }
  }
}

/// 视频播放页面
class VideoPlayerPage extends StatefulWidget {
  final String videoUrl;

  const VideoPlayerPage({super.key, required this.videoUrl});

  @override
  State<VideoPlayerPage> createState() => _VideoPlayerPageState();
}

class _VideoPlayerPageState extends State<VideoPlayerPage> {
  late VideoPlayerController _controller;
  bool _isInitialized = false;
  String _errorMessage = '';

  @override
  void initState() {
    super.initState();
    _initializeVideo();
  }

  Future<void> _initializeVideo() async {
    try {
      debugPrint('[视频播放] 开始初始化: ${widget.videoUrl}');
      
      // iOS 兼容：不使用 videoPlayerOptions，避免 channel 错误
      _controller = VideoPlayerController.networkUrl(
        Uri.parse(widget.videoUrl),
      );
      
      await _controller.initialize();
      
      debugPrint('[视频播放] 初始化成功，时长: ${_controller.value.duration}');
      debugPrint('[视频播放] 宽高比: ${_controller.value.aspectRatio}');
      debugPrint('[视频播放] 是否已初始化: ${_controller.value.isInitialized}');
      
      if (mounted) {
        setState(() {
          _isInitialized = true;
          _errorMessage = '';
        });
        
        // 延迟播放，确保UI已渲染
        Future.delayed(const Duration(milliseconds: 300), () {
          if (mounted && _controller.value.isInitialized) {
            _controller.play();
            setState(() {});
            debugPrint('[视频播放] 开始播放');
          }
        });
      }
    } catch (e, stackTrace) {
      debugPrint('[视频播放] 初始化失败: $e');
      debugPrint('[视频播放] 堆栈: $stackTrace');
      if (mounted) {
        setState(() {
          _errorMessage = '视频加载失败: $e';
        });
      }
    }
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        elevation: 0,
        iconTheme: const IconThemeData(color: Colors.white),
      ),
      body: Center(
        child: _buildVideoContent(),
      ),
    );
  }

  Widget _buildVideoContent() {
    if (_errorMessage.isNotEmpty) {
      return Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(Icons.error_outline, size: 64, color: Colors.red[300]),
            const SizedBox(height: 16),
            Text(
              _errorMessage,
              style: const TextStyle(color: Colors.white, fontSize: 16),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 16),
            ElevatedButton(
              onPressed: () {
                setState(() {
                  _isInitialized = false;
                  _errorMessage = '';
                });
                _initializeVideo();
              },
              child: const Text('重试'),
            ),
          ],
        ),
      );
    }

    if (!_isInitialized) {
      return const Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          CircularProgressIndicator(color: Colors.white),
          SizedBox(height: 16),
          Text(
            '加载中...',
            style: TextStyle(color: Colors.white),
          ),
        ],
      );
    }

    // iOS 兼容：使用 Stack 包裹 VideoPlayer
    return Stack(
      fit: StackFit.expand,
      children: [
        Center(
          child: AspectRatio(
            aspectRatio: _controller.value.aspectRatio > 0 
                ? _controller.value.aspectRatio 
                : 16 / 9,
            child: VideoPlayer(_controller),
          ),
        ),
        // 播放控制层
        Positioned(
          bottom: 0,
          left: 0,
          right: 0,
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            decoration: BoxDecoration(
              gradient: LinearGradient(
                begin: Alignment.bottomCenter,
                end: Alignment.topCenter,
                colors: [
                  Colors.black.withOpacity(0.7),
                  Colors.transparent,
                ],
              ),
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                // 进度条
                VideoProgressIndicator(
                  _controller,
                  allowScrubbing: true,
                  colors: const VideoProgressColors(
                    playedColor: Colors.blue,
                    bufferedColor: Colors.grey,
                    backgroundColor: Colors.white30,
                  ),
                ),
                const SizedBox(height: 8),
                // 控制按钮
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    IconButton(
                      icon: Icon(
                        _controller.value.isPlaying
                            ? Icons.pause_circle_filled
                            : Icons.play_circle_filled,
                        size: 48,
                        color: Colors.white,
                      ),
                      onPressed: () {
                        setState(() {
                          if (_controller.value.isPlaying) {
                            _controller.pause();
                          } else {
                            _controller.play();
                          }
                        });
                      },
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}
