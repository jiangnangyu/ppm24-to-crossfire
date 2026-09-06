/**
 * @file main.c
 * @brief PPM24 解码 + CRSF 协议封装 (STM32F103C8T6)
 *        纯寄存器操作，不依赖任何 HAL 库
 *
 * 硬件连接:
 *   - PPM 输入: PB6 (TIM4_CH1)
 *   - CRSF 输出: PA2 (USART2_TX)
 *   - 时钟: 外部晶振 8MHz → PLL → 72MHz
 *
 * MX-22 设置: DSC Output = PPM24
 */

#include <stdint.h>
#include <string.h>

/* 中断使能/禁止宏定义（替代 __disable_irq / __enable_irq） */
#define __disable_irq() __asm volatile ("cpsid i")
#define __enable_irq()  __asm volatile ("cpsie i")

/* ==================== 寄存器地址定义 ==================== */
#define RCC_BASE        0x40021000U
#define GPIOA_BASE      0x40010800U
#define GPIOB_BASE      0x40010C00U
#define USART2_BASE     0x40004400U
#define TIM4_BASE       0x40000800U
#define NVIC_BASE       0xE000E100U
#define SCB_BASE        0xE000ED00U

/* RCC 寄存器 */
#define RCC_CR          (*(volatile uint32_t*)(RCC_BASE + 0x00U))
#define RCC_CFGR        (*(volatile uint32_t*)(RCC_BASE + 0x04U))
#define RCC_APB1ENR     (*(volatile uint32_t*)(RCC_BASE + 0x1CU))
#define RCC_APB2ENR     (*(volatile uint32_t*)(RCC_BASE + 0x18U))

/* GPIOA 寄存器 */
#define GPIOA_CRL       (*(volatile uint32_t*)(GPIOA_BASE + 0x00U))
#define GPIOA_CRH       (*(volatile uint32_t*)(GPIOA_BASE + 0x04U))

/* GPIOB 寄存器 */
#define GPIOB_CRL       (*(volatile uint32_t*)(GPIOB_BASE + 0x00U))
#define GPIOB_CRH       (*(volatile uint32_t*)(GPIOB_BASE + 0x04U))

/* USART2 寄存器 */
#define USART2_SR       (*(volatile uint32_t*)(USART2_BASE + 0x00U))
#define USART2_DR       (*(volatile uint32_t*)(USART2_BASE + 0x04U))
#define USART2_BRR      (*(volatile uint32_t*)(USART2_BASE + 0x08U))
#define USART2_CR1      (*(volatile uint32_t*)(USART2_BASE + 0x0CU))
#define USART2_CR2      (*(volatile uint32_t*)(USART2_BASE + 0x10U))

/* TIM4 寄存器 */
#define TIM4_CR1        (*(volatile uint32_t*)(TIM4_BASE + 0x00U))
#define TIM4_SMCR       (*(volatile uint32_t*)(TIM4_BASE + 0x08U))
#define TIM4_DIER       (*(volatile uint32_t*)(TIM4_BASE + 0x0CU))
#define TIM4_SR         (*(volatile uint32_t*)(TIM4_BASE + 0x10U))
#define TIM4_CCMR1      (*(volatile uint32_t*)(TIM4_BASE + 0x18U))
#define TIM4_CCER       (*(volatile uint32_t*)(TIM4_BASE + 0x20U))
#define TIM4_PSC        (*(volatile uint32_t*)(TIM4_BASE + 0x28U))
#define TIM4_ARR        (*(volatile uint32_t*)(TIM4_BASE + 0x2CU))
#define TIM4_CCR1       (*(volatile uint32_t*)(TIM4_BASE + 0x34U))
#define TIM4_CCR2       (*(volatile uint32_t*)(TIM4_BASE + 0x38U))

/* NVIC 寄存器 */
#define NVIC_ISER0      (*(volatile uint32_t*)(NVIC_BASE + 0x000U))
#define NVIC_ICER0      (*(volatile uint32_t*)(NVIC_BASE + 0x080U))
#define NVIC_IPR7       (*(volatile uint32_t*)(NVIC_BASE + 0x31CU))

/* SCB 寄存器 */
#define SCB_VTOR        (*(volatile uint32_t*)(SCB_BASE + 0x08U))

/* ==================== 宏定义 ==================== */
#define MAX_PPM_CHANNELS       12
#define SYNC_THRESHOLD_US      4000
#define CRSF_BAUDRATE          420000
#define CRSF_SYNC_BYTE         0xC8U
#define CRSF_RC_FRAME_LENGTH   24U
#define CRSF_TYPE_RC           0x16U
#define CRSF_CHANNEL_COUNT     16

/* ==================== 全局变量 ==================== */
volatile uint16_t ppm_channel_time[MAX_PPM_CHANNELS];
volatile uint8_t  current_channel = 0;
volatile uint8_t  is_in_sync = 0;
volatile uint8_t  is_first_start = 1;
volatile uint8_t  edge_flag = 1;
volatile uint8_t  frame_ready = 0;

