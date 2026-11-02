/**
 * @file selftest_main.c
 * @brief Self-test do WPS 3.2 na Pico W pura: PZEM na UART0, LED da placa e log USB.
 *
 * Cada recurso tem uma única tarefa dona:
 *
 *   tarefa   prio  dona de                       período
 *   -------  ----  ----------------------------  -------
 *   pzem      3    uart0 (GP0/GP1)               1 s
 *   status    2    LED da placa (CYW43)          100 ms
 *   report    1    stdio USB                     2 s
 *
 * Saída: relatório pelo USB-CDC a cada 2 s e "SELFTEST PASS" depois de 60 s
 * com o PZEM respondendo e no máximo 5% de leituras com falha.
 * LED aceso fixo = PZEM respondendo; piscando = sem resposta válida.
 */
#include <stdbool.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

#include "board_init.h"
#include "pzem_link.h"

/* ---------------------------------------------------------------- config -- */
#define PRIO_INIT            (configMAX_PRIORITIES - 1)
#define PRIO_PZEM            3
#define PRIO_STATUS          2
#define PRIO_REPORT          1

#define STACK_SMALL          512u    /* palavras (2 KB) */
#define STACK_PRINTF         1024u   /* printf usa bastante pilha */

#define PERIOD_PZEM_MS       1000u
#define PERIOD_STATUS_MS     100u
#define PERIOD_REPORT_MS     2000u
#define BLINK_HALF_PERIOD_MS 500u    /* sem PZEM: pisca a 1 Hz */

#define MIN_RUN_SECONDS      60u     /* janela mínima antes de declarar PASS */
#define PZEM_MAX_FAIL_PCT    5u

/* ----------------------------------------------------------------- tipos -- */
/*
 * Estatísticas com UM escritor por campo (a tarefa dona); a report só lê.
 * Leitura/escrita de 32 bits alinhada é atômica no Cortex-M0+: sem mutex.
 */
typedef struct {
    volatile uint32_t cyw43_ok;
    volatile uint32_t pzem_ok, pzem_fail, pzem_last;
    volatile uint32_t voltage_dv, current_ma, power_dw, energy_wh, freq_dhz, pf_centi;
} selftest_stats_t;

static selftest_stats_t s_stats;

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

        printf("\n[%4lus] cyw43=%s  heap_livre=%u B\n", (unsigned long)uptime_s,
               s_stats.cyw43_ok ? "ok" : "FALHOU", (unsigned)xPortGetFreeHeapSize());
        printf("  PZEM  ok=%lu falha=%lu ultimo=%lu\n", (unsigned long)s_stats.pzem_ok,
               (unsigned long)s_stats.pzem_fail, (unsigned long)s_stats.pzem_last);
        printf("  V=%lu.%lu V  I=%lu.%03lu A  P=%lu.%lu W  E=%lu Wh  f=%lu.%lu Hz  FP=%lu.%02lu\n",
               (unsigned long)(s_stats.voltage_dv / 10u), (unsigned long)(s_stats.voltage_dv % 10u),
               (unsigned long)(s_stats.current_ma / 1000u), (unsigned long)(s_stats.current_ma % 1000u),
               (unsigned long)(s_stats.power_dw / 10u), (unsigned long)(s_stats.power_dw % 10u),
               (unsigned long)s_stats.energy_wh,
               (unsigned long)(s_stats.freq_dhz / 10u), (unsigned long)(s_stats.freq_dhz % 10u),
               (unsigned long)(s_stats.pf_centi / 100u), (unsigned long)(s_stats.pf_centi % 100u));

        const bool ok_cyw43 = s_stats.cyw43_ok != 0;
        const bool ok_pzem  = pzem_within_tolerance();
        const bool ok_time  = uptime_s >= MIN_RUN_SECONDS;

        if (ok_cyw43 && ok_pzem && ok_time) {
            if (!announced) printf("  >>> SELFTEST PASS <<<\n");
            announced = true;
        } else {
            printf("  pendente:%s%s%s\n", ok_cyw43 ? "" : " cyw43", ok_pzem ? "" : " pzem",
                   ok_time ? "" : " tempo");
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
 * FreeRTOS espera o escalonador ativo.
 */
static void init_task(void *arg)
{
    (void)arg;
    s_stats.cyw43_ok = board_init_cyw43() ? 1u : 0u;
    if (!s_stats.cyw43_ok) printf("[init] cyw43_arch_init falhou\n");
    board_init_pzem_uart();

    create_task_or_panic(pzem_task, "pzem", STACK_SMALL, PRIO_PZEM);
    create_task_or_panic(status_task, "status", STACK_SMALL, PRIO_STATUS);
    create_task_or_panic(report_task, "report", STACK_PRINTF, PRIO_REPORT);

    printf("[init] self-test pronto\n");
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
