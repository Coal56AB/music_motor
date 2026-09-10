#ifndef MUSIC_CONFIG_H
#define MUSIC_CONFIG_H
#include <stdint.h>

/* Logical motor mask bits; independent of HAL GPIO pins and valid in #if. */
#define MOTOR_1 (1u << 0)
#define MOTOR_2 (1u << 1)
#define MOTOR_3 (1u << 2)
#define MOTOR_4 (1u << 3)
#define MOTOR_5 (1u << 4)
#define MOTOR_6 (1u << 5)


/* Основные настройки прошивки. Этот файл не перезаписывается CubeMX.
 * GPIO меняются в MusicMotor.ioc; метки из generated main.h используются ниже.
 * Тактирование/USART согласуйте с CubeMX: несовпадение проверяется при старте.
 */
#define FIRMWARE_VERSION        "1.0"
#define MOTOR_COUNT             6u       /* Фиксировано форматом UART STATUS. */
#define DEFAULT_INSTALLED_MASK  (/*MOTOR_1|*/MOTOR_2|MOTOR_3|MOTOR_4|MOTOR_5/*|MOTOR_6*/)


/* Настройки таймера и ноты*/
#define TIMER_HZ                1000000 /* Частота свободных счётчиков TIM2/3/4. */
#define MIN_FREQ_MHZ            20000u   /* Миллигерцы: 20 Гц. */
#define MAX_FREQ_MHZ            4000000u /* Миллигерцы: 4000 Гц. */
#define DEFAULT_NOTE            69u     /* A4, частота STEP = 440 Гц. */



/* Настройки связи */
#define NOTES_BUFFER_DEPTH          256    /* При изменении согласовать с desktop. */
#define UART_BAUD               115200u /* 8N1, USART1 PA9/PA10, также в CubeMX. */
#define UART_RING_SIZE          512u    /* Степень двойки; отдельные RX и TX. */
#define LINK_TIMEOUT_MS         2000u
#define FRAME_TIMEOUT_MS        100u
#define DUPLICATE_WINDOW_MS     1000u
#define STARTUP_DELAY_MS        2u      /* DIR setup + выход из SLEEP/RESET. */
#define RESET_PULSE_MS          2u
#define STREAM_START_DELAY_MS   100u    /* Согласовать с desktop playback. */
#define STEP_STOP_GUARD_US      3u      /* Минимальная выдержка перед forced low. */
#define OC_MIN_AHEAD_TICKS      8u      /* Запас до следующего CCR. */

/* Электрические активные уровни. 0 = активный низкий.
 * При изменении также изменить Output Level в CubeMX и внешние подтяжки.
 */
#define ENABLE_ACTIVE_LEVEL     0u
#define SLEEP_ACTIVE_LEVEL      0u
#define RESET_ACTIVE_LEVEL      0u

/* Компенсация обратного направления из-за подключения обмоток.
 * 0 = обычный DIR, 1 = инвертировать физический DIR только этого двигателя.
 * Например: M3_DIR_INVERT 1 для M3. Логический DIR в UART/GUI не меняется.
 * STEP и ENABLE не инвертируются; менять частоту/число импульсов не нужно.
 * HR4998 сам коммутирует фазы: отдельные обмотки A/B через STEP/DIR
 * программно инвертировать нельзя. Перепутанные пары обмоток чинятся проводами.
 */
#ifndef M1_DIR_INVERT
#define M1_DIR_INVERT 1u
#endif
#ifndef M2_DIR_INVERT
#define M2_DIR_INVERT 0u
#endif
#ifndef M3_DIR_INVERT
#define M3_DIR_INVERT 0u
#endif
#ifndef M4_DIR_INVERT
#define M4_DIR_INVERT 0u
#endif
#ifndef M5_DIR_INVERT
#define M5_DIR_INVERT 1u
#endif
#ifndef M6_DIR_INVERT
#define M6_DIR_INVERT 0u
#endif
#define MOTOR_DIR_INVERT_MASK ((M1_DIR_INVERT << 0) | (M2_DIR_INVERT << 1) | \
                               (M3_DIR_INVERT << 2) | (M4_DIR_INVERT << 3) | \
                               (M5_DIR_INVERT << 4) | (M6_DIR_INVERT << 5))
static inline uint8_t config_dir_level(uint8_t motor, uint8_t logical_dir) {
    return (uint8_t)((logical_dir != 0u) ^ ((MOTOR_DIR_INVERT_MASK >> motor) & 1u));
}