static uint16_t working_vals[MAX_PPM_CHANNELS];
static volatile uint32_t last_capture = 0;
uint16_t ppm_output[MAX_PPM_CHANNELS] = { 1500 };

/* ==================== 函数声明 ==================== */
void SystemClock_Config(void);
void GPIO_Init(void);
void USART2_Init(void);
void TIM4_InputCapture_Init(void);
void send_crsf_frame(uint16_t *channels, uint8_t count);
uint8_t crc8_dvb_s2(uint8_t *data, uint8_t len);
void delay_ms(uint32_t ms);
void nop(void);

/* ==================== 主函数 ==================== */
int main(void) {
    /* 设置中断向量表偏移（0x08000000 是 Flash 起始地址） */
    SCB_VTOR = 0x08000000U;

    SystemClock_Config();
    GPIO_Init();
    USART2_Init();
    TIM4_InputCapture_Init();

    /* 使能 TIM4 中断：更新中断、CC1 中断、CC2 中断 */
    TIM4_DIER |= (1U << 0U) | (1U << 1U) | (1U << 2U);

    /* 使能 TIM4 中断（IRQ 30） */
    NVIC_ISER0 = (1U << 30U);

    /* 使能 TIM4 计数器 */
    TIM4_CR1 |= 1U;

    while (1) {
        if (frame_ready) {
            frame_ready = 0;
            /* 临界区保护 */
            __disable_irq();
            for (int i = 0; i < MAX_PPM_CHANNELS; i++) {
                ppm_output[i] = working_vals[i];
            }
            __enable_irq();
            send_crsf_frame(ppm_output, MAX_PPM_CHANNELS);
        }
        delay_ms(1);
    }
}

/* ==================== 系统时钟配置 ==================== */
void SystemClock_Config(void) {
    /* 使能 HSE（外部 8MHz 晶振） */
    RCC_CR |= (1U << 16U);
    while (!(RCC_CR & (1U << 17U)));

    /* 配置 PLL: HSE 作为源，9 倍频 → 72MHz */
    RCC_CFGR |= (1U << 16U);           /* PLL 源 = HSE */
    RCC_CFGR |= (7U << 18U);           /* PLL 倍频 9 (7 对应 9) */
    RCC_CFGR |= (1U << 17U);           /* PLL 预分频 = 1 */

    /* 使能 PLL */
    RCC_CR |= (1U << 24U);
    while (!(RCC_CR & (1U << 25U)));

    /* 配置总线分频 */
    RCC_CFGR &= ~(0xF << 4U);          /* AHB = SYSCLK / 1 */
    RCC_CFGR |= (4U << 8U);            /* APB1 = HCLK / 2 (36MHz) */
    RCC_CFGR &= ~(0x7 << 11U);         /* APB2 = HCLK / 1 (72MHz) */

    /* 切换到 PLL 作为系统时钟 */
    RCC_CFGR |= (2U << 0U);
    while ((RCC_CFGR & (3U << 2U)) != (2U << 2U));
}

/* ==================== GPIO 初始化 ==================== */
void GPIO_Init(void) {
    /* 使能 GPIOA, GPIOB 时钟 (APB2) */
    RCC_APB2ENR |= (1U << 2U) | (1U << 3U);

    /* PA2: USART2_TX - 复用推挽输出, 50MHz */
    GPIOA_CRL &= ~(0xF << (2U * 4U));
    GPIOA_CRL |= (0xB << (2U * 4U));

    /* PB6: TIM4_CH1 - 输入捕获, 浮空输入 */
    GPIOB_CRL &= ~(0xF << (6U * 4U));
    GPIOB_CRL |= (0x4 << (6U * 4U));
}

/* ==================== USART2 初始化 ==================== */
void USART2_Init(void) {
    /* 使能 USART2 时钟 (APB1) */
    RCC_APB1ENR |= (1U << 17U);

    /* 波特率: 72000000 / (16 * 420000) ≈ 10.714 → 11 */
    USART2_BRR = 11;

    /* 使能发送 (TE=bit3), 使能接收 (RE=bit2) */
    USART2_CR1 |= (1U << 3U) | (1U << 2U);
    /* 使能 USART2 (UE=bit13) */
    USART2_CR1 |= (1U << 13U);
}

