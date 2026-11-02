/**
 * @file selftest_main.c
 * @brief Self-test de periféricos simultâneos — critério de aceite do WPS 3.2.
 *
 * Todos os periféricos rodam AO MESMO TEMPO, cada um com uma única tarefa dona:
 *
 *   tarefa   prio  dona de                       período
 *   -------  ----  ----------------------------  -------
 *   pzem      3    uart0 (GP0/GP1)               1 s
 *   input     2    ADC via mutex, fila de botões 20 ms
 *   ui        1    i2c1 OLED, PIO matriz, PWM    50 ms
 *   report    1    stdio USB, LED da Pico W      2 s
 *
 * Saída: relatório pelo USB-CDC a cada 2 s e "SELFTEST PASS" quando todos os
 * critérios forem atingidos (inclui apertar A, B e o joystick ao menos 1 vez).
 */
#include <stdbool.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "adc_shared.h"
#include "board_resources.h"
#include "oled_probe.h"
#include "periph_init.h"
#include "pzem_link.h"
#include "ws2812.h"

#ifndef SELFTEST_USE_CYW43
#define SELFTEST_USE_CYW43 1
#endif
#if SELFTEST_USE_CYW43
#include "pico/cyw43_arch.h"
#endif

/* ---------------------------------------------------------------- config -- */
#define PRIO_INIT            (configMAX_PRIORITIES - 1)
#define PRIO_PZEM            3
#define PRIO_INPUT           2
#define PRIO_UI              1
#define PRIO_REPORT          1

#define STACK_SMALL          512u    /* palavras (2 KB) */
#define STACK_PRINTF         1024u   /* printf usa bastante pilha */

#define PERIOD_PZEM_MS       1000u
#define PERIOD_INPUT_MS      20u
#define PERIOD_UI_MS         50u
#define PERIOD_REPORT_MS     2000u
#define OLED_TOGGLE_MS       500u
#define BEEP_MS              80u
#define BEEP_HZ              2000u
#define BLUE_FLASH_MS        300u

#define DEBOUNCE_US          30000u
#define ADC_TIMEOUT_MS       5u
#define MIN_RUN_SECONDS      60u     /* janela mínima antes de declarar PASS */
#define PZEM_MAX_FAIL_PCT    5u

/* ----------------------------------------------------------------- tipos -- */
typedef struct {
    uint8_t gpio;
} button_event_t;

/*
 * Estatísticas com UM escritor por campo (a tarefa dona); a report só lê.
 * Leitura/escrita de 32 bits alinhada é atômica no Cortex-M0+: sem mutex.
 */
typedef struct {
    volatile uint32_t init_errors;
    volatile uint32_t pzem_ok, pzem_fail, pzem_last, pzem_voltage_dv;
    volatile uint32_t adc_ok, adc_fail, joy_x, joy_y, mic;
    volatile uint32_t btn_a, btn_b, btn_joy;
    volatile uint32_t oled_ok, oled_fail, ui_frames;
} selftest_stats_t;

static selftest_stats_t s_stats;
static QueueHandle_t s_button_isr_queue;   /* ISR   -> input */
static QueueHandle_t s_beep_queue;         /* input -> ui    */

/* --------------------------------------------------------------- botões -- */
/*
 * O pico-sdk permite UM callback de GPIO por núcleo. Este é o único; outros
 * módulos que precisarem de IRQ de GPIO devem usar gpio_add_raw_irq_handler().
 */
static void gpio_isr(uint gpio, uint32_t events)
{
    static uint32_t last_us[NUM_BANK0_GPIOS];
    (void)events;
    if (gpio >= NUM_BANK0_GPIOS) return;

    uint32_t now = time_us_32();
    if ((now - last_us[gpio]) < DEBOUNCE_US) return;   /* debounce por pino */
    last_us[gpio] = now;

    button_event_t ev = {.gpio = (uint8_t)gpio};
    BaseType_t woken = pdFALSE;
    (void)xQueueSendFromISR(s_button_isr_queue, &ev, &woken);  /* fila cheia: descarta */
    portYIELD_FROM_ISR(woken);
}

static void enable_button_irqs(void)
{
    gpio_set_irq_enabled_with_callback(BOARD_PIN_BTN_A, GPIO_IRQ_EDGE_FALL, true, &gpio_isr);
    gpio_set_irq_enabled(BOARD_PIN_BTN_B, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BOARD_PIN_JOY_SW, GPIO_IRQ_EDGE_FALL, true);
}

