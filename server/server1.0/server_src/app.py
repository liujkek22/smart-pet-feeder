import json
import traceback
import time
import logging
from datetime import datetime, timedelta
from flask import Flask, request, jsonify, render_template, send_from_directory
from flask_sock import Sock
import mysql.connector
from log_config import setup_logger
from simple_websocket.errors import ConnectionClosed
import requests
# 初始化日志
logger = setup_logger()

# 初始化 Flask 和 Sock
app = Flask(__name__)
sock = Sock(app)
# 全局clients 对象
clients = {}
# 相隔四分钟即检查是否离线
MAX_INACTIVE_TIME = timedelta(minutes=5)

# 数据库连接函数
def get_db_connection():
    return mysql.connector.connect(
        host="localhost",
        user="root",
        password="your_password",
        database="pet_feeder_test"
    )

def register_device_in_db(uuid):
    try:
        connection = get_db_connection()
        cursor = connection.cursor()
        cursor.execute("SELECT COUNT(*) FROM device_info WHERE uuid = %s", (uuid,))
        result = cursor.fetchone()
        if result[0] == 0:
            cursor.execute("""
                INSERT INTO device_info (
                    uuid, last_heartbeat, status, created_at
                ) VALUES (
                    %s, NOW(), 'online', NOW()
                )
            """, (uuid,))
            logger.info(f"Device {uuid} registered in the database.")
        else:
            cursor.execute("""
                UPDATE device_info
                SET last_heartbeat = NOW(), status = 'online'
                WHERE uuid = %s
            """, (uuid,))
            logger.info(f"Device {uuid} status updated to online.")
        connection.commit()
    finally:
        cursor.close()
        connection.close()
        return 1


# 更新设备心跳时间
def update_device_heartbeat(uuid):
    connection = get_db_connection()
    cursor = connection.cursor()
    now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    query = """
        UPDATE device_info 
        SET last_heartbeat = %s, status = 'online' 
        WHERE uuid = %s
    """
    cursor.execute(query, (now, uuid))
    connection.commit()
    cursor.close()
    connection.close()
    logger.info(f"Device {uuid} is online. Last heartbeat updated.")
# 写入离线信息
def update_device_offline(uuid):
    connection = get_db_connection()
    cursor = connection.cursor()
    query = """
        UPDATE device_info 
        SET status = 'offline' 
        WHERE uuid = %s
    """
    cursor.execute(query, (uuid,))
    connection.commit()
    cursor.close()
    connection.close()
    logger.info(f"Device {uuid} is offline.")

# 检查设备是否离线
def sync_device_status():
    global client
    """
    根据当前连接的 clients 列表更新所有设备状态：
    - clients: 当前在线的 uuid 字典，如 {'uuid1': server_client1, 'uuid2': server_client2, .... }
    """
    try:
        connection = get_db_connection()
        cursor = connection.cursor()

        # 全部先设为 offline（排除还在线的）
        if clients:
            uuids = list(clients.keys())  # 获取所有在线设备的 uuid 列表
            placeholders = ','.join(['%s'] * len(uuids))
            query_offline = f"""
                UPDATE device_info 
                SET status = 'offline' 
                WHERE uuid NOT IN ({placeholders})
            """
            cursor.execute(query_offline, uuids)  # ✅ 传入 uuids 而不是 clients
        else:
            # 如果没有任何在线设备，全部标记为离线
            cursor.execute("UPDATE device_info SET status = 'offline'")

        # 在线设备更新为 online + 心跳时间
        now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        for uuid in clients.keys():
            cursor.execute("""
                UPDATE device_info 
                SET status = 'online', last_heartbeat = %s 
                WHERE uuid = %s
            """, (now, uuid))
        
        connection.commit()
        logger.info("设备状态同步完成")
    except Exception as e:
        logger.error(f"[数据库错误] 设备状态同步失败：{e}")
    finally:
        try:
            cursor.close()
            connection.close()
        except:
            pass

