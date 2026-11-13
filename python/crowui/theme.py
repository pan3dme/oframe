# -*- coding: utf-8 -*-
"""
全局统一 UI 主题模块
====================
整个项目的颜色 / 字体 / 控件样式都从这里取，禁止在业务代码里硬编码色值。
改配色只需要改这一个文件。

设计风格：对齐微信小程序 cowsheepwechat（rpx 按 2rpx=1px 折算）
- 操作按钮主色：Google 蓝 #1A73E8（小程序 index.wxss）
- Tab 栏 / 功能网格 / 登录按钮：微信绿 #07C160（custom-tab-bar / features.wxss / login.wxss）
- 中性色：浅灰背景 + 白色卡片 + 深灰文字
"""

import os

from PyQt6.QtGui import QFont, QIcon

# ==================== 色板 ====================

# 主色（Google 蓝，操作按钮；小程序 index.wxss #1a73e8）
PRIMARY          = "#1A73E8"   # 主色：按钮、选中、强调
PRIMARY_HOVER    = "#1765CC"   # 主色悬停
PRIMARY_PRESSED  = "#1558B0"   # 主色按下
PRIMARY_LIGHT    = "#E8F0FE"   # 主色浅底（悬停高亮底色）

# 微信绿（Tab 栏选中 / 功能网格 / 登录与确认按钮；小程序 #07c160）
GREEN            = "#07C160"
GREEN_HOVER      = "#06AD56"
GREEN_DISABLED   = "#A8E6C0"   # 登录按钮禁用态（login.wxss）
GREEN_BORDER     = "#D4EDDA"   # 功能网格按钮描边（features.wxss）
GREEN_BG_A       = "#F0FAF5"   # 功能网格渐变起点
GREEN_BG_B       = "#E8F8EF"   # 功能网格渐变终点

# Tab 栏（custom-tab-bar 规格）
TAB_TEXT         = "#999999"   # 未选中文字
TAB_SELECTED     = GREEN       # 选中文字（selectedColor #07c160）
TAB_BORDER       = "#EEEEEE"   # 顶部分隔线

# 背景与卡片
BG               = "#F0F2F5"   # 面板/窗口背景（小程序 index.wxss #f0f2f5）
CARD_BG          = "#FFFFFF"   # 卡片背景
CARD_BORDER      = "#E2E8F0"   # 卡片描边
CARD_HOVER_BG    = "#F8FAFF"   # 卡片悬停底色
CARD_HOVER_BORDER = PRIMARY    # 卡片悬停描边

# 危险操作（注销登入）
DANGER           = "#EF5350"
DANGER_HOVER     = "#E53935"

# 图标目录（小程序 images/ 下复制的 tabBar 图标）
ICON_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'icons')


def tab_icon(name):
    """取 tabBar 图标（如 'tab_home'）"""
    path = os.path.join(ICON_DIR, name + '.png')
    return QIcon(path) if os.path.exists(path) else QIcon()

# 文字
TEXT_MAIN        = "#1E293B"   # 标题 / 主要文字
TEXT_NAV         = "#334155"   # 导航按钮文字
TEXT_SECONDARY   = "#64748B"   # 次要信息
TEXT_MUTED       = "#94A3B8"   # 弱化信息（占位、未链接等）

# 分隔线与滚动条
DIVIDER          = "#E2E8F0"
SCROLL_TRACK     = "#F1F5F9"
SCROLL_HANDLE    = "#CBD5E1"
SCROLL_HANDLE_HOVER = "#94A3B8"

# 字体
FONT_FAMILY = "Microsoft YaHei"


def font(size, bold=False):
    """统一字体入口：theme.font(11) / theme.font(14, bold=True)"""
    weight = QFont.Weight.Bold if bold else QFont.Weight.Normal
    return QFont(FONT_FAMILY, size, weight)


# ==================== 控件样式（QSS） ====================

# 顶部导航按钮（可勾选：选中即当前页面，主色填充）
NAV_BUTTON_QSS = f"""
    QPushButton {{
        background-color: {CARD_BG};
        color: {TEXT_NAV};
        border: 1px solid {CARD_BORDER};
        border-radius: 8px;
        font-size: 13px;
        font-weight: bold;
        padding: 8px 12px;
        min-height: 40px;
    }}
    QPushButton:hover {{
        background-color: {PRIMARY_LIGHT};
        color: {PRIMARY};
        border: 1px solid {PRIMARY};
    }}
    QPushButton:pressed {{
        background-color: {PRIMARY_LIGHT};
        color: {PRIMARY_PRESSED};
        border: 1px solid {PRIMARY_PRESSED};
    }}
    QPushButton:checked {{
        background-color: {PRIMARY};
        color: white;
        border: 1px solid {PRIMARY};
    }}
"""

