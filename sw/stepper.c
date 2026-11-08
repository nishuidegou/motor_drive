/**************************************************************************//**
 * @file     stepper.c
 * @version  V1.0
 * @brief    两相四线步进电机驱动模块实现（AS4950 全桥 x2）
 *
 * @details  控制策略：
 *             - 相序表直接映射到 4 个 IN 引脚（IN_AP/IN_AM/IN_BP/IN_BM）的
 *               PB 位掩码，高电平 = 对应桥臂导通。
 *             - 整步采用 2-2 相励磁（4 拍），半步为 8 拍。
 *             - VREF_A/VREF_B 由 PWM1_CH5/CH4 输出，经 RC 低通后作为斩波
 *               电流门限；改变占空比即可调节相电流（也用于微步幅值）。
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include "stepper.h"

/*============================================================================*/
/*  相序表（写入 Flash，只读）                                                */
/*============================================================================*/

/* 整步：4 拍，2-2 相励磁（每拍两相同时通电，力矩大） */
static const uint32_t s_full_step_seq[4] = {
    STEPPER_PIN_AP | STEPPER_PIN_BP,   /* 0: A+ B+ */
    STEPPER_PIN_AM | STEPPER_PIN_BP,   /* 1: A- B+ */
    STEPPER_PIN_AM | STEPPER_PIN_BM,   /* 2: A- B- */
    STEPPER_PIN_AP | STEPPER_PIN_BM,   /* 3: A+ B- */
};

/* 半步：8 拍（分辨率翻倍，单/双相交替） */
static const uint32_t s_half_step_seq[8] = {
    STEPPER_PIN_AP,                    /* 0: A+      */
    STEPPER_PIN_AP | STEPPER_PIN_BP,   /* 1: A+ B+   */
    STEPPER_PIN_BP,                    /* 2: B+      */
    STEPPER_PIN_AM | STEPPER_PIN_BP,   /* 3: A- B+   */
    STEPPER_PIN_AM,                    /* 4: A-      */
    STEPPER_PIN_AM | STEPPER_PIN_BM,   /* 5: A- B-   */
    STEPPER_PIN_BM,                    /* 6: B-      */
    STEPPER_PIN_AP | STEPPER_PIN_BM,   /* 7: A+ B-   */
};

/*============================================================================*/
/*  模块状态                                                                  */
/*============================================================================*/

static stepper_mode_t s_mode      = STEPPER_MODE_FULL_STEP;
static stepper_dir_t  s_dir       = STEPPER_DIR_CW;
static uint32_t       s_speed_us  = STEPPER_DEFAULT_SPEED_US;
static uint32_t       s_step_idx  = 0;   /* 当前相序下标（0..n-1） */
static int32_t        s_position  = 0;   /* 累计步数（带符号） */
static uint8_t        s_enabled   = 0;   /* 是否励磁 */

/*============================================================================*/
/*  内部函数                                                                  */
/*============================================================================*/

/* 当前模式对应的相序表长度 */
static uint32_t _seq_len(void)
{
    return (s_mode == STEPPER_MODE_HALF_STEP) ? 8UL : 4UL;
}

/* 当前模式对应的相序表指针 */
static const uint32_t *_seq(void)
{
    return (s_mode == STEPPER_MODE_HALF_STEP) ? s_half_step_seq : s_full_step_seq;
}

/* 把当前相序写入引脚（只改 4 个 IN 脚，不影响 PB 其它脚） */
static void _apply_phase(uint32_t phase_mask)
{
    STEPPER_PORT->DOUT = (STEPPER_PORT->DOUT & ~STEPPER_PIN_ALL) | (phase_mask & STEPPER_PIN_ALL);
}

/*============================================================================*/
/*  公共接口                                                                  */
/*============================================================================*/

