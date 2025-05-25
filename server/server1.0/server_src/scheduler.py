# task_scheduler.py

import json
from datetime import datetime, timedelta
from apscheduler.schedulers.background import BackgroundScheduler
import mysql.connector
from app import clients,get_db_connection 
from log_config import setup_logger

logger = setup_logger()
# 向设备发送喂食指令
def send_feed_command(device_uuid, slot):
    if device_uuid in clients:
        try:
            payload = {
                "action": "feed",
                "slot": slot
            }
            clients[device_uuid].send(json.dumps(payload))
            logger.info(f"✅ 已发送喂食命令 -> 设备: {device_uuid}，槽位: {slot}")
        except Exception as e:
            logger.error(f"❌ 发送失败：{device_uuid} -> {e}")
    else:
        logger.warning(f"⚠️ 设备 {device_uuid} 不在线，跳过发送")

# 每分钟检查是否有任务触发
def check_and_trigger_tasks():
    now = datetime.now().strftime("%H:%M")
    logger.debug(f"🕒 本轮调度时间: {now}")
    try:
        conn = get_db_connection()
        cursor = conn.cursor()

        cursor.execute("""
            SELECT uuid, feed_enabled,
                   feed_task_time1, feed_task_status1,
                   feed_task_time2, feed_task_status2,
                   feed_task_time3, feed_task_status3
            FROM device_info
        """)

        for row in cursor.fetchall():
            uuid = row[0]
            feed_enabled = row[1]

            if not feed_enabled:
                logger.debug(f"🚫 设备 {uuid} 的喂食功能已关闭，跳过")
                continue

            for i in range(3):
                task_time = row[2 + i * 2]
                task_status = row[3 + i * 2]

                if task_time is None:
                    continue

                try:
                    if isinstance(task_time, timedelta):
                        total_seconds = task_time.total_seconds()
                        hours = int(total_seconds // 3600)
                        minutes = int((total_seconds % 3600) // 60)
                        task_hm = f"{hours:02d}:{minutes:02d}"
                    else:
                        task_hm = task_time.strftime("%H:%M")
                except Exception as e:
                    logger.warning(f"❌ 设备 {uuid} 槽位{i+1} 的任务时间转换失败，跳过 -> {e}")
                    continue


                if task_hm == now and task_status == 0:
                    logger.info(f"⏰ [调度成功] 设备 {uuid} 在 {now} 触发第 {i + 1} 槽任务")

                    # 发送指令
                    send_feed_command(uuid, i + 1)

                    # 更新执行状态，避免重复执行
                    cursor.execute(f"""
                        UPDATE device_info SET feed_task_status{i + 1} = 1 WHERE uuid = %s
                    """, (uuid,))
                    conn.commit()
                elif task_hm == now and task_status == 1:
                    logger.debug(f"ℹ️ 设备 {uuid} 第 {i + 1} 个任务已执行，跳过")
                else:
                    logger.debug(f"⏭ 设备 {uuid} 第 {i + 1} 个任务设定为 {task_hm}，当前时间为 {now}，不触发")

        cursor.close()
        conn.close()

    except Exception as e:
        logger.error(f"🔥 调度器执行异常: {e}")

# 每天凌晨 00:00 重置任务状态
def reset_feed_status_daily():
    try:
        conn = get_db_connection()
        cursor = conn.cursor()
        cursor.execute("""
            UPDATE device_info
            SET feed_task_status1 = 0,
                feed_task_status2 = 0,
                feed_task_status3 = 0,
                feed_count = 0
        """)
        conn.commit()
        cursor.close()
        conn.close()
        logger.info("🌅 所有设备的喂食任务状态已重置为 0")
    except Exception as e:
        logger.error(f"🔥 重置任务状态失败: {e}")

# 启动调度器
def run_feed_scheduler():
    scheduler = BackgroundScheduler()
    scheduler.add_job(check_and_trigger_tasks, 'cron', minute='*')
    scheduler.add_job(reset_feed_status_daily, 'cron', hour=0, minute=0)
    scheduler.start()
    logger.info("🟢 APScheduler 已启动，任务调度开启")
def reset_feed_task_status():
    """重置所有设备的喂食任务状态，每天 0 点执行，或手动调用"""
    try:
        conn = get_db_connection()
        cursor = conn.cursor()
        cursor.execute("""
            UPDATE device_info
            SET feed_task_status1 = 0,
                feed_task_status2 = 0,
                feed_task_status3 = 0,
                feed_count = 0
        """)
        conn.commit()
        cursor.close()
        conn.close()
        print("🌅 所有设备的任务状态已手动重置为 0")
    except Exception as e:
        print(f"❌ 手动重置失败：{e}")