/* ==================== TIM4 输入捕获初始化 ==================== */
void TIM4_InputCapture_Init(void) {
    /* 使能 TIM4 时钟 (APB1) */
    RCC_APB1ENR |= (1U << 2U);

    /* 预分频器: 72MHz / 72 = 1MHz (每微秒加 1) */
    TIM4_PSC = 71U;
    /* 自动重装载值: 最大 */
    TIM4_ARR = 0xFFFFU;

    /* 通道1: 输入捕获, 直接 TI1 */
    TIM4_CCMR1 |= (1U << 0U) | (1U << 1U);   /* CC1S = 01 */
    TIM4_CCMR1 &= ~(1U << 2U);               /* IC1PSC = 00 */
    TIM4_CCMR1 &= ~(0xF << 4U);              /* IC1F = 0 */

    /* 通道2: 输入捕获, 间接 TI1FP2 (用于测暂停) */
    TIM4_CCMR1 |= (2U << 8U);                /* CC2S = 10 */
    TIM4_CCMR1 &= ~(1U << 10U);              /* IC2PSC = 00 */
    TIM4_CCMR1 &= ~(0xF << 12U);             /* IC2F = 0 */

    /* 使能捕获通道1和通道2 */
    TIM4_CCER |= (1U << 0U) | (1U << 4U);    /* CC1E=1, CC2E=1 */
    TIM4_CCER &= ~((1U << 1U) | (1U << 5U)); /* CC1P=0, CC2P=0 (上升沿) */

    /* 从模式: 复位模式, 触发源 TI1FP1 */
    TIM4_SMCR |= (4U << 0U);                 /* SMS = 100 (复位模式) */
    TIM4_SMCR |= (5U << 4U);                 /* TS = 101 (TI1FP1) */
}

/* ==================== TIM4 中断服务程序 ==================== */
void TIM4_IRQHandler(void) {
    uint32_t status = TIM4_SR;

    /* CC1 捕获中断 */
    if (status & (1U << 1U)) {
        TIM4_SR &= ~(1U << 1U);

        uint32_t now = TIM4_CCR1;
        uint32_t width;
        if (now >= last_capture) {
            width = now - last_capture;
        } else {
            width = 0xFFFFU - last_capture + now;
        }
        last_capture = now;

        uint32_t pause = TIM4_CCR2;

        if (is_first_start) {
            is_first_start = 0;
            return;
        }

        if (is_in_sync && edge_flag) {
            /* 检测同步脉冲: 信号宽度 > 4000us 且 暂停宽度约 300us */
            if (width >= SYNC_THRESHOLD_US && (pause > 250U && pause < 350U)) {
                current_channel = 0;
                is_in_sync = 0;
            } else if (width > pause && width < 2500U) {
                if (current_channel < MAX_PPM_CHANNELS) {
                    working_vals[current_channel] = (uint16_t)width;
                    current_channel++;
                }
            }
        }

        if (current_channel >= MAX_PPM_CHANNELS) {
            is_in_sync = 1;
            frame_ready = 1;
        }

        edge_flag = !edge_flag;
    }

    /* 更新中断（溢出） */
    if (status & (1U << 0U)) {
        TIM4_SR &= ~(1U << 0U);
    }
}

/* ==================== CRSF 协议封装 ==================== */
uint8_t crc8_dvb_s2(uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80U) {
                crc = (uint8_t)((crc << 1U) ^ 0xD5U);
            } else {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }
    return crc;
}

void send_crsf_frame(uint16_t *channels, uint8_t count) {
    uint8_t frame[26];
    uint8_t payload[22] = {0};
    uint16_t crsf_values[CRSF_CHANNEL_COUNT];
    uint8_t idx = 0, bit_pos = 0;

    /* 帧头 */
    frame[idx++] = CRSF_SYNC_BYTE;
    frame[idx++] = CRSF_RC_FRAME_LENGTH;
    frame[idx++] = CRSF_TYPE_RC;

    /* 映射 PPM 脉宽到 CRSF 值 (172~1811) */
    for (int i = 0; i < CRSF_CHANNEL_COUNT; i++) {
        if (i < count) {
            uint16_t val = channels[i];
            if (val < 800U) val = 800U;
            if (val > 2200U) val = 2200U;
            crsf_values[i] = (uint16_t)(172U + (uint32_t)(val - 800U) * (1811U - 172U) / (2200U - 800U));
        } else {
            crsf_values[i] = 992U;
        }
    }

    /* 打包 16 个 11 位数据到 22 字节 */
    for (int i = 0; i < CRSF_CHANNEL_COUNT; i++) {
        uint16_t val = crsf_values[i] & 0x07FFU;
        for (int b = 0; b < 11; b++) {
            if (val & (1U << b)) {
                payload[bit_pos / 8] |= (uint8_t)(1U << (bit_pos % 8));
            }
            bit_pos++;
        }
    }

    /* 复制 payload 到帧 */
    for (int i = 0; i < 22; i++) {
        frame[idx++] = payload[i];
    }

    /* CRC: 从类型字节到 payload 结束 (共 23 字节) */
    frame[idx] = crc8_dvb_s2(&frame[2], 23);
    idx++;

    /* 串口发送 */
    for (int i = 0; i < idx; i++) {
        while (!(USART2_SR & (1U << 7U)));
        USART2_DR = frame[i];
    }
}

/* ==================== 延时函数 ==================== */
void delay_ms(uint32_t ms) {
    volatile uint32_t count = ms * 72000U;
    while (count--) {
        __asm volatile ("nop");
    }
}

/* ==================== 空实现（防止链接错误） ==================== */
void _Error_Handler(char *file, int line) {
    while (1) {}
}

void __aeabi_unwind_cpp_pr0(void) {}
void __aeabi_unwind_cpp_pr1(void) {}