void Stepper_Init(void)
{
    /* --- 系统级：PWM1 时钟 / 复位 --- */
    SYS_UnlockReg();

    CLK_EnableModuleClock(PWM1_MODULE);
    CLK_SetModuleClock(PWM1_MODULE, CLK_CLKSEL3_PWM1SEL_PCLK1, 0);
    SYS_ResetModule(PWM1_RST);

    /* 复用功能：PA14 -> PWM1_CH4（VREF_B），PA15 -> PWM1_CH5（VREF_A） */
    SYS->GPA_MFP3 = (SYS->GPA_MFP3 & ~SYS_GPA_MFP3_PA14MFP_Msk) | SYS_GPA_MFP3_PA14MFP_PWM1_CH4;
    SYS->GPA_MFP3 = (SYS->GPA_MFP3 & ~SYS_GPA_MFP3_PA15MFP_Msk) | SYS_GPA_MFP3_PA15MFP_PWM1_CH5;

    /* 复用功能：PB12~PB15 -> GPIO（方向控制脚） */
    SYS->GPB_MFP3 = (SYS->GPB_MFP3 & ~SYS_GPB_MFP3_PB12MFP_Msk) | SYS_GPB_MFP3_PB12MFP_GPIO;
    SYS->GPB_MFP3 = (SYS->GPB_MFP3 & ~SYS_GPB_MFP3_PB13MFP_Msk) | SYS_GPB_MFP3_PB13MFP_GPIO;
    SYS->GPB_MFP3 = (SYS->GPB_MFP3 & ~SYS_GPB_MFP3_PB14MFP_Msk) | SYS_GPB_MFP3_PB14MFP_GPIO;
    SYS->GPB_MFP3 = (SYS->GPB_MFP3 & ~SYS_GPB_MFP3_PB15MFP_Msk) | SYS_GPB_MFP3_PB15MFP_GPIO;

    SYS_LockReg();

    /* --- GPIO：4 个方向脚推挽输出，禁上下拉 --- */
    GPIO_SetMode(STEPPER_PORT, STEPPER_PIN_ALL, GPIO_MODE_OUTPUT);
    GPIO_SetPullCtl(STEPPER_PORT, STEPPER_PIN_ALL, GPIO_PUSEL_DISABLE);

    /* --- VREF PWM：默认占空比，提供相电流门限 --- */
    Stepper_SetCurrent(STEPPER_DEFAULT_CURRENT);
    PWM_EnableOutput(STEPPER_PWM, STEPPER_VREF_CH_MASK);
    PWM_Start(STEPPER_PWM, STEPPER_VREF_CH_MASK);

    /* --- 初始状态：断电滑行 --- */
    s_mode     = STEPPER_MODE_FULL_STEP;
    s_dir      = STEPPER_DIR_CW;
    s_speed_us = STEPPER_DEFAULT_SPEED_US;
    s_step_idx = 0;
    s_position = 0;
    s_enabled  = 0;
    _apply_phase(0);
}

void Stepper_SetMode(stepper_mode_t mode)
{
    if (mode != STEPPER_MODE_FULL_STEP && mode != STEPPER_MODE_HALF_STEP) {
        return;
    }
    s_mode = mode;
    /* 保证下标在新相序表范围内 */
    if (s_step_idx >= _seq_len()) {
        s_step_idx = 0;
    }
}

void Stepper_SetDirection(stepper_dir_t dir)
{
    if (dir == STEPPER_DIR_CW || dir == STEPPER_DIR_CCW) {
        s_dir = dir;
    }
}

void Stepper_SetSpeed(uint32_t step_period_us)
{
    s_speed_us = step_period_us;
}

void Stepper_SetCurrent(uint32_t duty_percent)
{
    if (duty_percent > 100UL) {
        duty_percent = 100UL;
    }
    /* CH4/CH5 共享预分频与周期（同频），各自 CMR（占空比）独立 */
    PWM_ConfigOutputChannel(STEPPER_PWM, STEPPER_VREF_A_CH, STEPPER_PWM_FREQ, duty_percent);
    PWM_ConfigOutputChannel(STEPPER_PWM, STEPPER_VREF_B_CH, STEPPER_PWM_FREQ, duty_percent);
}

void Stepper_Step(void)
{
    uint32_t n = _seq_len();

    /* 励磁当前相 */
    _apply_phase(_seq()[s_step_idx]);
    s_enabled = 1;

    /* 更新下标：CW 递增，CCW 递减 */
    if (s_dir == STEPPER_DIR_CW) {
        s_step_idx = (s_step_idx + 1UL) % n;
        s_position++;
    } else {
        s_step_idx = (s_step_idx + n - 1UL) % n;
        s_position--;
    }
}

void Stepper_Move(int32_t steps)
{
    stepper_dir_t saved_dir = s_dir;

    /* 负步数自动反向 */
    if (steps < 0) {
        s_dir = (s_dir == STEPPER_DIR_CW) ? STEPPER_DIR_CCW : STEPPER_DIR_CW;
        steps = -steps;
    }

    while (steps-- > 0) {
        Stepper_Step();
        CLK_SysTickDelay(s_speed_us);
    }

    s_dir = saved_dir;   /* 恢复原方向，不改变调用方设定 */
}

void Stepper_Stop(void)
{
    _apply_phase(0);       /* 四个 IN 全低 -> Coast（滑行） */
    s_enabled = 0;
}

void Stepper_Brake(void)
{
    _apply_phase(STEPPER_PIN_ALL);  /* 四个 IN 全高 -> Brake（锁轴） */
    s_enabled = 0;
}

void Stepper_Enable(uint8_t enable)
{
    if (enable) {
        _apply_phase(_seq()[s_step_idx]);
        s_enabled = 1;
    } else {
        Stepper_Stop();
    }
}

int32_t Stepper_GetPosition(void)
{
    return s_position;
}
