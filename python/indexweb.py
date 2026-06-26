import webview
import re
import webbrowser
import threading


class Api:
    """pywebview JS API 接口类"""
    def __init__(self):
        self.window = None
        self.last_coord = ""

    def load_url(self, url):
        """加载指定URL到iframe"""
        if url and not url.startswith("http"):
            url = "https://" + url
        self.window.evaluate_js(f"document.getElementById('web-frame').src = '{url}'")

    def get_current_url(self):
        """获取当前iframe的URL"""
        return self.window.evaluate_js("document.getElementById('web-frame').src")

    def go_back(self):
        """上一页（通过修改URL中的数字）"""
        url = self.window.evaluate_js("document.getElementById('web-frame').src")
        if url:
            match = re.search(r'(\d+)$', url)
            if match:
                num = int(match.group(1))
                new_url = url[:match.start()] + str(num - 1) + url[match.end():]
                self.window.evaluate_js(f"document.getElementById('web-frame').src = '{new_url}'")
                self.window.evaluate_js(f"document.getElementById('url-bar').value = '{new_url}'")

    def go_next(self):
        """下一页（通过修改URL中的数字）"""
        url = self.window.evaluate_js("document.getElementById('web-frame').src")
        if url:
            match = re.search(r'(\d+)$', url)
            if match:
                num = int(match.group(1))
                new_url = url[:match.start()] + str(num + 1) + url[match.end():]
                self.window.evaluate_js(f"document.getElementById('web-frame').src = '{new_url}'")
                self.window.evaluate_js(f"document.getElementById('url-bar').value = '{new_url}'")

    def open_in_browser(self):
        """在系统浏览器中打开"""
        url = self.window.evaluate_js("document.getElementById('web-frame').src")
        if url:
            webbrowser.open(url)

    def copy_to_clipboard(self, text):
        """复制文本到剪贴板"""
        self.window.evaluate_js(f"navigator.clipboard.writeText('{text}')")


# 工具栏 + iframe 的 HTML 页面
HTML_CONTENT = """
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
html, body { height: 100%; overflow: hidden; font-family: "Microsoft YaHei", sans-serif; }
#toolbar {
    display: flex; align-items: center; gap: 5px;
    padding: 5px; background: #f0f0f0; border-bottom: 1px solid #ccc;
    height: 45px; flex-shrink: 0;
}
#url-bar {
    flex: 1; height: 33px; padding: 0 10px; font-size: 14px;
    border: 1px solid #ccc; border-radius: 4px; outline: none;
}
#url-bar:focus { border-color: #4a90d9; }
.btn {
    height: 33px; padding: 0 12px; font-size: 13px; color: white;
    border: none; border-radius: 4px; cursor: pointer; white-space: nowrap;
}
.btn:hover { opacity: 0.85; }
.btn:active { opacity: 0.7; }
.btn-gray { background: #6c757d; }
.btn-blue { background: #4a90d9; }
.btn-green { background: #5cb85c; }
.btn-orange { background: #f0ad4e; }
.btn-cyan { background: #5bc0de; }
.btn-teal { background: #17a2b8; }
#web-frame {
    width: 100%; flex: 1; border: none; background: white;
}
#container { display: flex; flex-direction: column; height: 100%; }
</style>
</head>
<body>
<div id="container">
    <div id="toolbar">
        <button class="btn btn-gray" onclick="pywebview.api.open_in_browser()">浏览器打开</button>
        <input id="url-bar" type="text" value="https://fccw54.com/embed/107729"
               placeholder="输入网址，按回车加载"
               onkeydown="if(event.key==='Enter'){document.getElementById('web-frame').src=this.value.trim();}">
        <button class="btn btn-blue" onclick="pywebview.api.go_back()">上一页</button>
        <button class="btn btn-blue" onclick="pywebview.api.go_next()">下一页</button>
        <button class="btn btn-cyan" onclick="copyCoord()">复制坐标</button>
        <button class="btn btn-cyan" onclick="copyUrl()">复制网址</button>
    </div>
    <iframe id="web-frame" src="https://fccw54.com/embed/107729"
            allowfullscreen allow="autoplay; encrypted-media; fullscreen"></iframe>
</div>
<script>
function copyUrl() {
    var url = document.getElementById('url-bar').value;
    navigator.clipboard.writeText(url);
}
function copyCoord() {
    // 预留坐标复制功能
    navigator.clipboard.writeText('');
}
</script>
</body>
</html>
"""


def on_loaded(window):
    """窗口加载完成回调"""
    api.window = window


if __name__ == "__main__":
    api = Api()
    window = webview.create_window(
        title="极简浏览器 (Edge WebView2)",
        html=HTML_CONTENT,
        width=1200,
        height=800,
        js_api=api,
        text_select=True,
    )
    window.events.loaded += lambda: on_loaded(window)
    webview.start(debug=False)
