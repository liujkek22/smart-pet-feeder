import cmd
import os
import json
import logging
import asyncio
from datetime import datetime

from app import clients, logger, send_to_client
from scheduler import reset_feed_task_status

LOG_FILE = 'logs/server.log'

class ServerConsole(cmd.Cmd):
    intro = "\n欢迎使用服务器测试控制台。输入 help 或 ? 查看命令列表。"
    prompt = ">> "

    def do_client(self, arg):
        """列出当前连接的设备 UUID"""
        if clients:
            print("\n当前在线设备 UUID 列表：")
            for uuid in clients:
                print(f"  - {uuid}")
        else:
            print("\n当前没有在线设备。")

    def do_infor(self, arg):
        """查看最近 10 条日志内容"""
        if not os.path.exists(LOG_FILE):
            print("日志文件不存在。")
            return

        with open(LOG_FILE, 'r', encoding='utf-8') as f:
            lines = f.readlines()[-10:]
            print("\n--- 最近日志记录 ---")
            for line in lines:
                print(line.strip())

    def do_websocket(self, arg):
        """websocket <UUID> <内容> -> 向指定设备发送自定义消息"""
        parts = arg.strip().split(' ', 1)
        if len(parts) != 2:
            print("用法错误:websocket <UUID> <内容>")
            return

        uuid, message = parts
        if uuid not in clients:
            print(f"设备 {uuid} 不在线或不存在。")
            return

        try:
            payload = {
                "id": 999,
                "action": "console",
                "content": message
            }
            send_to_client(uuid, payload)
            logger.info(f"控制台向 {uuid} 发送信息：{message}")
            print("✅ 发送成功")
        except Exception as e:
            print(f"发送失败: {e}")

    def do_testfeed(self, arg):
        """testfeed <UUID> -> 模拟喂食测试"""
        if arg not in clients:
            print("设备不在线或 UUID 无效。")
            return
        clients[arg].send(json.dumps({"id": 2, "action": "feed"}))
        print(f"✅ 已发送喂食指令到 {arg}")

    def do_testwater(self, arg):
        """testwater <UUID> -> 模拟换水测试"""
        if arg not in clients:
            print("设备不在线或 UUID 无效。")
            return
        clients[arg].send(json.dumps({"id": 3, "action": "water"}))
        print(f"✅ 已发送换水指令到 {arg}")

    def do_resetfeed(self, arg):
        """手动执行任务状态重置（模拟每天 0 点执行）"""
        reset_feed_task_status()
        print("🌅 所有设备执行状态已重置为 0")

    def do_ping(self, arg):
        """ping <UUID> -> 测试设备连接是否存活"""
        if arg not in clients:
            print("设备不在线或 UUID 无效。")
            return
        payload = {"action": "ping", "timestamp": datetime.now().isoformat()}
        send_to_client(arg, payload)
        print(f"📡 向 {arg} 发送 ping")

    def do_clear(self, arg):
        """清屏"""
        os.system('cls' if os.name == 'nt' else 'clear')

    def do_exit(self, arg):
        """退出控制台"""
        print("👋 已退出控制台。")
        return True
