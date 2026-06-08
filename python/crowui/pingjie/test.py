import cv2
import glob

def check_image_pair_matching(img1, img2, idx1, idx2):
    """检查两张图片是否能匹配"""
    # 使用SIFT特征检测器
    sift = cv2.SIFT_create()
    
    # 检测关键点和描述符
    kp1, des1 = sift.detectAndCompute(img1, None)
    kp2, des2 = sift.detectAndCompute(img2, None)
    
    if des1 is None or des2 is None:
        return False, 0, "无法提取特征点"
    
    # 使用FLANN匹配器
    FLANN_INDEX_KDTREE = 1
    index_params = dict(algorithm=FLANN_INDEX_KDTREE, trees=5)
    search_params = dict(checks=50)
    flann = cv2.FlannBasedMatcher(index_params, search_params)
    
    try:
        matches = flann.knnMatch(des1, des2, k=2)
    except:
        return False, 0, "匹配过程出错"
    
    # 应用Lowe's比率测试
    good_matches = []
    for m, n in matches:
        if m.distance < 0.7 * n.distance:
            good_matches.append(m)
    
    match_count = len(good_matches)
    min_matches = 10  # 最小匹配点数
    
    if match_count < min_matches:
        return False, match_count, f"匹配点不足（需要至少{min_matches}个，实际{match_count}个）"
    
    return True, match_count, f"匹配成功（{match_count}个匹配点）"

def stitch_images(image_paths, output_path='result.jpg'):
    # 读取所有图片并检查
    images = []
    valid_images = []
    
    print(f"\n开始处理 {len(image_paths)} 张图片...")
    
    for i, path in enumerate(image_paths):
        img = cv2.imread(path)
        if img is None:
            print(f"❌ 错误：无法读取图片 {i+1}: {path}")
        else:
            print(f"✓ 成功读取图片 {i+1}: {path} (尺寸: {img.shape[1]}x{img.shape[0]})")
            images.append(img)
            valid_images.append(path)
    
    if len(images) < 2:
        print(f"\n错误：有效图片数量不足（当前: {len(images)}），至少需要2张图片")
        return
    
    print(f"\n开始检查图片之间的匹配关系...")
    print("="*60)
    
    # 检查相邻图片的匹配情况
    failed_pairs = []
    for i in range(len(images)-1):
        success, count, msg = check_image_pair_matching(images[i], images[i+1], i+1, i+2)
        status_icon = "✓" if success else "❌"
        print(f"{status_icon} 图片{i+1}和图片{i+2}: {msg}")
        if not success:
            failed_pairs.append((i+1, i+2, msg))
    
    print("="*60)
    
    if failed_pairs:
        print(f"\n⚠️  发现问题图片对：")
        for idx1, idx2, msg in failed_pairs:
            print(f"   图片{idx1} ({valid_images[idx1-1]}) 和 图片{idx2} ({valid_images[idx2-1]}) - {msg}")
        print(f"\n建议：检查这些图片是否有足够的重叠区域或内容相似度")
        return
    
    print(f"\n所有图片对匹配检查通过，开始拼接...")
    
    # 创建拼接器
    stitcher = cv2.Stitcher.create(cv2.Stitcher_PANORAMA)  # 也可用 SCANS 模式
    # 执行拼接
    status, pano = stitcher.stitch(images)

    if status == cv2.Stitcher_OK:
        cv2.imwrite(output_path, pano)
        print(f"\n✓ 拼接成功，保存为 {output_path}")
    else:
        error_messages = {
            0: "成功",
            1: "错误：需要更多图像（相机参数估计失败）",
            2: "错误：无法匹配图像",
            3: "错误：无法估计相机参数或匹配图像",
            4: "错误：无法调整相机参数",
            5: "错误：无法融合图像",
            6: "错误：输入图像无效",
            7: "错误：图像大小不匹配"
        }
        
        print(f"\n❌ 拼接失败！")
        print(f"错误码: {status}")
        print(f"错误描述: {error_messages.get(status, '未知错误')}")
        print(f"\n注意：虽然相邻图片对都通过了匹配检查，但整体拼接仍失败。")
        print(f"可能原因：")
        print(f"  1. 图片序列的拍摄角度变化过大")
        print(f"  2. 某些图片质量较差（模糊、曝光差异大等）")
        print(f"  3. 图片之间的重叠区域不够均匀")
        print(f"\n处理的图片列表（共{len(valid_images)}张）:")
        for i, path in enumerate(valid_images):
            img = images[i]
            print(f"  {i+1}. {path} (尺寸: {img.shape[1]}x{img.shape[0]}, 类型: {img.dtype})")

# 使用示例：把当前目录下所有 .jpg 图片按文件名排序后拼接
image_files = sorted(glob.glob("*.png"))
if len(image_files) < 2:
    print("至少需要两张图片")
else:
    stitch_images(image_files)