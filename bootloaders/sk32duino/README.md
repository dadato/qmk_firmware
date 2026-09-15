# sk32duino —— SK32F077 USB DFU Bootloader

针对 3Think SK32F077（Cortex-M0，与 STM32F072 寄存器兼容，但 **flash 页擦粒度不同、USB IP 不同、无 VTOR**）的 stm32duino 风格 DFU bootloader。

- 固件占用 flash `0x08000000 .. 0x08003FFF`（16 KB，"Boot area 16K"），从复位向量接管。
- QMK 应用链接在 `0x08004000`（见 `sk32duino.ld` 与 `../kb17/ld/`），通过 USB DFU 在线烧录。
- USB 枚举为 stm32duino 风格：VID:PID `1EAF:0003`，App 介质为 **alt 2**。

---

## 目录结构

```
bootloaders/sk32duino/
├── source/
│   ├── main.c          # bootloader 入口：DFU 判定、应用跳转、会话锁存
│   ├── usbdfu.c        # USB DFU 设备端（整协议跑在 EP0 中断，无线程）
│   ├── usbdfu.h        # DFU 请求/状态枚举、会话锁存宏
│   ├── dfu_target.c    # SK32 片上 flash 编程后端（2KB 页、懒擦、erase_pause）
│   └── dfu_target.h    # flash 几何常量
├── cfg/
│   ├── chconf.h        # 最小化 RT 配置（build_bl.sh 会强制裁剪）
│   ├── halconf.h
│   └── mcuconf.h
├── build_bl.sh         # 编译入口：先强制最小化 chconf，再 make
├── Makefile
├── sk32duino.ld        # 16KB 链接脚本
└── build/              # 构建产物 + 大量 openocd / ps1 调试与取证脚本
```

---

## Flash 布局（与 STM32F072 不同的关键点）

| 区段 | 地址 | 说明 |
|---|---|---|
| Bootloader | `0x08000000 .. 0x08003FFF` | 16 KB，本固件 |
| Application | `0x08004000` | QMK 固件（最大可下载 = F_SIZE − 16 KB，运行期读取） |
| 总 flash | 0x08000000 .. (F_SIZE 决定) | F_SIZE 寄存器运行期读取（64 KB / 128 KB），页粒度 **2 KB** |

**flash 页擦粒度 = 2 KB（`SK32_FLASH_PAGE_SIZE = 0x800`）**，实测证实，**不是** STM32F072 的 1 KB。擦除命令地址落在某 2 KB 页内即擦整个 2 KB 页（地址被硬件掩到页底）。所有页操作必须按 2 KB 页对齐。

| 关键宏（`dfu_target.h`） | 值 |
|---|---|
| `SK32_FLASH_BASE` | `0x08000000` |
| `SK32_BOOT_SIZE` | `0x4000` (16 KB) |
| `SK32_APP_BASE` | `0x08004000` |
| `SK32_FLASH_SIZE_REG` | `((volatile uint32_t *)0x1FFFF7CC)` (价值 = KB) |
| `SK32_FLASH_SIZE_MASK` | `0x000000FF`（高字节硅片未驱动，读回为 1，必须按此掩码取容量） |
| `SK32_FLASH_PAGE_SIZE` | `0x800` (2 KB) |

---

## SRAM 握手槽（Cortex-M0 无 VTOR 的补偿）

物理 SRAM 为 10 KB（`0x20000000 .. 0x200027FF`）。两个链接脚本都把 RAM 起点设为 `0x20000200`，前 512 字节保留作向量表拷贝区，并在此区尾部放置跨复位存活的握手槽（System reset 不清 SRAM）：

| 地址 | 用途 |
|---|---|
| `0x200001E4 / E8` | "运行应用" 双字签名（`0x4B1E2F51` + `0x33325453`），跳转前清零 |
| `0x200001F0` | 应用写入的 **DFU 请求 magic** `0x4B32D7A1`（QK_BOOT） |
| `0x200001F4` | **会话锁存**：`KEYED=1` / `TOUCHED=2` |

**DFU 跳转流程**：应用复位向量表先从 flash 拷贝到 SRAM（512B），再通过 `SYSCFG->CFGR1 MEM_MODE` 将 SRAM 重映射到地址 0，最后直接 `msr msp + bx` 分支到应用复位向量。校验 SP∈[0x20000200,0x20002800]、PC∈[0x08004000, 0x08000000+F_SIZE] 且 Thumb 位为 1，防止跳到被擦坏的镜像。

---

## 进入 DFU 的方式

1. **Boot key 复位进入**（两选一，`main.c` 顶部宏）：
   - `SK32_DFU_KEY_KB17`（默认）：KB17/W17PAD 的 NumLock，即矩阵 `[0][0]` = 行 `PB2` × 列 `PB12`。复位时按住。
   - `SK32_DFU_KEY_ONEKEY`：onekey 直连键 `PB5`（内上拉，按下为低）。
   - 都不定义则只能走方式 2。
2. **应用内 QK_BOOT**：应用写 magic 后复位。
3. **ST-Link 虚拟拔插**：App 运行态按住 boot key 再对板子复位即可进入 DFU；DFU 下载完若主机未识别新设备，再点一次 ST-Link 复位即完成到 App/HID 的重枚举。

---

## 编译

```bash
# MSYS 环境必须先切 MINGW64，否则 qmk/gcc 不在 PATH
export MSYSTEM=MINGW64
bash ./bootloaders/sk32duino/build_bl.sh
```

- `build_bl.sh` 会用 `sed` 强制把 `REGISTRY/WAITEXIT/SEMAPHORES/CONDVARS/EVENTS/DYNAMIC/MAILBOXES/MEMCORE/MEMPOOLS/OBJ_CACHES/JOBS` 置为 FALSE（最小化 RT），再 `make`。
- 产物：`build/sk32duino.bin`（<16 KB）。当前实测 BL ≈ text 83xx + data 8 ≈ 8250 B。
- Makefile 用 `-Os`，`USE_LTO = no`（Windows 下 -flto 会因工具链路径含空格导致链接失败）。

> 注意：若直接对 `cfg/chconf.h` 做修改，这些文件可能被外部回退，因此统一从 `build_bl.sh` 进入编译。

---

## 烧录 / 上传

```bash
# 下载应用固件（App 介质 = alt 2）
dfu-util -a 2 -D sk32_kb17_default.bin

# 从 0x08004000 读回验证 flash 内容（无需 ST-Link）
dfu-util -a 2 -U dump.bin -s 0x08000000:114688
# 说明：BL 支持 DFU_UPLOAD，可从 0x08004000 读回整个 Flash 应用区（大小 = F_SIZE − 16 KB）。
```

**不要用 ST-Link debug 模式在线刷写**：SK32 在 debug 模式下会启用 flash 写保护（userguide 4.3），下载会报 `Error: failed to download Segment[0]`。在线重刷请走 DFU（dfu-util）。

---

## 内容不变量

- `SK32_DFU_MAGIC = 0x4B32D7A1` 及其地址 `0x200001F0` 必须与 `platforms/chibios/bootloaders/sk32duino.c`（应用侧 QK_BOOT 实现）完全一致。
- 应用与 bootloader 的链接脚本必须交接一致：flash 分区、RAM 起点 `0x20000200` 与向量保留区。

## 参考

- 应用关键命令实现：`platforms/chibios/bootloaders/sk32duino.c`
- SK32 flash 几何：`source/dfu_target.h`
- board 配置：`lib/chibios-contrib/os/hal/boards/GENERIC_SK32_F077`
- SK32 端口：`lib/chibios-contrib/os/hal/ports/SK32/SK32F0xx`
