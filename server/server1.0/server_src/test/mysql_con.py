import mysql.connector

# 连接到 MySQL 数据库
connection = mysql.connector.connect(
    host="localhost",        # 数据库主机地址
    user="root",    # 数据库用户名
    password="your_password",# 数据库密码
    database="pet_feeder_test"       # 选择的数据库
)

# 创建游标对象
cursor = connection.cursor()
uuid='123e4567-e89b-12d3-a456-426614174000'
# 执行查询
# 查询数据库检查设备是否存在
query = "SELECT status FROM device_info WHERE uuid = %s"
cursor.execute(query, (uuid,))  # 执行查询并传递 UUID

# 获取查询结果
result = cursor.fetchone()  # 返回查询的第一行数据
print(result[0])

# 关闭游标和连接
cursor.close()
connection.close()
