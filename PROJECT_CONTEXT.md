# i.MX6ULL HCM 项目上下文

> 最后整理：2026-09-21
> Git 基线：`a5d67ebb94e75196626d60b7f5a7bf2a087bb4d7`
> 本文把源码事实、用户提供的实测输出和待确认项分开记录。实际源码与最新板端
> 输出优先于本文；环境变化后应同步更新本文。

## 1. 项目定位与完成状态

本项目基于百问网 i.MX6ULL_PRO 开发板，实现环境与振动监测、参数持久化、
告警、电机控制、LVGL 触摸界面、MQTT 远程监控和开机启动。

截至本次整理，阶段 01～19 的主体功能以及阶段 21 的驱动加载/应用启动流程已由
用户分阶段验证；阶段 20 的源码存在，但其当前板端部署状态仍待确认。仓库保留
学习和集成过程中的阶段快照，因此相邻目录存在重复代码。功能最完整的源码位于
`20_mqtt_connection_status`；`21_system_autostart` 是独立启动配置，当前指向
阶段 19 的板端可执行文件。

## 2. 三套开发环境

### Windows PC：源码、Codex 与 Git

| 项目 | 当前记录 |
| --- | --- |
| 项目仓库 | `D:\Linux\imx6ull-code-codex\hcm_project` |
| GitHub | `https://github.com/fight-for-dream/linux_project` |
| 内核源码副本 | `D:\Linux\Linux-4.9.88` |
| mqttclient 源码副本 | `D:\Linux\imx6ull-code-codex\mqttclient` |
| 原理图和模块资料 | `D:\Linux\imx6ull\02_100ask_imx6ull_pro_2022.08\04_开发板原理图` |

Windows 侧主要负责编辑、审查和 Git 管理。当前 PATH 中有 Git、OpenSSH、
Node.js、Python 和 ripgrep；没有 `adb`、`make`、ARM 交叉编译器或 `gh`，
因此不能在 Windows 当前环境直接完成本项目构建和部署。

### Ubuntu 虚拟机：交叉编译

| 项目 | 当前记录 |
| --- | --- |
| 用户 | `book` |
| 内核构建树 | `/home/book/100ask_imx6ull-sdk/Linux-4.9.88` |
| 交叉编译器 | `/home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-gcc` |
| 编译器版本 | GCC 7.5.0，Buildroot 2020.02 |
| LVGL 工程 | `/home/book/hcm/lvgl/lv_port_linux_frame_buffer_imx6ull/lv_port_linux_frame_buffer_imx6ull` |
| mqttclient 工作目录 | 曾在 `/home/book/hcm/mqtt/mqttclient` 操作；最终 include、静态库输出和链接路径待确认 |

驱动、应用、LVGL、mqttclient 静态库及 DTB 都在 Ubuntu 构建。阶段 10～20 只
保存项目自身源码，需要放入外部 LVGL 工程并链接 mqttclient 静态库。

### i.MX6ULL 开发板：运行与硬件验证

用户提供的实测环境：

```text
Linux 100ask 4.9.88 #5 SMP PREEMPT armv7l GNU/Linux
Linux version 4.9.88 (book@100ask), GCC 7.5.0
/proc/device-tree/model: Freescale i.MX6 ULL 14x14 EVK Board
```

上述 model 字符串来自设备树；实际硬件是百问网 i.MX6ULL_PRO。

| 项目 | 当前部署记录 |
| --- | --- |
| 驱动模块目录 | `/root/hcm/modules_project` |
| 当前自启动应用 | `/root/hcm/19_mqtt_remote_config/lv_hcm_test` |
| 自启动配置目录 | `/root/hcm/21_system_autostart` |
| 应用日志 | `/tmp/hcm_app.log` |
| 传输方式 | ADB（在 Ubuntu 使用） |

开发板目前通过手机热点联网。当前记录的 Mosquitto Broker 位于 Windows PC，
地址为 `10.175.22.126:1883`；该 IP 是部署参数，热点变化后可能失效。

