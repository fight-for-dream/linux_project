# i.MX6ULL HCM Monitoring and Control System

基于百问网 i.MX6ULL_PRO 开发板实现的嵌入式 Linux 综合监测与控制项目。

本仓库保存在 PC 上，用于管理自主编写的驱动、应用程序、设备树修改片段和板端启动脚本。交叉编译、完整设备树合并与 DTB 生成均在 Ubuntu 中完成。

## 硬件模块

- DHT11 温湿度传感器
- ADXL345 三轴加速度传感器
- AT24C02 EEPROM
- 四相步进电机
- 1024×600 LCD 与 Goodix 触摸屏
- 百问网 i.MX6ULL_PRO 开发板

## 主要功能

- DHT11、ADXL345、EEPROM 和电机 Linux 驱动
- 多线程传感器采集与故障管理
- 温度、湿度和振动报警
- EEPROM 配置持久化
- CSV 历史数据记录
- LVGL 触摸界面
- 电机自动与手动控制
- MQTT 数据上报
- MQTT 远程电机控制与参数配置
- MQTT 在线状态和遗嘱消息
- 开机自动加载驱动并启动应用

## 目录说明

| 目录 | 内容 |
| --- | --- |
| `01_dht11`～`04_motor` | 驱动、测试应用和设备树修改片段 |
| `01_dht11_irq_fix` | DHT11 时序稳定性修复版 |
| `02_adxl345_devid_fix` | ADXL345 设备 ID 校验修复版 |
| `05_monitor_app`～`09_runtime_config` | 非 LVGL 综合应用的阶段版本 |
| `10_lvgl_ui`～`15_fault_management` | LVGL 界面与系统集成阶段版本 |
| `16_mqtt_publish_test` | MQTT 独立发布测试 |
| `17_mqtt_system_publish`～`20_mqtt_connection_status` | MQTT 综合功能阶段版本 |
| `21_system_autostart` | 板端启动配置和启动脚本 |
| `general_Makefile` | 通用 Makefile 模板与示例 |

各编号目录用于保留学习和开发过程，因此相邻阶段中会存在重复源码。

## 开发环境

- Linux 4.9.88
- Buildroot 2020.02
- `arm-buildroot-linux-gnueabihf-gcc` 7.5.0
- LVGL 8
- [mqttclient](https://github.com/jiejieTop/mqttclient)
- Mosquitto Broker

## 构建方式

### 驱动和设备树

仓库中的 `.dtsi` 文件是设备树修改片段。需要在 Ubuntu 中将其内容合并到实际内核设备树，再编译 DTB。驱动模块也在 Ubuntu 的 Linux 4.9.88 内核构建环境中交叉编译。

### LVGL 综合应用

第 10 阶段之后的目录只保存本项目应用源码。完整 LVGL 源码、Linux framebuffer/evdev 端口及其 Makefile 位于 Ubuntu 构建环境，不包含在本仓库中。编译时将目标阶段的源码放入 LVGL 工程并链接 mqttclient 库。

### 部署

编译生成的 `.ko`、DTB、静态库和 ARM 可执行文件通过 ADB 传输到开发板。这些构建产物不纳入 Git 版本管理。

## 运行设备节点

```text
/dev/hcm_dht11
/dev/hcm_adxl345
/dev/hcm_eeprom
/dev/hcm_motor
```

`21_system_autostart/hcm.conf` 中的板端路径和 Broker 地址属于部署参数，使用前应根据实际环境调整。

