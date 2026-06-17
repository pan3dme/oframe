import http.server
import socketserver
import os
import webbrowser
import threading

PORT = 8080
DIRECTORY = os.path.dirname(os.path.abspath(__file__))

class MyHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

def start_server():
    with socketserver.TCPServer(("", PORT), MyHandler) as httpd:
        print(f"服务器启动在 http://localhost:{PORT}")
        print(f"按 Ctrl+C 停止服务器")
        httpd.serve_forever()

if __name__ == "__main__":
    # 在新线程中启动服务器
    server_thread = threading.Thread(target=start_server, daemon=True)
    server_thread.start()
    
    # 打开浏览器访问测试页面
    test_url = f"http://localhost:{PORT}/baidu_map_diagnostic.html"
    print(f"正在打开浏览器: {test_url}")
    webbrowser.open(test_url)
    
    try:
        # 保持主线程运行
        server_thread.join()
    except KeyboardInterrupt:
        print("\n服务器已停止")