## 3. 硬件资源与驱动接口

| 模块 | 总线/资源 | 关键参数 | 设备节点 |
| --- | --- | --- | --- |
| DHT11 | `GPIO4_IO23`，PAD `CSI_DATA02` | 最短读取间隔 2 秒 | `/dev/hcm_dht11` |
| ADXL345 | ECSPI1 CS0，SPI mode 3 | 1 MHz，ID `0xE5`，100 Hz，FULL_RES ±2 g | `/dev/hcm_adxl345` |
| AT24C02 | I2C1，7 位地址 `0x50` | 256 B，8 B/page | `/dev/hcm_eeprom` |
| 四相步进电机 | `GPIO4_IO19`～`GPIO4_IO22` | 8 拍相序 | `/dev/hcm_motor` |

**当前四模块共存的前提：**ADXL345 INT1 接到 `GPIO4_IO20`，但当前驱动不使用中断；
EEPROM 扩展口的 `GPIO4_IO22` 对应模块引脚未连接。当前软件不会重复申请这些
资源，四个驱动已实测可同时加载和运行。将来增加 ADXL345 中断或连接辅助引脚
时必须重新检查与电机的冲突。

板级 `100ask_imx6ull-14x14.dts` 的旧 `/dht11` 节点使用 `GPIO4_IO19`。
当前 `hcm-dht11.dtsi` 会先删除旧节点，再把 DHT11 迁移到 `GPIO4_IO23`；
如果实际 DTS 仍有旧节点，合并时必须保留该删除操作，否则会与电机冲突。

显示与输入：

- `/dev/fb0`：`mxs-lcdif`，1024×600，32 bpp。
- Goodix 触摸屏当前实测为 `/dev/input/event1`，已观察到多点坐标和 `BTN_TOUCH` 事件；event 编号可能随枚举变化，部署后应从 `/proc/bus/input/devices` 重新确认。

## 4. 独立驱动基线

| 模块 | 推荐参考目录 | 已确认实现 |
| --- | --- | --- |
| DHT11 | `01_dht11_irq_fix` | GPIO 时序采集；稳定版在关键采样期间处理抢占和本地中断 |
| ADXL345 | `02_adxl345_devid_fix` | SPI 字符设备；读取时校验 `0xE5`，用于发现断线/浮空数据 |
| EEPROM | `03_eeprom` | `read/write/llseek`；跨 8 字节页写入 |
| 电机 | `04_motor` | 用户态下发方向、步数和步间隔；`write()` 完成动作后返回 |

EEPROM 单模块测试区是 `0x80`；综合程序的正式配置区是 `0x00`，二者用途不同。

## 5. 阶段目录

| 阶段 | 主要内容 |
| --- | --- |
| `01_dht11`、`01_dht11_irq_fix` | DHT11 驱动、应用、设备树及稳定性调整 |
| `02_adxl345`、`02_adxl345_devid_fix` | ADXL345 驱动、应用、设备树及 ID 校验 |
| `03_eeprom` | AT24C02 驱动、应用和设备树 |
| `04_motor` | 步进电机驱动、应用和设备树 |
| `05_monitor_app` | 单线程综合监测 |
| `06_monitor_thread` | 多线程传感器采集 |
| `07_alarm_control` | 告警与条件变量电机控制 |
| `08_history_log` | CSV 历史记录 |
| `09_runtime_config` | 命令线程和 EEPROM 参数持久化 |
| `10_lvgl_ui` | 独立 LVGL 界面 |
| `11_system_integration` | LVGL 与四个设备集成 |
| `12_ui_settings`～`14_ui_motor_control` | 参数界面、异步保存、手动/自动电机控制 |
| `15_fault_management` | 传感器故障检测与恢复 |
| `16_mqtt_publish_test` | MQTT 独立发布测试 |
| `17_mqtt_system_publish` | 周期发布综合系统状态 |
| `18_mqtt_remote_control` | MQTT 远程电机控制 |
| `19_mqtt_remote_config` | MQTT 远程参数修改、保存与反馈 |
| `20_mqtt_connection_status` | 在线状态、保留消息、LWT 和 UI 状态 |
| `21_system_autostart` | 加载四个模块并启动阶段 19 应用 |

