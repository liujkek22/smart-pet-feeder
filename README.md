# Smart Pet Feeder
# 🐾 Smart Pet Feeder 智能宠物喂食系统

本项目是一套基于 **ESP32 嵌入式控制**、**Flask + WebSocket 服务端** 和 **微信小程序前端** 的智能宠物喂食系统。通过三端协作，用户可远程实现对宠物喂食、喂水、水位监测、水温控制等操作，并接收设备状态反馈，实现宠物看护智能化。

---

## 📁 项目结构

```
smart-pet-feeder/
├── target_copy/          # ESP32 嵌入式代码（ESP-IDF）
├── server/            # Python Flask WebSocket 后端服务
├── EspEyeForWeChat-masterp/           # 微信小程序前端界面（基于 Vant）
├── README.md          # 项目说明文档
└── .gitignore         # Git 忽略规则
```

---

## ✨ 核心功能

* ✅ 小程序远程控制投喂与加水
* ✅ 支持定时喂食调度
* ✅ 水位与水温实时检测与上传
* ✅ 温度调节控制系统（自动加热）
* ✅ 微信订阅消息推送（设备掉线/异常提醒）
* ✅ 支持设备注册、在线状态判断
* ✅ ESP32 端与服务器长连接保持（WebSocket）
* ✅ MySQL 数据库存储设备信息与状态

---

## 🧪 软硬件环境

### 🖥 服务端

* Python 3.8+
* Flask + WebSocket
* MySQL
* Nginx (用于托管静态资源)

### 📱 小程序端

* 微信开发者工具
* Vant Weapp UI 库
* 云开发 or API 调用支持

### 🔧 嵌入式端

* ESP32-WROOM-S3
* 开发环境：ESP-IDF v5.0+
* 模块使用：

  * 舵机 / 步进电机控制喂食
  * 双通道水泵控制加水
  * DS18B20 监测水温
  * HX711 监测剩余食量
  * 电压型水位传感器监测水位

---

## 🚀 快速开始

### 1️⃣ 服务端

```bash
cd server/
pip install -r requirements.txt
python app.py
```

> 确保你已配置好数据库连接，并已初始化表结构。

---

### 2️⃣ 嵌入式端（ESP32）

```bash
cd embedded/
idf.py menuconfig     # 配置 WiFi 和服务器地址
idf.py build
idf.py flash -p /dev/ttyUSB0
idf.py monitor
```

---

### 3️⃣ 小程序端

* 使用微信开发者工具打开 `miniapp/`
* 配置基础 API 地址（可使用 config.js 或环境变量）
* 上传测试或真机预览

---

## 🧐 数据交互说明

* **设备注册**：设备上电后向服务器发送 `{action: "register", UUID: "xxx"}`，建立 WebSocket 连接
* **心跳包**：设备每 30 秒发送一次 `heartbeat`，维持连接状态
* **数据上传**：包括水位、水温、喂食次数等时间上报
* **命令下发**：小程序请求 API，服务器通过 WebSocket 向设备下发 JSON 命令

---

## 📷 系统展示图（可选）

> 可在此处添加系统架构图、界面截图等内容，增强项目完整度。

---

## 📌 注意事项

* 请勿将 `.pio/`, `build/`, `node_modules/` 等中间产物加入版本控制
* 建议使用 `.env` 管理服务器配置或密钥信息
* 推送前建议执行 `git status` 检查变动内容

---

## 📄 License

本项目仅用于学术研究和毕业设计使用，禁止用于任何商业行为。
