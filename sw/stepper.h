/**************************************************************************//**
 * @file     stepper.h
 * @version  V1.0
 * @brief    两相四线步进电机驱动模块（AS4950 全桥 x2）——接口与配置
 *
 * @note     硬件拓扑（依据 motor_driver.pdf 原理图）：
 *             - 驱动芯片：2 片 AS4950（单全桥，A4950/DRV8870 兼容），
 *               U7 驱动 A 相绕组，U6 驱动 B 相绕组。
 *             - 每个全桥由 IN1/IN2 两个逻辑输入控制方向/刹车/滑行：
 *                 IN1=1,IN2=0 -> OUT1 高、OUT2 低（电流 OUT1 -> OUT2）
 *                 IN1=0,IN2=1 -> OUT1 低、OUT2 高（电流 OUT2 -> OUT1）
 *                 IN1=0,IN2=0 -> 滑行（Coast，桥臂全关，高阻）
 *                 IN1=1,IN2=1 -> 刹车（Brake，两下桥导通）
 *             - VREF 引脚由 MCU 的 PWM 经 RC 低通（1K + 0.1uF）提供，
 *               占空比决定斩波电流门限：I_trip ≈ VREF / (Av * Rsense)。
 *             - MCU 引脚映射：
 *                 PB12 = IN_AM   PB13 = IN_AP   PB14 = IN_BM   PB15 = IN_BP
 *                 PA14 = PWM1_CH4 = VREF_B      PA15 = PWM1_CH5 = VREF_A
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __STEPPER_H__
#define __STEPPER_H__

#include <stdint.h>
#include "NuMicro.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*/
/*  用户配置区（按实际硬件修改）                                              */
/*============================================================================*/

/* ---- 方向控制引脚（AS4950 的 IN1/IN2，均为高电平有效） ---- */
#define STEPPER_PORT        PB          /* 4 个 IN 脚都在 PB 口 */
#define STEPPER_PIN_AP      BIT13       /* PB13 = IN_AP（A 相 IN1） */
#define STEPPER_PIN_AM      BIT12       /* PB12 = IN_AM（A 相 IN2） */
#define STEPPER_PIN_BP      BIT15       /* PB15 = IN_BP（B 相 IN1） */
#define STEPPER_PIN_BM      BIT14       /* PB14 = IN_BM（B 相 IN2） */
#define STEPPER_PIN_ALL     (STEPPER_PIN_AP | STEPPER_PIN_AM | \
                             STEPPER_PIN_BP | STEPPER_PIN_BM)

/* ---- 电流参考 VREF（PWM 输出，经 RC 滤波后进入 AS4950 VREF 脚） ---- */
#define STEPPER_PWM         PWM1
#define STEPPER_VREF_A_CH   5           /* PA15 = PWM1_CH5 = VREF_A */
#define STEPPER_VREF_B_CH   4           /* PA14 = PWM1_CH4 = VREF_B */
#define STEPPER_VREF_CH_MASK  ( (1UL << STEPPER_VREF_A_CH) | (1UL << STEPPER_VREF_B_CH) )

/* ---- 默认参数 ---- */
#define STEPPER_PWM_FREQ            240000UL   /* VREF PWM 频率(Hz)，应 >> RC 截止频率(~1.6kHz) */
#define STEPPER_DEFAULT_CURRENT     50         /* 默认 VREF 占空比(%)，控制相电流大小 */
#define STEPPER_DEFAULT_SPEED_US    1000UL     /* 默认每步周期(us)，1000us = 1ms/步 */

/* ---- 电机参数（1.8° 两相步进电机） ---- */
#define STEPPER_FULL_STEPS_PER_REV  200        /* 整步每转 200 步 */
#define STEPPER_HALF_STEPS_PER_REV  400        /* 半步每转 400 步 */

/*============================================================================*/
/*  类型定义                                                                  */
/*============================================================================*/

/* 步进模式 */
typedef enum {
    STEPPER_MODE_FULL_STEP = 0,     /* 整步：4 拍（2-2 相励磁） */
    STEPPER_MODE_HALF_STEP = 1,     /* 半步：8 拍 */
} stepper_mode_t;

/* 旋转方向 */
typedef enum {
    STEPPER_DIR_CW  = 0,            /* 正向 */
    STEPPER_DIR_CCW = 1,            /* 反向 */
} stepper_dir_t;

/*============================================================================*/
/*  公共接口                                                                  */
/*============================================================================*/

/**
 * @brief  初始化步进电机硬件（GPIO + VREF PWM），并使电机进入断电滑行状态。
 * @note   调用前需已配置系统时钟并调用过 SystemCoreClockUpdate()，
 *         以便内部延时（CLK_SysTickDelay）与 PWM 频率计算正确。
 */
void Stepper_Init(void);

/**
 * @brief  设置步进模式（整步/半步）。
 * @param  mode  见 stepper_mode_t。
 */
void Stepper_SetMode(stepper_mode_t mode);

/**
 * @brief  设置旋转方向。
 * @param  dir  见 stepper_dir_t。
 * @note   若实际转向与预期相反，调用本函数切换方向即可，无需改接线。
 */
void Stepper_SetDirection(stepper_dir_t dir);

/**
 * @brief  设置转速（每步周期）。
 * @param  step_period_us  每步间隔时间（微秒）。数值越小转速越高。
 */
void Stepper_SetSpeed(uint32_t step_period_us);

/**
 * @brief  设置相电流（VREF PWM 占空比）。
 * @param  duty_percent  0~100，对应 VREF 电压 = duty/100 * VDD。
 * @note   占空比越高电流越大。I_trip ≈ (duty/100 * 3.3V) / (Av * Rsense)，
 *         本板 Rsense = 0.1Ω，Av 通常约 10，请按实际数据手册核算。
 */
void Stepper_SetCurrent(uint32_t duty_percent);

/**
 * @brief  按当前方向走一步（非阻塞，调用后立即返回）。
 * @note   步间延时应由调用方控制（配合 Stepper_SetSpeed 的周期自行延时）。
 */
void Stepper_Step(void);

/**
 * @brief  按当前方向连续走 N 步（阻塞）。
 * @param  steps  步数；正数按当前方向走，负数自动反向走 |steps| 步。
 * @note   步间延时使用 Stepper_SetSpeed 设定的周期。
 */
void Stepper_Move(int32_t steps);

/**
 * @brief  停止并断电（滑行 Coast）：四个 IN 全部输出低，桥臂全关。
 */
void Stepper_Stop(void);

/**
 * @brief  刹车（Brake）：四个 IN 全部输出高，两相下桥导通锁轴。
 */
void Stepper_Brake(void);

/**
 * @brief  使能 / 去使能。
 * @param  enable  1 = 按当前相序重新励磁；0 = 断电滑行（同 Stepper_Stop）。
 */
void Stepper_Enable(uint8_t enable);

/**
 * @brief  获取当前位置（相对上电点的累计步数，带符号）。
 * @return 累计步数（整步/半步都按“步”计）。
 */
int32_t Stepper_GetPosition(void);

#ifdef __cplusplus
}
#endif

#endif /* __STEPPER_H__ */