## 6. 综合应用架构

阶段 20 打开四个设备节点并包含七个工作线程：DHT11、ADXL345、电机、CSV
日志、EEPROM 保存、终端命令和 MQTT。LVGL 在主线程运行。

关键行为：

- UI 数据刷新周期 200 ms，日志周期 5000 ms。
- 传感器连续读取失败 3 次后进入故障状态。
- 振动连续 3 次达到阈值后报警。
- 默认电机动作 512 步，步间隔 3000 μs；正转表示打开，反转表示关闭。
- 支持 `SIGINT` 和 `SIGTERM`，退出时唤醒并回收工作线程。
- 历史日志为 `/tmp/hcm_history_fault.csv`。

EEPROM 配置结构包含 magic `0x48434d31`、版本 1、五个参数和校验值。

| 参数 | 默认值 | 合法范围 |
| --- | ---: | ---: |
| 温度阈值 | 26 °C | -40～80 |
| 湿度阈值 | 60 %RH | 1～100 |
| 振动阈值 | 30 mg | 1～16000 |
| DHT11 周期 | 2000 ms | 2000～60000 |
| ADXL345 周期 | 200 ms | 10～10000 |

## 7. MQTT 接口

完整应用启动格式：

```text
lv_hcm_test <broker_ip> [port]
```

默认端口为 1883。

| Topic | 行为 |
| --- | --- |
| `device/imx6ull/data` | 每 5 秒发布系统 JSON，QoS 1 |
| `device/imx6ull/control/motor` | 接收 `auto/manual/open/close`，QoS 1 |
| `device/imx6ull/control/config` | 接收 `set ...` 和 `save`，QoS 1 |
| `device/imx6ull/config/result` | 返回配置命令结果，QoS 0 |
| `device/imx6ull/status` | 阶段 20 发布 retained `online/offline`，QoS 1，LWT 为 `offline` |

初始 `mqtt_connect()` 失败时每 3000 ms 重试。连接建立后的掉线恢复时序受当前
mqttclient 版本和内部机制影响，仓库源码不能完整确认，仍需结合实际库版本和
断网测试。当前实现没有用户名、密码、TLS 或可配置 Topic 前缀，适用于当前可信
局域网实验环境。

## 8. 构建与部署边界

```text
Windows 源码/Git
        ↓ 同步到 Ubuntu
Ubuntu Linux 4.9.88 + ARM 工具链 + LVGL + mqttclient
        ↓ 生成 .ko / DTB / ARM 应用
ADB 传输到开发板
        ↓
insmod、运行应用、dmesg/日志/硬件验证
```

- 仓库中的 `.dtsi` 是 include 片段，不是独立 DTS 或运行时 overlay。
- 目标板级 DTS 在注释中记录为 `100ask_imx6ull-14x14.dts`；实际启动的 DTB
  文件名、安装位置和 bootloader 选择逻辑仍待确认。
- 驱动 Makefile 使用 Ubuntu 内核构建树
  `/home/book/100ask_imx6ull-sdk/Linux-4.9.88`。
- LVGL 与 MQTT 最终应用缺少仓库内可复现的一键构建脚本。
- 编译产物通过 ADB 部署，不进入 Git。

## 9. 自启动配置

`21_system_autostart/S99hcm` 支持 `start|stop|restart|status`，依次加载：

```text
dht11_drv.ko
adxl345_drv.ko
eeprom_drv.ko
motor_drv.ko
```

脚本使用 `/var/run/hcm_app.pid`，忽略 SIGHUP，将输入重定向自 `/dev/null`，
把输出追加到 `/tmp/hcm_app.log`。它不管理网络，`stop` 也不卸载驱动。
`stop` 最多等待进程 10 秒，之后会删除 PID 文件，但不会发送 SIGKILL 或再次确认
进程已经退出；若应用未响应 SIGTERM，可能遗留进程并导致后续 `status` 误报。

