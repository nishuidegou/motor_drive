/**************************************************************************//**
 * @file     main.c
 * @version  V1.0
 * @brief    步进电机驱动测试程序（M2L31 + 2x AS4950）
 *
 * @note     本文件是 stepper 模块的测试代码。运行后会依次执行下列测试：
 *             TEST1 整步正/反转各一圈
 *             TEST2 半步正/反转各一圈
 *             TEST3 变速运行（加速-减速）
 *             TEST4 电流（扭矩）调节演示
 *           循环往复，每个测试段用 LED 闪烁标记。
 *
 *           若转向与预期相反，调用 Stepper_SetDirection() 切换即可，无需改线。
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <stdio.h>
#include "NuMicro.h"
#include "stepper.h"

/*============================================================================*/
/*  LED 指示（PC14，低电平点亮，按实际板卡调整）                              */
/*============================================================================*/
#define LED_PORT    PC
#define LED_PIN     BIT14

/*============================================================================*/
/*  延时封装（基于 SysTick，精确且不受编译优化影响）                         */
/*============================================================================*/
#define DELAY_US(us)    CLK_SysTickDelay((us))
#define DELAY_MS(ms)    CLK_SysTickLongDelay((ms) * 1000UL)

/*============================================================================*/
/*  测试开关：1 = 使能该测试项                                               */
/*============================================================================*/
#define TEST1_FULL_STEP_EN      1   /* 整步正反转 */
#define TEST2_HALF_STEP_EN      1   /* 半步正反转 */
#define TEST3_SPEED_SWEEP_EN    1   /* 变速运行 */
#define TEST4_CURRENT_SWEEP_EN  1   /* 电流调节演示 */

/*============================================================================*/
/*  系统初始化（仅时钟 + GPIO 时钟）                                          */
/*============================================================================*/
void SYS_Init(void)
{
    SYS_UnlockReg();

    /* 关闭外部晶振，仅使用内部 HIRC（12MHz） */
    CLK_DisableXtalRC(CLK_PWRCTL_HXTEN_Msk | CLK_PWRCTL_LXTEN_Msk);
    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk);
    while ((CLK->STATUS & CLK_STATUS_HIRCSTB_Msk) == 0UL);

    /* HCLK 切换到 HIRC，不分频 */
    CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC, CLK_CLKDIV0_HCLK(1));

    /* GPIO 端口时钟（LED=PC14，电机方向脚=PB，VREF=PA） */
    CLK_EnableModuleClock(GPA_MODULE);
    CLK_EnableModuleClock(GPB_MODULE);
    CLK_EnableModuleClock(GPC_MODULE);

    /* 更新系统时钟与 CyclesPerUs（延时 / PWM 依赖） */
    SystemCoreClockUpdate();

    SYS_LockReg();
}

/*============================================================================*/
/*  LED 驱动                                                                  */
/*============================================================================*/
void LED_Init(void)
{
    GPIO_SetMode(LED_PORT, LED_PIN, GPIO_MODE_OUTPUT);
    GPIO_SetPullCtl(LED_PORT, LED_PIN, GPIO_PUSEL_DISABLE);
    LED_PORT->DOUT |= LED_PIN;      /* 初始熄灭 */
}

static void LED_On(void)
{
    LED_PORT->DOUT &= ~LED_PIN;     /* 低电平点亮 */
}

static void LED_Off(void)
{
    LED_PORT->DOUT |= LED_PIN;
}

static void LED_Toggle(void)
{
    LED_PORT->DOUT ^= LED_PIN;
}

/* 闪烁 n 次作为测试段标记 */
static void LED_Blink(uint32_t times, uint32_t period_ms)
{
    for (uint32_t i = 0; i < times; i++) {
        LED_Toggle();
        DELAY_MS(period_ms);
        LED_Toggle();
        DELAY_MS(period_ms);
    }
}

/*============================================================================*/
/*  测试函数                                                                  */
/*============================================================================*/

