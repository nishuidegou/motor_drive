# 步进电机驱动软件 (M2L31 + 2×AS4950)

两相四线步进电机驱动，MCU 为 Nuvoton **M2L31LGDAE**（Cortex-M23），驱动芯片为 **2×AS4950 全桥**（A4950/DRV8870 兼容）。

## 文件说明
| 文件 | 说明 |
|---|---|
| `main.c` | 测试程序：整步/半步正反转、变速、电流调节 |
| `stepper.c` | 步进电机驱动模块实现（相序表、方向/速度/电流控制） |
| `stepper.h` | 驱动模块接口与引脚/参数配置 |
| `Keil/` | Keil MDK (uVision) 工程 |
| `IAR/` | IAR EWARM 工程 |
| `GCC/` | GCC 工程 |
| `VSCode/` | VSCode / csolution 工程 |

## 引脚映射
- 方向：`PB12=IN_AM` `PB13=IN_AP` `PB14=IN_BM` `PB15=IN_BP`
- 电流参考：`PA14=PWM1_CH4=VREF_B` `PA15=PWM1_CH5=VREF_A`

## 编译
需要 Nuvoton **M2L31 BSP**（`Library/CMSIS`、`Library/StdDriver`、`Library/Device`），
本仓库未包含，请从 Nuvoton 官方获取后放入 Keil 工程的 include 路径
（工程默认引用 `..\..\..\Library\...`）。

1. 打开 `Keil/Template.uvprojx`
2. 配置 Library 路径
3. 编译并下载
