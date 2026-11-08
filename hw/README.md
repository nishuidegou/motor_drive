# 电机驱动板硬件 (KiCad)

机械臂电机驱动板。KiCad 9.0 工程。

## 文件
| 文件 | 说明 |
|---|---|
| `motor_driver.kicad_pro` | KiCad 工程 |
| `motor_driver.kicad_sch` | 原理图 |
| `motor_driver.kicad_pcb` | PCB（4 层板） |
| `motor_driver-altium-import.kicad_sym` | 自定义符号库 |
| `motor_driver.pdf` | 原理图 PDF（便于快速查看） |
| `motor_driver.png` | 板卡预览图 |
| `bom/ibom.html` | 交互式 BOM |

## 关键器件
- MCU：Nuvoton **M2L31LGDAE**（Cortex-M23）
- 电机驱动：**2×AS4950** 全桥（A4950/DRV8870 兼容），A/B 两相
- 编码器：**MT6816** 磁编码器（SPI）
- 电源：**ME3116** 降压（VCC_5V）+ LP2992 LDO（VCC_3V3）
- 通讯：CAN（SN65HVD232）+ UART + I2C + SPI

## 备注
- Gerber 生产文件、KiCad 自动备份（motor_driver-backups/）未纳入仓库。