/* Итоговая разводка пользователя. Позиции сверху вниз, USB снизу.
 * M  ряд                  позиции   DIR  STEP  ENABLE  таймер/канал
 * 1  внешний левый        8,6,5     PA11 PA15  PB3     TIM2 CH1
 * 2  внешний левый        2,3,4     PB8  PB7   PB5     TIM4 CH2
 * 3  внутренний левый     2,4,5     PB4  PB6   PB9     TIM4 CH1
 * 4  внутренний правый    3,4,6     PA4  PA6   PB2     TIM3 CH1
 * 5  внешний правый       3,4,5     PB0  PA7   PA5     TIM3 CH2
 * 6  внешний правый       6,7,8     PB12 PB10  PB1     TIM2 CH3
 * Общие: PB15=MS1, PB13=MS2, PB11=MS3, PA8=SLEEP, PA12=RESET.
 * TIM2 full remap; TIM3/TIM4 no remap. JTAG off; SWD PA13/PA14 сохранён.
 * PB2 свободен от обвязки BOOT1 по подтверждению пользователя.
 * Каналы ниже нумеруются от НУЛЯ для CCR/CCMR/DIER.
 */
#define M1_TIMER TIM2
#define M1_CHANNEL 0u
#define M2_TIMER TIM4
#define M2_CHANNEL 1u
#define M3_TIMER TIM4
#define M3_CHANNEL 0u
#define M4_TIMER TIM3
#define M4_CHANNEL 0u
#define M5_TIMER TIM3
#define M5_CHANNEL 1u
#define M6_TIMER TIM2
#define M6_CHANNEL 2u
#define MOTOR_TIMERS {M1_TIMER, M2_TIMER, M3_TIMER, M4_TIMER, M5_TIMER, M6_TIMER}
#define MOTOR_CHANNELS {M1_CHANNEL, M2_CHANNEL, M3_CHANNEL, M4_CHANNEL, M5_CHANNEL, M6_CHANNEL}
#define MOTOR_PIN(n, signal) {M##n##_##signal##_GPIO_Port, M##n##_##signal##_Pin}
#define STEP_PINS {MOTOR_PIN(1, STEP), MOTOR_PIN(2, STEP), MOTOR_PIN(3, STEP), MOTOR_PIN(4, STEP), MOTOR_PIN(5, STEP), MOTOR_PIN(6, STEP)}
#define DIR_PINS {MOTOR_PIN(1, DIR), MOTOR_PIN(2, DIR), MOTOR_PIN(3, DIR), MOTOR_PIN(4, DIR), MOTOR_PIN(5, DIR), MOTOR_PIN(6, DIR)}
#define ENABLE_PINS {MOTOR_PIN(1, ENABLE), MOTOR_PIN(2, ENABLE), MOTOR_PIN(3, ENABLE), MOTOR_PIN(4, ENABLE), MOTOR_PIN(5, ENABLE), MOTOR_PIN(6, ENABLE)}
#define MS_PINS {{MS1_GPIO_Port, MS1_Pin}, {MS2_GPIO_Port, MS2_Pin}, {MS3_GPIO_Port, MS3_Pin}}
#define SLEEP_PIN {SLEEP_GPIO_Port, SLEEP_Pin}
#define RESET_PIN {RESET_GPIO_Port, RESET_Pin}



/* Всякое */
#define CONFIG_STRING_INNER(x) #x
#define CONFIG_STRING(x) CONFIG_STRING_INNER(x)

#if DEFAULT_NOTE > 127
#error Default note must be MIDI 0..127
#endif
#if MOTOR_COUNT != 6 || DEFAULT_INSTALLED_MASK > 0x3f
#error Invalid motor count or installed mask
#endif
#if (M1_DIR_INVERT > 1) || (M2_DIR_INVERT > 1) || (M3_DIR_INVERT > 1) || (M4_DIR_INVERT > 1) || (M5_DIR_INVERT > 1) || (M6_DIR_INVERT > 1)
#error DIR inversion flags must be 0 or 1
#endif
#if ENABLE_ACTIVE_LEVEL > 1 || SLEEP_ACTIVE_LEVEL > 1 || RESET_ACTIVE_LEVEL > 1
#error Active levels must be 0 or 1
#endif
#if UART_RING_SIZE < 256 || UART_RING_SIZE > 4096 || (UART_RING_SIZE & (UART_RING_SIZE - 1))
#error UART ring size must be a power of two from 256 to 4096
#endif
#if NOTES_BUFFER_DEPTH < 1 || NOTES_BUFFER_DEPTH > 512
#error Queue capacity must fit the STM32C8 RAM budget
#endif
#if TIMER_HZ < 1000000 || TIMER_HZ > 4000000 || (TIMER_HZ % 1000000)
#error Timer tick frequency must be an integer 1..4 MHz
#endif
#if MIN_FREQ_MHZ == 0 || MAX_FREQ_MHZ < MIN_FREQ_MHZ
#error Invalid STEP frequency range
#elif ((TIMER_HZ * 500u + MIN_FREQ_MHZ / 2u) / MIN_FREQ_MHZ) > 32767u
#error Minimum STEP frequency causes an ambiguous 16-bit compare deadline
#elif ((TIMER_HZ * 500u + MAX_FREQ_MHZ / 2u) / MAX_FREQ_MHZ) <= OC_MIN_AHEAD_TICKS
#error Maximum STEP frequency leaves no compare IRQ margin
#endif
#if STARTUP_DELAY_MS < 2 || RESET_PULSE_MS < 1 || STEP_STOP_GUARD_US < 3
#error Driver timing guards are too short
#endif
#endif
