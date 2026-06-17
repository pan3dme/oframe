import sys
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import QApplication, QMainWindow, QVBoxLayout, QWidget

from display3d.google_scene3d import GoogleScene3D


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("GoogleScene3D 测试")
        self.setGeometry(100, 100, 1440, 800)
        
        # 创建中心部件
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # 创建垂直布局
        layout = QVBoxLayout(central_widget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # 创建GoogleScene3D实例
        self.googleMapScene3D = GoogleScene3D()
        self.googleMapScene3D.setAutoFillBackground(True)
        palette = self.googleMapScene3D.palette()
        palette.setColor(self.googleMapScene3D.backgroundRole(), QColor("#2C3E50"))
        self.googleMapScene3D.setPalette(palette)
        self.googleMapScene3D.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        
        # 添加到布局
        layout.addWidget(self.googleMapScene3D)
        
        print("✅ GoogleScene3D 已添加到窗口")


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())
