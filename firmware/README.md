# SK32 键盘固件

这里存放 SK32F077 键盘的**预编译固件**，clone 仓库后无需自己编译，直接烧录即可使用。

## 固件列表

| 文件 | 对应键盘 | VID:PID | 说明 |
|---|---|---|---|
| `sk32_kb17_default.bin` | KB17（W17PAD） | `0x0194:0x0463` | 17 键数字小键盘，VIA 支持 |
| `sk32_ld7_oled_default.bin` | LD7_OLED | `0x0194:0x0464` | 7 键，OLED 统计显示 + 7 灯 RGB，VIA 支持 |

> 固件都从 flash 默认键位启动；上电后 **EEPROM 中旧的 VIA 配置会保留**（如需彻底清空见下文"重置"）。

## 进入 DFU 模式（烧录前必读）

两块键盘共用 sk32duino 引导（16KB，App 从 0x08004000 开始）。进入 DFU 的方法：

1. **按住 KEY1（矩阵 [0][0]，KB17/LD7 均为 PB2 × PB12 那个键）不松手**；
2. **按一下板子上的复位键**（或重新插拔 USB）；
3. 松手。此时电脑上会枚举出一个 **`1EAF:0003`**（stm32duino 风格）的 DFU 设备；
4. 进入 DFU 后，**需要在 DFU 状态下载入固件或等待其自动复位**，之后才会重新枚举为正常键盘。

## 方法一：QMK Toolbox（图形界面，推荐新手）

1. 下载安装 [QMK Toolbox](https://github.com/qmk/qmk_toolbox/releases)；
2. 打开后点击 **Open** 选择上面的 `.bin` 文件；
3. 按上面方法进入 DFU（设备列表里出现 `1EAF:0003`）；
4. 点击 **Flash**。刷完自动复位，键盘变成 HID 键盘。

## 方法二：dfu-util（命令行）

### 获取 dfu-util

- **Windows（推荐）**：本仓库配套的 MSYS 环境自带，路径一般为
  `D:\QMK_MSYS\mingw64\bin\dfu-util.exe`；
- **独立安装**：从官方 GitHub 下载 Windows 版：
  <https://github.com/dfu-util/dfu-util/releases>（解压后 `dfu-util.exe`）；
- **Linux**：`sudo apt install dfu-util`；
- **macOS**：`brew install dfu-util`。

### 烧录命令

```
dfu-util -a 2 -D sk32_kb17_default.bin          # KB17
dfu-util -a 2 -D sk32_ld7_oled_default.bin      # LD7_OLED
```

要点：

- `-a 2`：sk32duino 的 **App 介质是 DFU 备用接口 2**，必须指定；
- 先进入 DFU（出现 `1EAF:0003`）再执行命令；
- 刷写成功后板子自动复位并重新枚举为 HID 键盘。

## 方法三：一键脚本（Windows，需本机 QMK MSYS 环境）

每个键盘目录下都有一键"编译 + 等待 DFU + 烧录"脚本：

- `keyboards/sk32/kb17/flash.bat`
- `keyboards/sk32/ld7_oled/flash.bat`

双击运行，按提示在 45 秒内进入 DFU 即可自动烧录。

## 重置（清空 EEPROM / VIA 配置）

- **VIA 里重置**：VIA 软件中加载对应 `via.json` 后执行重置，或在固件配套指令下清空；
- **恢复出厂键位**：刷入固件后首次启动会用 flash 默认键位重建 EEPROM；
- 若需要彻底清除 EEPROM，可重新烧录一次固件（部分状态下引导会触发配置重置）。

## 如何自己编译（可选）

需要 QMK MSYS 环境（MINGW64）：

```
qmk compile -kb sk32/kb17 -km default
qmk compile -kb sk32/ld7_oled -km default
```

产物在 `.build/` 下；VIA 配置文件在各自键盘目录：`keyboards/sk32/kb17/via.json`、`keyboards/sk32/ld7_oled/via.json`。