# 主操作按钮（编辑 / 获取数据 / 返回 等，Google 蓝）
PRIMARY_BUTTON_QSS = f"""
    QPushButton {{
        background-color: {PRIMARY};
        color: white;
        border: none;
        border-radius: 6px;
        padding: 5px 14px;
    }}
    QPushButton:hover {{
        background-color: {PRIMARY_HOVER};
    }}
    QPushButton:pressed {{
        background-color: {PRIMARY_PRESSED};
    }}
"""

# 绿色确认按钮（登录 / 弹窗确认，微信绿胶囊，对标 login.wxss: 高80rpx 圆角46rpx）
GREEN_BUTTON_QSS = f"""
    QPushButton {{
        background-color: {GREEN};
        color: white;
        border: none;
        border-radius: 23px;
        padding: 8px 24px;
        min-height: 30px;
    }}
    QPushButton:hover {{
        background-color: {GREEN_HOVER};
    }}
    QPushButton:disabled {{
        background-color: {GREEN_DISABLED};
    }}
"""

# 灰色次要按钮（弹窗"取消"等，对标 features.wxss .track-btn-gray #999）
GRAY_BUTTON_QSS = f"""
    QPushButton {{
        background-color: #999999;
        color: white;
        border: none;
        border-radius: 23px;
        padding: 8px 24px;
        min-height: 30px;
    }}
    QPushButton:hover {{
        background-color: #8A8A8A;
    }}
"""

# 危险操作按钮（注销登入，红色）
DANGER_BUTTON_QSS = f"""
    QPushButton {{
        background-color: {DANGER};
        color: white;
        border: none;
        border-radius: 6px;
        padding: 5px 14px;
    }}
    QPushButton:hover {{
        background-color: {DANGER_HOVER};
    }}
"""

# 场景浮层按钮（覆盖在 3D/2D 地图上，半透明深色玻璃风）
OVERLAY_BUTTON_QSS = """
    QPushButton {
        background-color: rgba(15, 23, 42, 180);
        color: white;
        border: 1px solid rgba(255, 255, 255, 60);
        border-radius: 6px;
        font-size: 12px;
        padding: 4px 10px;
    }
    QPushButton:hover {
        background-color: rgba(15, 23, 42, 230);
        border: 1px solid rgba(255, 255, 255, 120);
    }
    QPushButton:pressed {
        background-color: rgba(2, 6, 23, 240);
    }
"""

# 可悬停的列表卡片（设备卡片 / 牛羊卡片）
CARD_QSS = f"""
    QFrame {{
        background-color: {CARD_BG};
        border: 1px solid {CARD_BORDER};
        border-radius: 10px;
    }}
    QFrame:hover {{
        background-color: {CARD_HOVER_BG};
        border: 1px solid {PRIMARY};
    }}
"""

# 静态卡片（详情页信息卡，无悬停）
CARD_STATIC_QSS = f"""
    QFrame {{
        background-color: {CARD_BG};
        border: 1px solid {CARD_BORDER};
        border-radius: 10px;
    }}
"""

# 滚动区域 + 滚动条（全局统一）
SCROLLBAR_QSS = f"""
    QScrollArea {{
        border: none;
        background-color: transparent;
    }}
    QScrollBar:vertical {{
        border: none;
        background: {SCROLL_TRACK};
        width: 8px;
        border-radius: 4px;
    }}
    QScrollBar::handle:vertical {{
        background: {SCROLL_HANDLE};
        border-radius: 4px;
        min-height: 30px;
    }}
    QScrollBar::handle:vertical:hover {{
        background: {SCROLL_HANDLE_HOVER};
    }}
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
        height: 0px;
    }}
    QScrollBar:horizontal {{
        border: none;
        background: {SCROLL_TRACK};
        height: 8px;
        border-radius: 4px;
    }}
    QScrollBar::handle:horizontal {{
        background: {SCROLL_HANDLE};
        border-radius: 4px;
        min-width: 30px;
    }}
    QScrollBar::handle:horizontal:hover {{
        background: {SCROLL_HANDLE_HOVER};
    }}
    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {{
        width: 0px;
    }}
"""

# 数据表格
TABLE_QSS = f"""
    QTableWidget {{
        background-color: {CARD_BG};
        border: 1px solid {CARD_BORDER};
        border-radius: 8px;
        gridline-color: {DIVIDER};
        color: {TEXT_MAIN};
    }}
    QTableWidget::item {{
        padding: 5px;
    }}
    QTableWidget::item:selected {{
        background-color: {PRIMARY};
        color: white;
    }}
    QHeaderView::section {{
        background-color: {BG};
        color: {TEXT_SECONDARY};
        padding: 8px;
        border: none;
        border-bottom: 1px solid {CARD_BORDER};
        font-weight: bold;
    }}
"""

# 页面堆叠容器
STACK_QSS = f"""
    QStackedWidget {{
        background-color: {CARD_BG};
        border: 1px solid {CARD_BORDER};
        border-radius: 10px;
    }}
"""