#数据更新服务
def update_device_status(uuid, water_level, water_temperature,feed_count,food_weight):
    try:
        conn = get_db_connection()
        cursor = conn.cursor()
        # 更新水位、温度、喂食次数
        cursor.execute("""
            UPDATE device_info
            SET water_level = %s,
                water_temperature = %s,
                feed_count = %s,
                food_weight=%s
            WHERE uuid = %s
        """, (water_level, water_temperature,feed_count,food_weight,uuid))
        conn.commit()
        logger.info(f"📥 已更新状态：{uuid} -> 水位={water_level}, 水温={water_temperature},喂食次数={feed_count},食量余量={food_weight}")
    except Exception as e:
        logger.error(f"❌ 更新设备 {uuid} 状态失败: {e}")
    finally:
        cursor.close()
        conn.close()
def update_device_from_user(uuid, temperature, switch=0):
    # 从用户角度去更新恒温系统的开启和关闭
    try:
        conn = get_db_connection()
        cursor = conn.cursor()
        
        if switch:
            # 如果时开启了恒温系统
            logger.info(f"📥 将更新状态：{uuid}, 恒温系统将开启 -> 温度 {temperature}")
            cursor.execute("""
                UPDATE device_info
                SET warm_keep_temperature = %s,
                    warm_keep_switch = %s
                WHERE uuid = %s
            """, (temperature, switch, uuid))
        else:
            # 如果关闭了恒温系统
            logger.info(f"📥 将更新状态：{uuid}, 恒温系统将关闭")
            cursor.execute("""
                UPDATE device_info
                SET warm_keep_switch = %s,
                    warm_keep_temperature =%s
                WHERE uuid = %s
            """, (switch, temperature,uuid))
        
        conn.commit()
        logger.info(f"用户设定恒温系统完成，设备 {uuid} 状态更新成功。")
    
    except Exception as e:
        logger.error(f"❌ 更新设备 {uuid} 状态失败: {e}")
    
    finally:
        # Ensure cursor and connection are closed even in case of an error
        cursor.close()
        conn.close()

# 处理信息
@sock.route("/ws")
def websocket_handler(ws):
    try:
        while True:
            try:
                data = ws.receive()
                if not data:
                    logger.warning("⚠️ 收到空数据，可能是连接保活断开。")
                    continue

                logger.info(f"📨 收到数据：{data}")
                json_data = json.loads(data)

                action = json_data.get('action')
                uuid = json_data.get('UUID')

                if not uuid:
                    logger.error("❌ 收到数据中缺少 UUID")
                    continue

                # 设备注册
                if action == "register":
                    clients[uuid] = ws
                    if register_device_in_db(uuid):
                        logger.info(f"✅ 注册成功：{uuid}")
                        confirmation_msg = {"id": 1, "name": "esp32", "action": "register load"}
                        ws.send(json.dumps(confirmation_msg))

                # 心跳
                elif action == "heartbeat":
                    update_device_heartbeat(uuid)

                # 数据上传
                elif action == "upload":
                    water_level = json_data.get("water_level")
                    water_temperature = json_data.get("water_temperature")
                    feed_count = json_data.get("feed_count")
                    food_weight = json_data.get("food_weight")
                    if water_level is not None and water_temperature is not None and feed_count is not None:
                        update_device_status(uuid, water_level, water_temperature, feed_count,food_weight)
                    else:
                        logger.warning(f"⚠️ 上传数据不完整：{json_data}")

                else:
                    logger.warning(f"⚠️ 未知指令 {action} 来自设备 {uuid}")

            except json.JSONDecodeError:
                logger.error("❌ JSON 解析错误，数据非法。")
            except ConnectionClosed as ce:
                logger.info(f"🔌 WebSocket 已关闭连接：{ce}")
                break
            except Exception as e:
                logger.error(f"❌ 单次数据处理异常：{e}\n{traceback.format_exc()}")

    except ConnectionClosed as e:
        logger.info(f"🔌 WebSocket 客户端主动断开连接：{e}")
    except Exception as e:
        logger.error(f"❌ WebSocket 会话异常退出：{e}\n{traceback.format_exc()}")

    finally:
        # 清理连接并发送断线通知
        for uuid, client in list(clients.items()):
            if client == ws:
                del clients[uuid]
                logger.info(f"🔌 设备 {uuid} 已断开连接")
                try:
                    # 发送数据通知到微信客户端
                    update_device_offline(uuid)
                    notify_user_device_offline(uuid)
                except Exception as e:
                    logger.error(f"❌ 断线通知失败：{e}")
                break
