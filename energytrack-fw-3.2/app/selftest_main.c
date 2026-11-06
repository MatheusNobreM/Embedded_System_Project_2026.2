/**
 * @file selftest_main.c
 * @brief Self-test do WPS 3.2 no ESP32 DevKit: PZEM na UART2, LED da placa e log serial.
 *
 * Tarefas presas no núcleo 1, cada recurso com uma única tarefa dona:
 *
 *   tarefa   prio  dona de                       período
 *   -------  ----  ----------------------------  -------
 *   pzem      3    UART2 (GPIO17/GPIO16)         1 s
 *   status    2    LED da placa (GPIO2)          100 ms
 *   report    1    console (UART0, USB-serial)   2 s
 *
 * Saída: relatório pelo console a cada 2 s e "SELFTEST PASS" depois de 60 s
 * com o PZEM respondendo e no máximo 5% de leituras com falha.
 * LED aceso fixo = PZEM respondendo; piscando = sem resposta válida.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_init.h"
#include "pzem_link.h"

/* ---------------------------------------------------------------- config -- */
#define APP_CORE             1       /* núcleo 1 (APP_CPU) */
#define PRIO_INIT            10
#define PRIO_PZEM            3
#define PRIO_STATUS          2
#define PRIO_REPORT          1

#define STACK_SMALL          3072u   /* bytes */
#define STACK_REPORT         6144u   /* printf usa bastante pilha */

#define PERIOD_PZEM_MS       1000u
#define PERIOD_STATUS_MS     100u
#define PERIOD_REPORT_MS     2000u
#define BLINK_HALF_PERIOD_MS 500u    /* sem PZEM: pisca a 1 Hz */

#define MIN_RUN_SECONDS      60u     /* janela mínima antes de declarar PASS */
#define PZEM_MAX_FAIL_PCT    5u

/* ----------------------------------------------------------------- tipos -- */
/*
 * Estatísticas com UM escritor por campo (a tarefa dona); a report só lê.
 * Leitura/escrita de 32 bits alinhada é atômica no Xtensa LX6: sem mutex.
 */
typedef struct {
    volatile uint32_t pzem_ok, pzem_fail, pzem_last;
    volatile uint32_t voltage_dv, current_ma, power_dw, energy_wh, freq_dhz, pf_centi;
} selftest_stats_t;

static selftest_stats_t s_stats;

static void fatal(const char *what)
{
    printf("FATAL: %s\n", what);
    fflush(stdout);
    abort();
}

/* --------------------------------------------------------------- tarefas -- */
static void pzem_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        pzem_measurement_t m;
        pzem_parse_result_t r = pzem_link_read(&m);
        s_stats.pzem_last = (uint32_t)r;
        if (r == PZEM_PARSE_OK) {
            s_stats.voltage_dv = m.voltage_dv;
            s_stats.current_ma = m.current_ma;
            s_stats.power_dw = m.power_dw;
            s_stats.energy_wh = m.energy_wh;
            s_stats.freq_dhz = m.freq_dhz;
            s_stats.pf_centi = m.pf_centi;
            s_stats.pzem_ok++;
        } else {
            s_stats.pzem_fail++;
        }
        xTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_PZEM_MS));
    }
}

static void status_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        const bool pzem_ok = (s_stats.pzem_last == PZEM_PARSE_OK) && s_stats.pzem_ok > 0;
        const bool blink_on = ((xTaskGetTickCount() / pdMS_TO_TICKS(BLINK_HALF_PERIOD_MS)) & 1u) == 0;
        board_led_set(pzem_ok ? true : blink_on);
        xTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_STATUS_MS));
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
    bool announced = false;

    for (;;) {
        uint32_t uptime_s = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);

        printf("\n[%4lus] heap_livre=%lu B\n", (unsigned long)uptime_s, (unsigned long)esp_get_free_heap_size());
        printf("  PZEM  ok=%lu falha=%lu ultimo=%lu\n", (unsigned long)s_stats.pzem_ok,
               (unsigned long)s_stats.pzem_fail, (unsigned long)s_stats.pzem_last);
        printf("  V=%lu.%lu V  I=%lu.%03lu A  P=%lu.%lu W  E=%lu Wh  f=%lu.%lu Hz  FP=%lu.%02lu\n",
               (unsigned long)(s_stats.voltage_dv / 10u), (unsigned long)(s_stats.voltage_dv % 10u),
               (unsigned long)(s_stats.current_ma / 1000u), (unsigned long)(s_stats.current_ma % 1000u),
               (unsigned long)(s_stats.power_dw / 10u), (unsigned long)(s_stats.power_dw % 10u),
               (unsigned long)s_stats.energy_wh,
               (unsigned long)(s_stats.freq_dhz / 10u), (unsigned long)(s_stats.freq_dhz % 10u),
               (unsigned long)(s_stats.pf_centi / 100u), (unsigned long)(s_stats.pf_centi % 100u));

        const bool ok_pzem = pzem_within_tolerance();
        const bool ok_time = uptime_s >= MIN_RUN_SECONDS;

        if (ok_pzem && ok_time) {
            if (!announced) printf("  >>> SELFTEST PASS <<<\n");
            announced = true;
        } else {
            printf("  pendente:%s%s\n", ok_pzem ? "" : " pzem", ok_time ? "" : " tempo");
        }
        xTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_REPORT_MS));
    }
}

/* ------------------------------------------------------------------ init -- */
static void create_task_or_die(TaskFunction_t fn, const char *name, uint32_t stack_bytes,
                               UBaseType_t prio)
{
    if (xTaskCreatePinnedToCore(fn, name, stack_bytes, NULL, prio, NULL, APP_CORE) != pdPASS) {
        fatal(name);
    }
}

/*
 * A inicialização roda numa tarefa presa no núcleo 1: as interrupções dos
 * drivers (console e UART2) ficam no mesmo núcleo das tarefas.
 */
static void init_task(void *arg)
{
    (void)arg;
    board_init_console();
    board_init_led();
    board_init_pzem_uart();

    create_task_or_die(pzem_task, "pzem", STACK_SMALL, PRIO_PZEM);
    create_task_or_die(status_task, "status", STACK_SMALL, PRIO_STATUS);
    create_task_or_die(report_task, "report", STACK_REPORT, PRIO_REPORT);

    printf("[init] self-test pronto\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
    create_task_or_die(init_task, "init", STACK_REPORT, PRIO_INIT);
}
