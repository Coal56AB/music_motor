#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>
uint32_t platform_ms(void);
void platform_init(void);
void platform_safe_gpio(void);
void platform_fault(void);
void platform_tim2_irq(void);
void platform_tim3_irq(void);
void platform_tim4_irq(void);
void platform_uart_irq(void);
void platform_enable(uint8_t motor, uint8_t enabled);
void platform_dir(uint8_t motor, uint8_t dir);
void platform_common(uint8_t raw, uint8_t sleep, uint8_t reset);
void platform_step(uint8_t motor, uint16_t half_period);
void platform_stop(uint8_t motor);
void platform_write(const uint8_t *data, uint16_t len);
int platform_read(void);
uint8_t platform_uart_error(void);
uint8_t platform_oc_error(void);
uint32_t platform_lock(void);
void platform_unlock(uint32_t key);
#endif
