#include "periph_init.h"

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "board_resources.h"
#include "oled_probe.h"
#include "ws2812.h"

#define RGB_PWM_WRAP     255u    /* resolução de 8 bits por canal */
#define RGB_PWM_CLKDIV   16.0f   /* 125 MHz / 16 / 256 ≈ 30 kHz: sem cintilação visível */
#define BUZZER_DUTY_PCT  50u

void periph_init_pzem_uart(void)
{
    uart_init(BOARD_PZEM_UART, BOARD_PZEM_BAUD);
    uart_set_format(BOARD_PZEM_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(BOARD_PZEM_UART, false, false);
    /* FIFO de 32 bytes comporta a resposta inteira (25 bytes): a tarefa do
     * PZEM pode dormir enquanto o quadro chega, sem IRQ nem espera ocupada. */
    uart_set_fifo_enabled(BOARD_PZEM_UART, true);
    gpio_set_function(BOARD_PIN_PZEM_TX, GPIO_FUNC_UART);
    gpio_set_function(BOARD_PIN_PZEM_RX, GPIO_FUNC_UART);
    /* Pull-up no RX: linha em repouso alta se o conversor estiver desligado. */
    gpio_pull_up(BOARD_PIN_PZEM_RX);
}

void periph_init_oled_i2c(void)
{
    i2c_init(BOARD_OLED_I2C, BOARD_OLED_I2C_HZ);
    gpio_set_function(BOARD_PIN_OLED_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BOARD_PIN_OLED_SCL, GPIO_FUNC_I2C);
    /* A placa já tem pull-ups externos; os internos (~50 kΩ) só reforçam. */
    gpio_pull_up(BOARD_PIN_OLED_SDA);
    gpio_pull_up(BOARD_PIN_OLED_SCL);
}

static void init_button(uint gpio)
{
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_IN);
    gpio_pull_up(gpio);   /* ativo em nível baixo */
}

void periph_init_inputs(void)
{
    init_button(BOARD_PIN_BTN_A);
    init_button(BOARD_PIN_BTN_B);
    init_button(BOARD_PIN_JOY_SW);
}

static void init_pwm_pin(uint gpio, float clkdiv, uint16_t wrap)
{
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(gpio);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, clkdiv);
    pwm_config_set_wrap(&cfg, wrap);
    /* GP12 e GP13 dividem o slice 6: reconfigurar com os MESMOS valores é inofensivo. */
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(gpio, 0);
}

void periph_init_rgb_pwm(void)
{
    init_pwm_pin(BOARD_PIN_RGB_R, RGB_PWM_CLKDIV, RGB_PWM_WRAP);
    init_pwm_pin(BOARD_PIN_RGB_G, RGB_PWM_CLKDIV, RGB_PWM_WRAP);
    init_pwm_pin(BOARD_PIN_RGB_B, RGB_PWM_CLKDIV, RGB_PWM_WRAP);
}

void periph_init_buzzer_pwm(void)
{
    /* Slice 2 é exclusivo do buzzer; a frequência é ajustada em buzzer_tone(). */
    init_pwm_pin(BOARD_PIN_BUZZER, 125.0f, 1000u);
}

void periph_init_adc(void)
{
    adc_init();
    /* adc_gpio_init desliga o buffer digital e os pulls: pino 100% analógico. */
    adc_gpio_init(BOARD_PIN_JOY_Y);
    adc_gpio_init(BOARD_PIN_JOY_X);
    adc_gpio_init(BOARD_PIN_MIC);
}

uint32_t periph_init_all(void)
{
    uint32_t errors = PERIPH_ERR_NONE;

    periph_init_pzem_uart();
    periph_init_inputs();
    periph_init_rgb_pwm();
    periph_init_buzzer_pwm();
    periph_init_adc();

    periph_init_oled_i2c();
    if (!oled_probe_init()) {
        errors |= PERIPH_ERR_OLED;
    }
    /* PIO por último: o driver do CYW43 já reservou o que precisa. */
    if (!ws2812_init(BOARD_PIN_LED_MATRIX)) {
        errors |= PERIPH_ERR_MATRIX;
    }
    return errors;
}

void rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    pwm_set_gpio_level(BOARD_PIN_RGB_R, r);
    pwm_set_gpio_level(BOARD_PIN_RGB_G, g);
    pwm_set_gpio_level(BOARD_PIN_RGB_B, b);
}

void buzzer_tone(uint32_t freq_hz)
{
    uint slice = pwm_gpio_to_slice_num(BOARD_PIN_BUZZER);
    if (freq_hz == 0u) {
        pwm_set_gpio_level(BOARD_PIN_BUZZER, 0);
        return;
    }
    /* Base de 1 MHz (clk_sys / div); wrap = 1 MHz / f - 1. Faixa útil: 16 Hz a 20 kHz. */
    const float div = (float)clock_get_hz(clk_sys) / 1000000.0f;
    uint32_t wrap = 1000000u / freq_hz;
    if (wrap > 65535u) wrap = 65535u;
    if (wrap < 2u) wrap = 2u;
    pwm_set_clkdiv(slice, div);
    pwm_set_wrap(slice, (uint16_t)(wrap - 1u));
    pwm_set_gpio_level(BOARD_PIN_BUZZER, (uint16_t)(wrap * BUZZER_DUTY_PCT / 100u));
}