/* --------------------------------------------------------------- tarefas -- */
static void rtos_sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void pzem_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        pzem_measurement_t m;
        pzem_parse_result_t r = pzem_link_read(rtos_sleep_ms, &m);
        s_stats.pzem_last = (uint32_t)r;
        if (r == PZEM_PARSE_OK) {
            s_stats.pzem_voltage_dv = m.voltage_dv;
            s_stats.pzem_ok++;
        } else {
            s_stats.pzem_fail++;
        }
        xTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_PZEM_MS));
    }
}

static void read_adc_channel(uint8_t ch, volatile uint32_t *dest)
{
    uint16_t v;
    if (adc_shared_read(ch, ADC_TIMEOUT_MS, &v)) {
        *dest = v;
        s_stats.adc_ok++;
    } else {
        s_stats.adc_fail++;
    }
}

static void input_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        read_adc_channel(BOARD_ADC_CH_JOY_X, &s_stats.joy_x);
        read_adc_channel(BOARD_ADC_CH_JOY_Y, &s_stats.joy_y);
        read_adc_channel(BOARD_ADC_CH_MIC, &s_stats.mic);

        button_event_t ev;
        while (xQueueReceive(s_button_isr_queue, &ev, 0) == pdTRUE) {
            if (ev.gpio == BOARD_PIN_BTN_A)        s_stats.btn_a++;
            else if (ev.gpio == BOARD_PIN_BTN_B)   s_stats.btn_b++;
            else if (ev.gpio == BOARD_PIN_JOY_SW)  s_stats.btn_joy++;
            (void)xQueueSend(s_beep_queue, &ev, 0);
        }
        xTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_INPUT_MS));
    }
}

/* Converte leitura de 12 bits (0..4095) em coordenada 0..4 da matriz. */
static unsigned adc_to_cell(uint32_t raw)
{
    unsigned cell = (unsigned)((raw * 5u) / 4096u);
    return cell > 4u ? 4u : cell;
}

static void ui_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    TickType_t beep_until = 0, blue_until = 0, next_oled = 0;
    bool oled_all_on = false;

    for (;;) {
        TickType_t now = xTaskGetTickCount();

        button_event_t ev;
        if (xQueueReceive(s_beep_queue, &ev, 0) == pdTRUE) {
            buzzer_tone(BEEP_HZ);
            beep_until = now + pdMS_TO_TICKS(BEEP_MS);
            blue_until = now + pdMS_TO_TICKS(BLUE_FLASH_MS);
        }
        if (beep_until != 0 && (int32_t)(now - beep_until) >= 0) {
            buzzer_tone(0);
            beep_until = 0;
        }

        const bool pzem_ok = (s_stats.pzem_last == PZEM_PARSE_OK) && s_stats.pzem_ok > 0;

        /* LED RGB: azul = botão; verde = PZEM respondendo; vermelho = sem PZEM. */
        if ((int32_t)(blue_until - now) > 0)  rgb_set(0, 0, 64);
        else if (pzem_ok)                     rgb_set(0, 32, 0);
        else                                  rgb_set(32, 0, 0);

        /* Matriz: um pixel segue o joystick (ADC + PIO juntos). */
        ws2812_clear();
        ws2812_set_pixel(ws2812_xy_to_index(adc_to_cell(s_stats.joy_x), adc_to_cell(s_stats.joy_y)),
                         pzem_ok ? 0 : 255, pzem_ok ? 255 : 0, 64);
        ws2812_show();

        /* OLED: alterna "todos acesos" a cada 500 ms; cada ACK conta. */
        if ((int32_t)(now - next_oled) >= 0) {
            oled_all_on = !oled_all_on;
            if (oled_probe_set_all_on(oled_all_on)) s_stats.oled_ok++;
            else                                    s_stats.oled_fail++;
            next_oled = now + pdMS_TO_TICKS(OLED_TOGGLE_MS);
        }

        s_stats.ui_frames++;
        xTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_UI_MS));
    }
}

static bool pzem_within_tolerance(void)
{
    uint32_t total = s_stats.pzem_ok + s_stats.pzem_fail;
    return s_stats.pzem_ok > 0 && (s_stats.pzem_fail * 100u) <= (total * PZEM_MAX_FAIL_PCT);
}

static void report_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    bool led = false;
    bool announced = false;

    for (;;) {
        uint32_t uptime_s = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);

#if SELFTEST_USE_CYW43
        led = !led;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);   /* LED via CYW43 + PIO dele */
#else
        (void)led;
