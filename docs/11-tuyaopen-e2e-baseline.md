# 11 — 涂鸦端到端基线（TuyaOpen，仍为 FreeRTOS）

目标：在同一块联泓 T5-E1 板上，先用 TuyaOpen 的命令行工具把「建产品 → 编译 → 烧录 → App 配网 → 激活上云」走通，
再把 AP 核的 FreeRTOS 换成 XiZi。换内核后出问题时，可以据此区分内核问题和涂鸦侧问题。

## 1. 产品

- 用 `tuya-devplat-cli product copy` 从已有的 T5-E1 AI 产品复制出新产品「小秀」，品类 AI Toy，模组 T5-E1，Wi-Fi + Bluetooth。
  `category-tree`、`solution-category`、`solution-list` 三个查询接口在 2026-09-16 均返回 `Remote api run unknown failed`，
  `product create-common` 缺方案 ID 无法调用，所以走复制。
- CLI 的鉴权从 TuyaOpen IDE 的登录态换取，token 放在本地文件，不进仓库。
- PID 不写进仓库，`tuyaopen-overlay/app/xiaoxiu/app_default.config` 里用 `<your-PID>` 占位。

## 2. 板级

`tuyaopen-overlay/boards/T5AI/LINKH_T5E1/`，并入 TuyaOpen SDK 的 `boards/T5AI/`，`Kconfig.patch` 把它加进板子选择菜单。

- 厂商未提供引脚表，原厂 TuyaOS 把板级配置存在加密 KV 里，无法从固件读出。
- 因此只注册片上音频编解码器，不注册 LED 和按键，避免驱动未知线路。
- 喇叭使能脚填 56。T5 平台的 `tkl_audio.c` 只在 `spk_gpio < 56` 时写 GPIO，等于不控功放；代价是暂时没有声音。

## 3. 日志

TuyaOpen 在 T5 上默认把系统日志打到 UART1（P0/P1），本板没有引出。
`tal_cli_init()` 会按 115200 打开 UART0，所以在它之后加一行 `tal_log_add_output_term("uart0", user_log_output_cb)`
（`tuyaopen-overlay/app/xiaoxiu/tuya_main.c.patch`），TAL 层日志就会出现在 RX0/TX0 上，和 `tuya>` 命令行共用。
Armino 自身的 `bk_printf` 仍在 UART1，看不到。

`tuya>` 默认只有 `help`、`version`、`auth`、`sys_reboot` 等少数命令；
打开 `CONFIG_ENABLE_SERIAL_CLI_CMD=y` 后才有 `sys_wifi_scan`、`sys_iot_reset`、`sys_status`、`kv_*`。

## 4. 烧录

- `tos.py build` 产出的 `<proj>_QIO_<ver>.bin` 本身就是带 32+2 CRC 的物理镜像，布局
  bootloader 0x0 / tuyaboot 0x11000 / CP 0x22000 / AP 0x132000，与板上原厂固件布局一致。
- 长度不是 4K 整数倍，按 `docs/10` 补 0xFF 后用 bk_loader 从 0x0 烧写（`-e 1 -r`，460800）。
- `tos.py flash` 走 tyutool，本机没有配置，未使用。
- 覆盖前对当前这块板做了整片备份。注意 2026-09-16 22:09 换过板：两块板的 bootloader 和 CP 相同，
  授权分区（0x7CD000 起 21 个扇区）、`sys_rf`、`sys_net` 不同，整片备份不能跨板恢复。

## 5. 实测（2026-09-16 23:57 起）

启动日志（UART0）：

```
tuya_iot_init
tuyaopen_license_read read failure      ← KV 里是原厂 TuyaOS 的加密数据，回落到编译进去的授权
activate config not found               ← 未激活
ble adv updated 0                       ← 开始蓝牙广播
Activation data read fail, go activation mode...
Tuya Event ID:1(TUYA_EVENT_BIND_START)
```

App 配网：

```
Ble Connected
Ble is paired
token: ********, region: AY
wifi connnet <ssid>
Tuya Event ID:2(TUYA_EVENT_BIND_TOKEN_ON)
auto conn timeout cnt N, stat 3         ← stat 3 = WSS_NO_AP_FOUND
```

- App 到设备的蓝牙链路和令牌下发正常。
- 第一次选的热点不在 2.4G 上，设备报 `WSS_NO_AP_FOUND` 反复重试。T5 只支持 2.4G。
- 已拿到令牌后，App 再次连蓝牙下发的配置会被忽略。重启一次即回到干净的配网状态，
  因为令牌和 SSID 只在内存里，还没有激活，也没有写入 KV。
- 激活上云尚未完成，等 2.4G 热点就绪后复测。

## 6. 后续

1. 2.4G 热点上完成激活，确认 MQTT 在线。
2. 查到功放使能脚和按键脚后，补进 `LINKH_T5E1`。
3. 云端智能体「小秀」（`tuya-devplat-cli project ...`）。
4. AP 核换 XiZi：同一个应用、同一套 CP，走完同样的配网流程。