当前 `hcm.conf` 明确启动阶段 19。阶段 20 是否已经编译并部署为自启动程序
尚未确认。

## 10. Codex 能力检查（2026-09-21）

本节是指定日期的环境快照，不是永久能力清单；Codex 版本、插件、MCP、工作区
和有效权限应在需要时重新检查。

### 已有能力

- Codex CLI `0.154.0-alpha.6.2`，已启用多代理。
- 已启用浏览器/Computer Use、可视化、文档、PDF、表格、演示文稿和模板相关
  插件；配置了 `node_repl` 与 Playwright Edge MCP。
- 已安装系统 Skill：imagegen、openai-docs、plugin-creator、skill-creator、
  skill-installer；当前运行环境还提供 computer-use、visualize、documents、pdf、
  spreadsheets、presentations、template-creator 和 plugin-management 等能力。
- Git 2.31.1、OpenSSH 9.5、Node.js、Python 3.8 和 ripgrep 可用。
- 仓库 `main` 跟踪 `origin/main`，远端为 `fight-for-dream/linux_project`。

### 本次配置前的缺失与限制

- 全局 `C:\Users\19174\.codex\AGENTS.md` 是空文件；项目此前没有
  `AGENTS.md` 或自建 Skill。
- 本次会话工作区是 `D:\Linux\Linux-4.9.88`，不是 HCM 仓库；以后应直接
  以 `hcm_project` 打开 Codex，才能自动加载本项目规则和 Skill 并正常写入。
- Codex 配置尚未把 `D:\Linux\imx6ull-code-codex\hcm_project` 列为 trusted
  project；首次从该目录打开时应确认信任。
- Windows 没有 ADB、ARM 交叉编译器、make 或 GitHub CLI。
- Windows OpenSSH 可用，但当前没有 `~/.ssh/config`、密钥或 Ubuntu 主机配置。
- 当前 Codex 不能直接进入 Ubuntu 编译，也不能直接运行 ADB、读取板端日志或
  确认实时硬件状态。
- GitHub 插件与 Codex Security 插件尚未安装；Git HTTPS 推送已经可用。
- 浏览器自动化曾出现 request-header policy 初始化失败，使用前需按会话确认
  是否可用。

### 本次新增的持久能力

- 根目录 `AGENTS.md`：每次项目会话自动提供稳定规则。
- 本文：集中保存环境、硬件、路径、阶段和待确认项。
- `.agents/skills/imx6ull-hcm-development`：按需加载本项目开发与验证流程。

## 11. 待确认和后续能力缺口

1. Ubuntu 虚拟机当前 IP、SSH 服务、密钥登录和 Codex 远程连接。
2. Ubuntu 中 mqttclient 的精确 include、静态库输出和最终链接命令。
3. LVGL 与 mqttclient 的精确版本或 Git commit。
4. 实际启动 DTB 的文件名、板端路径和更新命令。
5. 当前板端实际使用的 DHT11/ADXL345 修复版模块及其构建提交。
6. 阶段 20 是否已部署，以及开机程序是否计划从阶段 19 切换到阶段 20。
7. 开发板当前 IP、热点变化后的 PC/Broker 地址。
8. Mosquitto 是否需要认证、TLS 和持久化。
9. 最终 LVGL/MQTT 应用的一键构建、部署、日志收集和冒烟测试脚本。
10. 各阶段的硬件测试记录、压力测试、传感器校准和阈值依据。
11. 顶层许可证、CI 和 GitHub 分支/PR 规则。

## 12. 维护规则

- 新事实只有在源码、命令输出或用户明确确认后写入。
- 变化频繁的 IP、路径和部署阶段要附“最后确认日期”。
- 不记录账号密码、Token、私钥、热点密码或其他凭据。
- 环境发生变化时同时检查 `AGENTS.md`、本文和 Skill 是否需要更新。
