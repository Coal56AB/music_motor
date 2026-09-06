#include "config.h"
#include "platform.h"
#include "main.h"
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} Pin;
static const Pin steps[MOTOR_COUNT] = STEP_PINS, dirs[MOTOR_COUNT] = DIR_PINS, enables[MOTOR_COUNT] = ENABLE_PINS;
static const Pin ms[3] = MS_PINS, sleep_pin = SLEEP_PIN, reset_pin = RESET_PIN;
static volatile uint16_t pending_half[MOTOR_COUNT], half[MOTOR_COUNT];
static volatile uint8_t active[MOTOR_COUNT], high[MOTOR_COUNT], oc_error;
static volatile uint8_t rx[UART_RING_SIZE], tx[UART_RING_SIZE], uart_error;
static volatile uint16_t rh, rt, th, tt;
uint32_t platform_lock(void) {
    uint32_t key;
    __asm volatile("mrs %0, primask\ncpsid i" : "=r"(key)::"memory");
    return key;
}
void platform_unlock(uint32_t key) {
    __asm volatile("msr primask, %0" ::"r"(key) : "memory");
}
static void write_pin(Pin p, uint8_t value) {
    p.port->BSRR = value ? p.pin : (uint32_t)p.pin << 16;
}
static void output_pin(Pin p) {
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = p.pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(p.port, &gpio);
}
static volatile uint32_t *ccr(TIM_TypeDef *t, uint8_t c) {
    return &t->CCR1 + c;
}
static TIM_TypeDef *timer(uint8_t m) {
    static TIM_TypeDef *const timers[MOTOR_COUNT] = MOTOR_TIMERS;
    return timers[m];
}
static uint8_t channel(uint8_t m) {
    static const uint8_t channels[MOTOR_COUNT] = MOTOR_CHANNELS;
    return channels[m];
}
static void oc_mode(TIM_TypeDef *t, uint8_t c, uint8_t value) {
    volatile uint32_t *r = c < 2 ? &t->CCMR1 : &t->CCMR2;
    uint8_t shift = (c % 2) * 8;
    *r = (*r & ~(255u << shift)) | ((uint32_t)value << (shift + 4));
}
void platform_enable(uint8_t m, uint8_t on) {
    write_pin(enables[m], on ? ENABLE_ACTIVE_LEVEL : !ENABLE_ACTIVE_LEVEL);
}
void platform_dir(uint8_t m, uint8_t dir) {
    write_pin(dirs[m], config_dir_level(m, dir));
}
void platform_common(uint8_t raw, uint8_t sleep, uint8_t reset) {
    for (uint8_t i = 0; i < 3; i++)
        write_pin(ms[i], (raw >> i) & 1);
    write_pin(sleep_pin, sleep ? SLEEP_ACTIVE_LEVEL : !SLEEP_ACTIVE_LEVEL);
    write_pin(reset_pin, reset ? RESET_ACTIVE_LEVEL : !RESET_ACTIVE_LEVEL);
}
void platform_stop(uint8_t m) {
    uint32_t key = platform_lock();
    TIM_TypeDef *t = timer(m);
    uint8_t c = channel(m);
    t->DIER &= ~(2u << c);
    oc_mode(t, c, 0); /* Freeze OCREF first: no new edge during the pulse-width guard. */
    /* Before forcing low, allow at least 3 us since a possible compare edge.
       No IRQ can create another schedule while this critical section is held. */
    uint16_t at = (uint16_t)t->CNT;
    while ((uint16_t)((uint16_t)t->CNT - at) < (STEP_STOP_GUARD_US * (TIMER_HZ / 1000000u))) {
    }
    oc_mode(t, c, 4);
    t->SR = ~(2u << c);
    active[m] = high[m] = 0;
    platform_unlock(key);
}
void platform_step(uint8_t m, uint16_t period) {
    uint32_t key = platform_lock();
    TIM_TypeDef *t = timer(m);
    uint8_t c = channel(m);
    pending_half[m] = period;
    if (!active[m]) {
        half[m] = period;
        high[m] = 0;
        oc_mode(t, c, 4);
        *ccr(t, c) = (uint16_t)(t->CNT + period);
        t->SR = ~(2u << c);
        active[m] = 1;
        oc_mode(t, c, 3);
        t->DIER |= 2u << c;
    }
    platform_unlock(key);
}
static inline void compare_irq(TIM_TypeDef *t, uint8_t c, uint8_t m) {
    uint32_t flag = 2u << c;
    if ((t->SR & t->DIER & flag) == 0)
        return;
    t->SR = ~flag;
    if (!active[m])
        return;
    high[m] ^= 1;
    /* Update on falling edge: complete the old high pulse first. */
    if (!high[m])
        half[m] = pending_half[m];
    uint16_t next = (uint16_t)(*ccr(t, c) + half[m]);
    uint16_t ahead = (uint16_t)(next - (uint16_t)t->CNT);
    if (ahead < OC_MIN_AHEAD_TICKS || ahead > 32767u) {
        oc_mode(t, c, 4);
        t->DIER &= ~flag;
        active[m] = 0;
        oc_error = 1;
        return;
    }
    *ccr(t, c) = next;
}
void platform_tim2_irq(void) {
    compare_irq(M1_TIMER, M1_CHANNEL, 0);
    compare_irq(M6_TIMER, M6_CHANNEL, 5);
}
void platform_tim3_irq(void) {
    compare_irq(M4_TIMER, M4_CHANNEL, 3);
    compare_irq(M5_TIMER, M5_CHANNEL, 4);
}
void platform_tim4_irq(void) {
    compare_irq(M3_TIMER, M3_CHANNEL, 2);
    compare_irq(M2_TIMER, M2_CHANNEL, 1);
}
uint32_t platform_ms(void) {
    return HAL_GetTick();
}
void platform_uart_irq(void) {
    uint32_t sr = USART1->SR;
    if (sr & 0x2fu) {
        uint8_t b = (uint8_t)USART1->DR;
        if (sr & 15u)
            uart_error = 1;
        else if (sr & (1u << 5)) {
            uint16_t next = (rh + 1) & (UART_RING_SIZE - 1u);
            if (next == rt)
                uart_error = 1;
            else {
                rx[rh] = b;
                rh = next;
            }
        }
    }
    if ((sr & (1u << 7)) && (USART1->CR1 & (1u << 7))) {
        if (tt != th) {
            USART1->DR = tx[tt];
            tt = (tt + 1) & (UART_RING_SIZE - 1u);
        } else
            USART1->CR1 &= ~(1u << 7);
    }
}
int platform_read(void) {
    if (rt == rh)
        return -1;
    int b = rx[rt];
    rt = (rt + 1) & (UART_RING_SIZE - 1u);
    return b;
}
void platform_write(const uint8_t *data, uint16_t n) {
    uint32_t key = platform_lock();
    if (n > ((tt - th - 1u) & (UART_RING_SIZE - 1u))) {
        uart_error = 1;
        platform_unlock(key);
        return;
    }
    for (uint16_t i = 0; i < n; i++) {
        tx[th] = data[i];
        th = (th + 1) & (UART_RING_SIZE - 1u);
    }
    USART1->CR1 |= 1u << 7;
    platform_unlock(key);
}
uint8_t platform_uart_error(void) {
    uint32_t key = platform_lock();
    uint8_t e = uart_error;
    uart_error = 0;
    if (e)
        rt = rh;
    platform_unlock(key);
    return e;
}
uint8_t platform_oc_error(void) {
    uint32_t key = platform_lock();
    uint8_t e = oc_error;
    oc_error = 0;
    platform_unlock(key);
    return e;
}
/* CubeMX owns clock, GPIO AF, timer prescalers and USART configuration.
   This layer owns only the running compare channels and UART ring buffers. */