/* TEST1：整步正/反转各一圈（1.8° 电机：200 步/圈） */
static void Test_FullStep(void)
{
    Stepper_SetMode(STEPPER_MODE_FULL_STEP);
    Stepper_SetSpeed(1000);         /* 1ms/步 */
    Stepper_SetCurrent(50);

    Stepper_SetDirection(STEPPER_DIR_CW);
    Stepper_Move(STEPPER_FULL_STEPS_PER_REV);   /* 正转一圈 */
    DELAY_MS(500);

    Stepper_SetDirection(STEPPER_DIR_CCW);
    Stepper_Move(STEPPER_FULL_STEPS_PER_REV);   /* 反转一圈 */
    DELAY_MS(500);
}

/* TEST2：半步正/反转各一圈（400 步/圈，更平滑） */
static void Test_HalfStep(void)
{
    Stepper_SetMode(STEPPER_MODE_HALF_STEP);
    Stepper_SetSpeed(700);          /* 700us/步 */

    Stepper_SetDirection(STEPPER_DIR_CW);
    Stepper_Move(STEPPER_HALF_STEPS_PER_REV);
    DELAY_MS(500);

    Stepper_SetDirection(STEPPER_DIR_CCW);
    Stepper_Move(STEPPER_HALF_STEPS_PER_REV);
    DELAY_MS(500);
}

/* TEST3：变速运行（加速 -> 匀速 -> 减速 -> 反向） */
static void Test_SpeedSweep(void)
{
    Stepper_SetMode(STEPPER_MODE_FULL_STEP);
    Stepper_SetDirection(STEPPER_DIR_CW);

    /* 加速：2ms -> 500us */
    for (uint32_t us = 2000; us >= 500; us -= 100) {
        Stepper_SetSpeed(us);
        Stepper_Move(40);
    }
    /* 匀速 */
    Stepper_SetSpeed(500);
    Stepper_Move(200);
    /* 减速：500us -> 2ms */
    for (uint32_t us = 500; us <= 2000; us += 100) {
        Stepper_SetSpeed(us);
        Stepper_Move(40);
    }

    DELAY_MS(500);
    Stepper_SetDirection(STEPPER_DIR_CCW);
    Stepper_SetSpeed(1000);
    Stepper_Move(400);              /* 反向回原点 */
    DELAY_MS(500);
}

/* TEST4：电流（扭矩）调节演示：占空比从低到高再回到默认 */
static void Test_CurrentSweep(void)
{
    Stepper_SetMode(STEPPER_MODE_FULL_STEP);
    Stepper_SetSpeed(800);
    Stepper_SetDirection(STEPPER_DIR_CW);

    for (uint32_t duty = 20; duty <= 90; duty += 10) {
        Stepper_SetCurrent(duty);
        Stepper_Move(50);
    }
    for (uint32_t duty = 90; duty >= 20; duty -= 10) {
        Stepper_SetCurrent(duty);
        Stepper_Move(50);
    }

    Stepper_SetCurrent(STEPPER_DEFAULT_CURRENT);    /* 恢复默认 */
    DELAY_MS(500);
}

/*============================================================================*/
/*  主函数                                                                    */
/*============================================================================*/
int main(void)
{
    SYS_Init();
    LED_Init();
    Stepper_Init();

    /* 上电心跳：闪 3 次表示就绪 */
    LED_Blink(3, 100);
    DELAY_MS(500);

    while (1) {
#if TEST1_FULL_STEP_EN
        LED_On();                   /* 段标记：长亮 -> 测试1 */
        Test_FullStep();
        LED_Off();
#endif

#if TEST2_HALF_STEP_EN
        LED_Blink(2, 100);          /* 段标记：闪 2 次 -> 测试2 */
        Test_HalfStep();
#endif

#if TEST3_SPEED_SWEEP_EN
        LED_Blink(3, 100);          /* 段标记：闪 3 次 -> 测试3 */
        Test_SpeedSweep();
#endif

#if TEST4_CURRENT_SWEEP_EN
        LED_Blink(4, 100);          /* 段标记：闪 4 次 -> 测试4 */
        Test_CurrentSweep();
#endif

        Stepper_Stop();             /* 一轮结束，断电滑行 */
        DELAY_MS(2000);
    }
}