# Web API: 设备信息查询
@app.route('/api/infor/get', methods=['GET'])
def info_get():
    uuid = request.args.get('UUID')
    if not uuid:
        return jsonify({"error": "UUID is missing"}), 400

    connection = get_db_connection()
    cursor = connection.cursor(dictionary=True)
    try:
        cursor.execute("SELECT * FROM device_info WHERE uuid = %s", (uuid,))
        result = cursor.fetchone()
        if result:
            usage_days = (datetime.now() - result['created_at']).days
            device_info = {
                "uuid": result['uuid'],
                "status": result['status'],
                "water_level": result['water_level'],
                "water_temperature": result['water_temperature'],
                "usage_days": usage_days,
                "feed_enabled":result['feed_enabled'],
                "feed_task_time1": str(result['feed_task_time1']),
                "feed_task_status1": result['feed_task_status1'],
                "feed_task_time2": str(result['feed_task_time2']),
                "feed_task_status2": result['feed_task_status2'],
                "feed_task_time3": str(result['feed_task_time3']),
                "feed_task_status3": result['feed_task_status3'],
                "feed_count":result['feed_count'],
                "food_weight":result['food_weight'],
                "warm_keep_temperature":result['warm_keep_temperature'],
                "warm_keep_switch":result['warm_keep_switch'],
                # 这两个字段前端不会使用到
                # "created_at": str(result['created_at']),
                # "last_heartbeat": str(result['last_heartbeat'])
            }
            return jsonify(device_info), 200
        else:
            return jsonify({"error": "Device not found"}), 404
    except mysql.connector.Error as err:
        return jsonify({"error": f"Database error: {err}"}), 500
    finally:
        cursor.close()
        connection.close()
# Web API: 控制设备行为（换水）
@app.route('/api/action', methods=['POST', 'GET'])
def action_client():
    uuid = request.args.get('UUID')
    action = request.args.get('action')
    # 喂食任务由定时任务负责
    # if action == "feed":
    #     return jsonify({"status": "success", "message": "Feeding command sent."})
    if action == "water":
        message_dict = {"status": "success", "action": "Water"}
        send_to_client(uuid, message_dict)
        return jsonify({"status": "success", "message": "Watering command sent."})
    elif action == "feed":
        message_dict = {"status": "success", "action": "feed"}
        send_to_client(uuid, message_dict)
        return jsonify({"status": "success", "message": "feed command sent."})
    else:
        return {"error": "Invalid action."}, 400
# web api 管理恒温系统的开启和设置恒温系统
@app.route('/api/warm_control', methods=['POST', 'GET'])
def warm_control():
    uuid = request.args.get('UUID')
    action = request.args.get('action')  # 可以是 'on' 或 'off'
    temperature = request.args.get('temperature')  # 设置的温度，如 '22'
    
    # 检查是否传入有效的设备 UUID
    if not uuid:
        return jsonify({"error": "UUID is required."}), 400
    # 处理开启或关闭恒温系统
    if action == 'on':
        if temperature:
            # 设置恒温系统的温度
            try:
                temperature = int(temperature)
                if 10 <= temperature <= 50:  # 假设温度范围是10到50度
                    # 发送设备开启恒温指令和发送恒温温度到设备
                    message_dict = {"action": "warm_on", "temperature": temperature}
                    send_to_client(uuid, message_dict)
                    # 同时连接数据库修改对应数据存储的数据和数据库开关
                    update_device_from_user(uuid, temperature, switch=1)
                    return jsonify({"status": "success", "message": f"Warm system enabled at {temperature}°C."})
                else:
                    return jsonify({"error": "Invalid temperature. Please provide a value between 10 and 50."}), 400
            except ValueError:
                return jsonify({"error": "Invalid temperature value."}), 400
        else:
            return jsonify({"error": "Temperature is required when turning on the warm system."}), 400

    elif action == 'off':
        # 发送设备关闭恒温指令
        message_dict = {"action": "warm_off", "temperature": None}
        send_to_client(uuid, message_dict)
        # 同时连接数据库修改对应数据存储的数据和数据库开关
        update_device_from_user(uuid, temperature=12, switch=0)
        return jsonify({"status": "success", "message": "Warm system disabled."})

    else:
        return jsonify({"error": "Invalid action. Use 'on' or 'off'."}), 400

# Web API: 静态文件图片访问
@app.route('/images/<path:filename>')
def serve_image(filename):
    return send_from_directory('images', filename)