void platform_safe_gpio(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        platform_enable(i, 0);
        output_pin(enables[i]);
        platform_dir(i, 0);
        output_pin(dirs[i]);
        write_pin(steps[i], 0);
        output_pin(steps[i]);
    }
    platform_common(0, 1, 1);
    for (uint8_t i = 0; i < 3; i++) output_pin(ms[i]);
    output_pin(sleep_pin);
    output_pin(reset_pin);
}
void platform_init(void) {
    uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0u) timer_clock *= 2u;
    if (timer_clock / (TIM2->PSC + 1u) != TIMER_HZ ||
        timer_clock / (TIM3->PSC + 1u) != TIMER_HZ ||
        timer_clock / (TIM4->PSC + 1u) != TIMER_HZ ||
        TIM2->ARR != 65535u || TIM3->ARR != 65535u || TIM4->ARR != 65535u) Error_Handler();
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        TIM_TypeDef *t = timer(i);
        uint8_t c = channel(i);
        platform_enable(i, 0);
        oc_mode(t, c, 4); /* Force STEP low before enabling its output. */
        t->CCER |= 1u << (c * 4);
        platform_dir(i, 0); /* Restore configured polarity after generated GPIO init. */
    }
    TIM2->DIER = TIM3->DIER = TIM4->DIER = 0;
    TIM2->SR = TIM3->SR = TIM4->SR = 0;
    TIM2->CR1 |= TIM_CR1_CEN;
    TIM3->CR1 |= TIM_CR1_CEN;
    TIM4->CR1 |= TIM_CR1_CEN;
    USART1->CR1 |= USART_CR1_RXNEIE;
    USART1->CR3 |= USART_CR3_EIE;
}
void platform_fault(void) {
    /* Also valid before timer initialization; do not wait on a stopped counter. */
    __disable_irq();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    TIM2->DIER = TIM3->DIER = TIM4->DIER = 0;
    for (uint8_t i = 0; i < MOTOR_COUNT; i++)
        oc_mode(timer(i), channel(i), 4);
    platform_safe_gpio();
}
