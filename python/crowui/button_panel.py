import random

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QPushButton,
                             QRadioButton, QCheckBox, QDialogButtonBox, QLineEdit)
from PyQt6.QtGui import QFont

from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout




class ButtonPanel(QWidget):
    """按钮面板组件"""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)
        layout.setSpacing(0)



        # 上半部分：控件区域
        self.top_widget = QWidget()
        self.top_layout = QVBoxLayout(self.top_widget)
        self.top_layout.setContentsMargins(5, 5, 5, 5)

        # QRadioButton
        self.radio1 = QRadioButton("选项一")
        self.radio1.setFont(QFont("Microsoft YaHei", 10))
        self.top_layout.addWidget(self.radio1)
        self.radio2 = QRadioButton("选项二")
        self.radio2.setFont(QFont("Microsoft YaHei", 10))
        self.top_layout.addWidget(self.radio2)

        # QCheckBox
        self.checkbox1 = QCheckBox("复选框 A")
        self.checkbox1.setFont(QFont("Microsoft YaHei", 10))
        self.top_layout.addWidget(self.checkbox1)
        self.checkbox2 = QCheckBox("复选框 B")
        self.checkbox2.setFont(QFont("Microsoft YaHei", 10))
        self.top_layout.addWidget(self.checkbox2)

        # QLineEdit
        self.line_edit = QLineEdit()
        self.line_edit.setPlaceholderText("请输入内容...")
        self.line_edit.setFont(QFont("Microsoft YaHei", 10))
        self.top_layout.addWidget(self.line_edit)

        # QDialogButtonBox
        self.button_box = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        self.button_box.setFont(QFont("Microsoft YaHei", 10))
        self.top_layout.addWidget(self.button_box)

        self.top_layout.addStretch()
        layout.addWidget(self.top_widget, stretch=1)

        # 下半部分：按钮区域
        self.bottom_widget = QWidget()
        self.bottom_layout = QVBoxLayout(self.bottom_widget)
        self.bottom_layout.setContentsMargins(5, 5, 5, 5)



        layout.addWidget(self.bottom_widget, stretch=0)


