# feed_api.py
from flask import request, jsonify
from app import app, get_db_connection
from datetime import datetime

# 设置三个时间段的时间与份量（上载时间）
# 微信小程序调用示例：
'''
wx.request({
  url: 'http://你的服务器地址:5000/api/feed-plan/set-all',
  method: 'POST',
  data: {
    UUID: "123e4567-e89b-12d3-a456-426614174000",
    plan: [
      { slot: 1, time: "07:30:00", portion: 80 },
      { slot: 2, time: "12:00:00", portion: 100 },
      { slot: 3, time: "18:30:00", portion: 120 }
    ]
  },
  header: {
    'content-type': 'application/json'
  },
  success(res) {
    console.log("配置成功", res.data);
  }
});
'''
@app.route('/api/feed-plan/set-all', methods=['POST'])
def update_all_feed_plans():
    data = request.get_json()
    uuid = data.get("UUID")
    plans = data.get("plan", [])

    if not uuid or not isinstance(plans, list):
        return jsonify({"error": "Invalid input"}), 400

    conn = get_db_connection()
    cursor = conn.cursor()

    now = datetime.now().strftime("%H:%M:%S")  # 当前时间字符串

    for p in plans:
        slot = p.get("slot")
        time_str = p.get("time")  # 计划时间字符串 "HH:MM:SS"
        if slot in [1, 2, 3] and time_str:
            # 判断时间是否有效（是否已过去）
            is_valid = int(time_str < now)  # 1 表示过期，不需要执行。0 表示未过期
            cursor.execute(f"""
                UPDATE device_info
                SET feed_task_time{slot} = %s,
                    feed_task_status{slot} = %s
                WHERE uuid = %s
            """, (time_str, is_valid, uuid))

    conn.commit()
    cursor.close()
    conn.close()

    return jsonify({"status": "success", "updated": len(plans)})


# 设置喂食总开关（启用/禁用所有定时任务）
# 微信小程序调用示例：
'''
wx.request({
  url: 'http://你的服务器地址:5000/api/feed-plan/set-enable',
  method: 'POST',
  data: {
    UUID: "123e4567-e89b-12d3-a456-426614174000",
    enabled: true
  },
  header: {
    'content-type': 'application/json'
  },
  success(res) {
    console.log("总开关设置结果", res.data);
  }
});
'''
@app.route('/api/feed-plan/set_switch_status', methods=['POST'])
def set_feed_enable():
    data = request.get_json()
    uuid = data.get("UUID")
    status = data.get("set_switch_status")

    if not uuid or status not in [0, 1, True, False]:
        return jsonify({"error": "Missing UUID or invalid status flag"}), 400

    status_int = int(bool(status))

    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("""
        UPDATE device_info
        SET feed_enabled = %s
        WHERE uuid = %s
    """, (status_int, uuid))
    conn.commit()
    cursor.close()
    conn.close()

    return jsonify({"status": "success", "feed_enabled": status_int})
# @app.route('/api/feed-plan/set-disenable', methods=['POST'])
# def set_feed_disenable():
#     data = request.get_json()
#     uuid = data.get("UUID")
#     disenable = data.get("disenabled")

#     if not uuid or disenable not in [0, 1, True, False]:
#         return jsonify({"error": "Missing UUID or invalid disenable flag"}), 400

#     disenable_int = int(bool(disenable))

#     conn = get_db_connection()
#     cursor = conn.cursor()
#     cursor.execute("""
#         UPDATE device_info
#         SET feed_enabled = %s
#         WHERE uuid = %s
#     """, (disenabled_int, uuid))
#     conn.commit()
#     cursor.close()
#     conn.close()

#     return jsonify({"status": "success", "feed_enabled": disenabled_int})