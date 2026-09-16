# 00 · 获取上游

本项目**不包含** XiUOS 与 TuyaOpen SDK 两个上游（体积过大，且都需要 fork 自维护）。
按下列步骤单独获取。

## XiUOS

```bash
git clone --depth 1 --single-branch --branch master \
    https://www.gitlink.org.cn/xuos/xiuos.git xiuos
```

- **唯一真源是 gitlink**。GitHub 镜像 `xuos/xiuos_mirror` 落后（master `49b20dfa`），
  且 `forgeplus.trustie.net` 上原地址已 404。
- 期望 HEAD：`0af75ee8c0aaf611b9b17b7da41bc593937d5e4c`（2026-02-05）
- 浅克隆体积约 **604.6 MB / 15,906 文件**
- 目标子目录：`Ubiquitous/XiZi_IIoT_Macro/`

## TuyaOpen SDK

推荐用 TuyaOpen IDE 安装（会自动 clone 并初始化环境），默认落在
`~/TuyaOpenIDE/TuyaOpenSDK`。

或手动：

```bash
git clone https://github.com/tuya/TuyaOpen.git TuyaOpenSDK
cd TuyaOpenSDK && ./export.sh        # Windows: .\export.ps1
```

环境变量（供 `tools/` 下脚本使用）：

```powershell
$env:TUYAOPEN_SDK = "$env:USERPROFILE\TuyaOpenIDE\TuyaOpenSDK"
```

## 复现本项目的测量

```powershell
$env:TUYAOPEN_SDK = "$env:USERPROFILE\TuyaOpenIDE\TuyaOpenSDK"
python tools/nm_closed_libs.py          # -> analysis/closed-lib-rtos-symbols.txt
python tools/scan_freertos_surface.py   # -> analysis/freertos-surface.txt
```