#endif
        printf("\n[%4lus] init_err=0x%02lx  heap_livre=%u B\n", (unsigned long)uptime_s,
               (unsigned long)s_stats.init_errors, (unsigned)xPortGetFreeHeapSize());
        printf("  PZEM  ok=%lu falha=%lu ultimo=%lu V=%lu.%lu\n",
               (unsigned long)s_stats.pzem_ok, (unsigned long)s_stats.pzem_fail,
               (unsigned long)s_stats.pzem_last, (unsigned long)(s_stats.pzem_voltage_dv / 10u),
               (unsigned long)(s_stats.pzem_voltage_dv % 10u));
        printf("  ADC   ok=%lu falha=%lu  joyX=%4lu joyY=%4lu mic=%4lu\n",
               (unsigned long)s_stats.adc_ok, (unsigned long)s_stats.adc_fail,
               (unsigned long)s_stats.joy_x, (unsigned long)s_stats.joy_y,
               (unsigned long)s_stats.mic);
        printf("  OLED  ok=%lu falha=%lu   UI frames=%lu\n", (unsigned long)s_stats.oled_ok,
               (unsigned long)s_stats.oled_fail, (unsigned long)s_stats.ui_frames);
        printf("  BTN   A=%lu B=%lu JOY=%lu\n", (unsigned long)s_stats.btn_a,
               (unsigned long)s_stats.btn_b, (unsigned long)s_stats.btn_joy);

        const bool ok_init    = s_stats.init_errors == PERIPH_ERR_NONE;
        const bool ok_pzem    = pzem_within_tolerance();
        const bool ok_adc     = s_stats.adc_ok > 0 && s_stats.adc_fail == 0;
        const bool ok_oled    = s_stats.oled_ok > 0 && s_stats.oled_fail == 0;
        const bool ok_buttons = s_stats.btn_a > 0 && s_stats.btn_b > 0 && s_stats.btn_joy > 0;
        const bool ok_time    = uptime_s >= MIN_RUN_SECONDS;

        if (ok_init && ok_pzem && ok_adc && ok_oled && ok_buttons && ok_time) {
            if (!announced) printf("  >>> SELFTEST PASS <<<\n");
            announced = true;
        } else {
            printf("  pendente:%s%s%s%s%s%s\n", ok_init ? "" : " init", ok_pzem ? "" : " pzem",
                   ok_adc ? "" : " adc", ok_oled ? "" : " oled",
                   ok_buttons ? "" : " botoes(A,B,JOY)", ok_time ? "" : " tempo");
        }
        xTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_REPORT_MS));
    }
}

/* ------------------------------------------------------------------ init -- */
static void create_task_or_panic(TaskFunction_t fn, const char *name, uint32_t stack,
                                 UBaseType_t prio)
{
    if (xTaskCreate(fn, name, stack, NULL, prio, NULL) != pdPASS) {
        panic("xTaskCreate(%s) falhou", name);
    }
}

/*
 * A inicialização roda numa tarefa (e não em main) porque o CYW43 com
 * FreeRTOS espera o escalonador ativo, e o PIO da matriz só deve ser
 * reservado depois que o driver do Wi-Fi pegou o dele.
 */
static void init_task(void *arg)
{
    (void)arg;
#if SELFTEST_USE_CYW43
    if (cyw43_arch_init() != 0) {
        printf("[init] cyw43_arch_init falhou\n");
    }
#endif
    s_stats.init_errors = periph_init_all();
    if (!adc_shared_init()) panic("mutex do ADC");

    /* Filas ANTES de habilitar a IRQ que escreve nelas. */
    s_button_isr_queue = xQueueCreate(8, sizeof(button_event_t));
    s_beep_queue = xQueueCreate(4, sizeof(button_event_t));
    if (s_button_isr_queue == NULL || s_beep_queue == NULL) panic("filas");
    enable_button_irqs();

    create_task_or_panic(pzem_task, "pzem", STACK_SMALL, PRIO_PZEM);
    create_task_or_panic(input_task, "input", STACK_SMALL, PRIO_INPUT);
    create_task_or_panic(ui_task, "ui", STACK_SMALL, PRIO_UI);
    create_task_or_panic(report_task, "report", STACK_PRINTF, PRIO_REPORT);

    printf("[init] perifericos prontos, erros=0x%02lx\n", (unsigned long)s_stats.init_errors);
    vTaskDelete(NULL);
}

int main(void)
{
    stdio_init_all();   /* USB-CDC; a uart0 é do PZEM (ver board_resources.h) */
    create_task_or_panic(init_task, "init", STACK_PRINTF, PRIO_INIT);
    vTaskStartScheduler();
    panic("escalonador retornou");
}

/* ----------------------------------------------------------------- hooks -- */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    panic("stack overflow: %s", name);
}

void vApplicationMallocFailedHook(void)
{
    panic("heap do FreeRTOS esgotado");
}
