from PIL import Image
import os

# ================= 配置区域 =================
INPUT_FILE = '2.png'
TARGET_WIDTH = 480  # 只指定宽度，高度自动算
# ===========================================

def resize_image_auto_height():
    if not os.path.exists(INPUT_FILE):
        print(f"错误: 找不到文件 {INPUT_FILE}")
        return

    try:
        # 1. 打开原始图片
        img = Image.open(INPUT_FILE)
        raw_width, raw_height = img.size
        print(f"原始尺寸: {raw_width} x {raw_height}")

        # 2. 计算缩放比例
        # 比例 = 目标宽度 / 原始宽度
        aspect_ratio = TARGET_WIDTH / float(raw_width)
        
        # 3. 计算新的高度 (取整)
        target_height = int((float(raw_height) * float(aspect_ratio)))

        print(f"计算新尺寸: {TARGET_WIDTH} x {target_height} (保持比例)")

        # 4. 执行缩放 (使用高质量滤镜)
        img_resized = img.resize((TARGET_WIDTH, target_height), Image.Resampling.LANCZOS)

        # 5. 生成动态文件名 (体现长宽)
        output_filename = f"output_{TARGET_WIDTH}x{target_height}.png"
        
        # 6. 保存
        img_resized.save(output_filename)
        print(f"✅ 处理完成: 已保存为 {output_filename}")

    except Exception as e:
        print(f"❌ 处理出错: {e}")

if __name__ == '__main__':
    resize_image_auto_height()