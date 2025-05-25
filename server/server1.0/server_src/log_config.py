import logging
import os

def setup_logger():
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)

    # 创建 logs 文件夹
    os.makedirs('logs', exist_ok=True)
    log_file = 'logs/server.log'

    # 创建日志处理器
    file_handler = logging.FileHandler(log_file, encoding='utf-8')
    console_handler = logging.StreamHandler()

    formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
    file_handler.setFormatter(formatter)
    console_handler.setFormatter(formatter)

    # 避免重复添加 handler
    if not logger.handlers:
        logger.addHandler(file_handler)
        logger.addHandler(console_handler)

    return logger
