# main.py
import threading
from app import app,sync_device_status
from console import ServerConsole
from scheduler import run_feed_scheduler
import feed_api  # 确保注册接口路由
import time 
# 路由线程
def run_server():
    """启动 Flask Web + WebSocket 服务"""
    app.run(host='127.0.0.1', port=5000, debug=True, use_reloader=False)

def start_background_thread(name, target):
    """启动守护线程并标记名称"""
    thread = threading.Thread(target=target, daemon=True)
    thread.name = name
    thread.start()
    return thread
#离线检查线程
def check_status_thread(time_minutes=5):
    while True:
        sync_device_status()
        time.sleep(time_minutes*60)
if __name__ == '__main__':
    print("🟢 启动 Flask + WebSocket 服务...")
    start_background_thread("flask_server", run_server)

    print("🟢 启动定时喂食调度器...")
    start_background_thread("feed_scheduler", run_feed_scheduler)

    print("🟢 启动离线检查...")
    start_background_thread("sync_device_status", check_status_thread)

    print("🟢 启动命令控制台...")
    try:
        ServerConsole().cmdloop()
    except KeyboardInterrupt:
        print("\n👋 已手动退出控制台。")