# Web API: 首页测试
@app.route('/')
def hello():
    return render_template('index.html')

# 控制台：从控制端向设备发送 JSON 消息
def send_to_client(uuid, message_dict):
    if uuid not in clients:
        print(f"❌ 客户端 {uuid} 不在线")
        return
    try:
        ws = clients[uuid]
        ws.send(json.dumps(message_dict))
    except Exception as e:
        print(f"❌ 发送异常：{e}")

# 微信后端，基本已经完成，等待升级和测试
# 🧾 微信登录换 openid
@app.route('/api/wechat-login')
def wechat_login():
    code = request.args.get("code")
    if not code:
        return jsonify({"error": "缺少 code 参数"}), 400

    appid = "wxe5581512333a0698"
    secret = "6e8685c964e7fb3bea02384121664fec"
    url = f"https://api.weixin.qq.com/sns/jscode2session?appid={appid}&secret={secret}&js_code={code}&grant_type=authorization_code"

    try:
        resp = requests.get(url)
        data = resp.json()
        logger.info(f"code2Session 返回数据：{data}")

        if "openid" in data:
            return jsonify({"openid": data["openid"]})
        else:
            return jsonify({"error": "微信认证失败", "detail": data}), 400
    except Exception as e:
        return jsonify({"error": str(e)}), 500

# ✅ 用户绑定 openid 和设备 UUID
@app.route('/api/user/bind', methods=['POST'])
def user_bind():
    data = request.get_json()
    openid = data.get("openid")
    uuid = data.get("UUID")

    if not openid or not uuid:
        return jsonify({"error": "参数不完整"}), 400

    conn = get_db_connection()
    cursor = conn.cursor()
    try:
        cursor.execute("""
            INSERT INTO user_binding (openid, uuid)
            VALUES (%s, %s)
            ON DUPLICATE KEY UPDATE uuid = VALUES(uuid)
        """, (openid, uuid))
        conn.commit()
        return jsonify({"status": "ok"})
    except Exception as e:
        return jsonify({"error": str(e)}), 500
    finally:
        cursor.close()
        conn.close()
#消息通知
def notify_user_device_offline(uuid):
    conn = get_db_connection()
    cursor = conn.cursor(dictionary=True)
    try:
        cursor.execute("SELECT openid FROM user_binding WHERE uuid = %s", (uuid,))
        result = cursor.fetchone()
        if not result:
            logger.info(f"🔍 设备 {uuid} 未绑定用户，跳过通知")
            return

        openid = result['openid']
        access_token = get_wechat_access_token()
        if not access_token:
            logger.error("❌ 获取 access_token 失败")
            return

        url = f"https://api.weixin.qq.com/cgi-bin/message/subscribe/send?access_token={access_token}"
        data = {
            "touser": openid,
            "template_id": "7Ia2ARLdfy5bD-MfqtcUKB0I4bc3q9xmtPeUC8dt5jw",  # ✅ 替换为你的模板 ID
            "page": "/pages/home/home",
            "miniprogram_state": "formal",
            "lang": "zh_CN",
            "data": {
                "thing2": {"value": f"设备掉线"},
                "phrase9": {"value": "严重"}
            }
        }

        resp = requests.post(url, json=data)
        logger.info(f"📨 推送断线通知给 {openid}，响应：{resp.text}")

    finally:
        cursor.close()
        conn.close()

# 获取微信 access_token
_access_token_cache = None
_access_token_time = None

def get_wechat_access_token():
    global _access_token_cache, _access_token_time
    import time

    if _access_token_cache and (time.time() - _access_token_time) < 7000:
        return _access_token_cache

    # appid = "你的AppID"
    # secret = "你的AppSecret"
    # 6e8685c964e7fb3bea02384121664fec
    appid = "wxe5581512333a0698"
    secret = "6e8685c964e7fb3bea02384121664fec"
    url = f"https://api.weixin.qq.com/cgi-bin/token?grant_type=client_credential&appid={appid}&secret={secret}"

    try:
        resp = requests.get(url)
        data = resp.json()
        if "access_token" in data:
            _access_token_cache = data["access_token"]
            _access_token_time = time.time()
            return _access_token_cache
        else:
            logger.error(f"❌ 获取 access_token 失败：{data}")
            return None
    except Exception as e:
        logger.error(f"❌ 请求 access_token 异常：{e}")
        return None
