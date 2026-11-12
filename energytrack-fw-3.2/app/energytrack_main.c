/**
 * @file energytrack_main.c
 * @brief Firmware do EnergyTrack (WPS 3.1): aquisição e processamento em tempo real.
 *
 * ESP32 DevKit (WROOM-32) + PZEM-004T v3 na UART2, com o FreeRTOS do
 * ESP-IDF. As tarefas de aquisição, processamento e saída, e as interrupções
 * delas, ficam presas no núcleo 1 (APP_CPU). O núcleo 0 fica com o sistema e
 * com a rede: a tarefa net manda a fotografia ao backend pelo Wi-Fi (feature
 * telemetria-wifi). Cada recurso tem uma única tarefa dona:
 *
 *   tarefa  núcleo prio  liberada por                     deadline  dona de
 *   ------  ------ ----  -------------------------------  --------  ----------------------
 *   proc      1     5    fila de amostras do pzem         50 ms     estado do processamento
 *   pzem      1     3    timer de hardware a cada 1 s     500 ms    UART2
 *   status    1     2    periódica, 50 ms                 soft      LED da placa (GPIO2)
 *   report    1     1    periódica, 2 s                   soft      console (UART0)
 *   net       0     2    periódica, 5 s                   soft      Wi-Fi e cliente HTTP
 *
 * Sem SSID ou URL do backend no menuconfig, a net não é criada e o firmware
 * roda sem rede. Números em src/rt/rt_tasks.c; diagrama em docs/diagrama-tarefas.md.
 * Cada job tem tempo de resposta medido (liberação -> término) contra o
 * deadline; o relatório traz a linha "rt t=..." e, depois de 10 min sem
 * estouro nas tarefas hard, ">>> RT PASS <<<".
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "acq_timer.h"
#include "board_init.h"
#include "http_post.h"
#include "processing.h"
#include "pzem_link.h"
#include "rt_monitor.h"
#include "rt_tasks.h"
#include "telemetry.h"
#include "wifi_link.h"

/* ---------------------------------------------------------------- config -- */
#define APP_CORE             1       /* núcleo 1 (APP_CPU): tarefas e ISRs do firmware */
#define NET_CORE             0       /* núcleo 0 (PRO_CPU): a rede, junto do Wi-Fi e do lwIP */
#define PRIO_INIT            10      /* acima de todas as tarefas do firmware (1..5) */
#define STACK_SMALL          3072u   /* bytes (no ESP-IDF a pilha conta em bytes) */
#define STACK_REPORT         6144u   /* printf usa bastante pilha */
#define STACK_NET            8192u   /* cliente HTTP; com https://, o handshake TLS roda nesta pilha */
#define NET_START_WAIT_MS    3000u   /* quanto o init espera o Wi-Fi iniciar antes de ligar o PZEM */
#define SAMPLE_QUEUE_LEN     8u
#define RT_PASS_SECONDS      600u    /* 10 min de bancada sem estouro */
#define PZEM_MIN_OK_PCT      95u
#define US_PER_S             1000000u
#define US_PER_MS            1000u

/* ----------------------------------------------------------------- tipos -- */
/* pzem -> proc. t_us = amostra pronta = liberação do job do proc. */
typedef struct {
    bool               ok;
    uint64_t           t_us;
    pzem_measurement_t m;
} sample_msg_t;

static QueueHandle_t s_q_samples;        /* pzem -> proc */
static QueueHandle_t s_q_snapshot;       /* proc -> status, report, net (caixa de 1, sobrescrita) */
static QueueHandle_t s_q_pzem_release;   /* ISR do timer -> pzem (1 liberação pendente no máximo) */
static QueueHandle_t s_q_net;            /* net (ou init, sem rede) -> report: estado da rede (caixa de 1) */

static TaskHandle_t s_task[RT_TASK_COUNT];
static TaskHandle_t s_net_task;               /* NULL quando a rede está desligada */
static TaskHandle_t s_init_task;              /* a net avisa o init quando o Wi-Fi iniciou */
static char s_net_body[TELEMETRY_JSON_MAX];   /* corpo do POST: só a net mexe (fora da pilha dela) */
static rt_monitor_t s_mon[RT_TASK_COUNT];     /* um escritor por monitor: a tarefa dona */
static volatile uint32_t s_pzem_lost_isr;     /* escrito só na ISR */
static proc_state_t s_proc;                   /* só a tarefa proc mexe */

static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

/* Sem o recurso o firmware não tem o que fazer: avisa e reinicia (o ESP-IDF imprime o backtrace). */
static void fatal(const char *what)
{
    printf("FATAL: %s\n", what);
    fflush(stdout);
    abort();
}

/* ------------------------------------------------------------------ ISRs -- */
static bool on_pzem_release(uint64_t release_us)
{
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(s_q_pzem_release, &release_us, &woken) != pdTRUE) {
        s_pzem_lost_isr++;   /* a leitura anterior ainda não terminou */
    }
    return woken == pdTRUE;  /* o driver troca de contexto na saída da ISR */
}

/* --------------------------------------------------------------- tarefas -- */
/* Leva para o monitor as liberações que a ISR não conseguiu entregar. */
static void account_lost(rt_monitor_t *mon, const volatile uint32_t *isr_count, uint32_t *seen)
{
    const uint32_t now = *isr_count;
    if (now != *seen) {
        rt_monitor_releases_lost(mon, now - *seen);
        *seen = now;
    }
}

/* Fila do proc cheia = amostra perdida = estouro de quem produziu. */
static void send_sample(rt_monitor_t *mon, sample_msg_t *msg)
{
    msg->t_us = now_us();
    if (xQueueSend(s_q_samples, msg, 0) != pdTRUE) {
        rt_monitor_releases_lost(mon, 1);
    }
}

static void pzem_task(void *arg)
{
    (void)arg;
    rt_monitor_t *mon = &s_mon[RT_TASK_PZEM];
    uint32_t lost_seen = 0;
    for (;;) {
        uint64_t release_us;
        xQueueReceive(s_q_pzem_release, &release_us, portMAX_DELAY);
        account_lost(mon, &s_pzem_lost_isr, &lost_seen);

        sample_msg_t msg = {0};
        msg.ok = (pzem_link_read(&msg.m) == PZEM_PARSE_OK);
        send_sample(mon, &msg);
        rt_monitor_job_done(mon, release_us, now_us());
    }
}

static void proc_task(void *arg)
{
    (void)arg;
    rt_monitor_t *mon = &s_mon[RT_TASK_PROC];
    proc_config_t cfg;
    proc_default_config(&cfg);
    proc_init(&s_proc, &cfg, now_us());

    for (;;) {
        sample_msg_t msg;
        xQueueReceive(s_q_samples, &msg, portMAX_DELAY);
        proc_on_pzem(&s_proc, msg.t_us, msg.ok, &msg.m);
        proc_snapshot_t snap;
        proc_snapshot(&s_proc, msg.t_us, &snap);
        xQueueOverwrite(s_q_snapshot, &snap);
        rt_monitor_job_done(mon, msg.t_us, now_us());
    }
}

static TickType_t period_ticks(rt_task_id_t id)
{
    return pdMS_TO_TICKS(RT_TASKS[id].period_us / US_PER_MS);
}

static void status_task(void *arg)
{
    (void)arg;
    rt_monitor_t *mon = &s_mon[RT_TASK_STATUS];
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        const uint64_t start = now_us();
        led_pattern_t pattern = LED_BLINK_SLOW;   /* ainda sem processamento */
        proc_snapshot_t snap;
        if (xQueuePeek(s_q_snapshot, &snap, 0) == pdTRUE) {
            pattern = proc_led_pattern(&snap);
        }
        board_led_set(led_pattern_level(pattern, start));
        rt_monitor_job_done(mon, start, now_us());
        if (xTaskDelayUntil(&last, period_ticks(RT_TASK_STATUS)) == pdFALSE) {
            rt_monitor_releases_lost(mon, 1);   /* já passou da hora da próxima liberação */
        }
    }
}

/* ------------------------------------------------------------------- net -- */
/*
 * Núcleo 0. Liga o Wi-Fi daqui (a interrupção do rádio fica no núcleo de quem
 * inicia o driver), avisa o init e, a cada 5 s, manda a fotografia mais
 * recente ao backend. Falha de rede só é contada: nada aqui bloqueia o
 * núcleo 1, e o próximo ciclo tenta de novo.
 */
static void net_task(void *arg)
{
    (void)arg;
    telemetry_status_t st;
    xQueuePeek(s_q_net, &st, 0);   /* o init publicou a configuração conferida */

    char device[TELEMETRY_DEVICE_MAX];
    wifi_link_device_id(device, sizeof device);
    if (!wifi_link_start(CONFIG_ET_WIFI_SSID, CONFIG_ET_WIFI_PASSWORD) || !http_post_init(CONFIG_ET_BACKEND_URL)) {
        st.cfg = TELEM_CFG_FALHA_INICIO;
    }
    xQueueOverwrite(s_q_net, &st);
    xTaskNotifyGive(s_init_task);
    if (st.cfg != TELEM_CFG_OK) {
        for (;;) vTaskDelay(portMAX_DELAY);   /* rede desligada; o relatório mostra o motivo */
    }

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        (void)xTaskDelayUntil(&last, pdMS_TO_TICKS(RT_NET_TASK.period_us / US_PER_MS));
        const uint64_t start = now_us();
        proc_snapshot_t snap;
        if (xQueuePeek(s_q_snapshot, &snap, 0) == pdTRUE) {
            const telemetry_header_t h = { device, telemetry_next_seq(&st.stats), (uint32_t)(start / US_PER_S) };
            const int len = telemetry_build_json(&h, &snap, s_net_body, sizeof s_net_body);
            const bool wifi_up = wifi_link_is_up();
            int http = 0;
            bool answered = false;
            if (len >= 0 && wifi_up) answered = http_post_json(s_net_body, len, &http);
            telemetry_record(&st.stats, telemetry_classify(len >= 0, wifi_up, answered, http), http);
        }
        st.wifi_up = wifi_link_is_up();
        wifi_link_ip(st.ip, sizeof st.ip);
        const uint32_t took_us = (uint32_t)(now_us() - start);
        if (took_us > st.max_send_us) st.max_send_us = took_us;
        xQueueOverwrite(s_q_net, &st);
    }
}

/* ---------------------------------------------------------------- report -- */
static void print_wh(const char *label, uint64_t mwh)
{
    printf("%s=%lu.%03lu Wh", label, (unsigned long)(mwh / 1000u), (unsigned long)(mwh % 1000u));
}

static void print_measurements(const proc_snapshot_t *s, uint32_t uptime_s)
{
    const pzem_measurement_t *m = &s->last;
    printf("\n[%5lus] PZEM ok=%lu falha=%lu | V=%u.%u V I=%lu.%03lu A P=%lu.%lu W f=%u.%u Hz FP=%u.%02u\n",
           (unsigned long)uptime_s, (unsigned long)s->pzem_ok, (unsigned long)s->pzem_fail,
           m->voltage_dv / 10u, m->voltage_dv % 10u,
           (unsigned long)(m->current_ma / 1000u), (unsigned long)(m->current_ma % 1000u),
           (unsigned long)(m->power_dw / 10u), (unsigned long)(m->power_dw % 10u),
           m->freq_dhz / 10u, m->freq_dhz % 10u, m->pf_centi / 100u, m->pf_centi % 100u);
}

static void print_energy_and_goal(const proc_snapshot_t *s)
{
    const uint32_t elapsed_min = (uint32_t)(s->day_elapsed_us / (60ull * US_PER_S));
    printf("  energia: dia %lu (%luh%02lu) ", (unsigned long)(s->day_index + 1u),
           (unsigned long)(elapsed_min / 60u), (unsigned long)(elapsed_min % 60u));
    print_wh("dia", s->day_mwh);
    printf(" | ");
    print_wh("anterior", s->prev_day_mwh);
    printf(" | ");
    print_wh("total", s->total_mwh);
    printf(" | Pmed=%lu.%lu W | lacunas recuperadas=%lu\n", (unsigned long)(s->avg_power_dw / 10u),
           (unsigned long)(s->avg_power_dw % 10u), (unsigned long)s->gaps_recovered);

    printf("  meta: uso=%lu%% ", (unsigned long)s->goal_used_pct);
    print_wh("projecao", s->projected_mwh);
    printf(" estado=%s | limite de potencia: %s\n", goal_status_name(s->goal_status),
           s->power_alert ? "ALERTA" : "ok");
}

static void print_anomalies(const proc_snapshot_t *s)
{
    printf("  anomalias:");
    if (s->anomalies == 0u) printf(" nenhuma");
    for (uint32_t bit = 0; bit < ANOM_COUNT; ++bit) {
        const anomaly_flag_t flag = (anomaly_flag_t)(1u << bit);
        if (!(s->anomalies & flag)) continue;
        printf(" %s", anomaly_name(flag));
        if (flag == ANOM_SUBTENSAO || flag == ANOM_SOBRETENSAO) {
            printf("(%s)", voltage_band_name(s->v_severity));
        }
    }
    printf(" | saltos=%lu\n", (unsigned long)s->spikes);
}

static void print_network(void)
{
    telemetry_status_t net;
    if (xQueuePeek(s_q_net, &net, 0) != pdTRUE) return;
    char line[200];
    telemetry_format_status(&net, line, sizeof line);
    printf("  %s\n", line);
}

static void print_rt_line(uint32_t uptime_s)
{
    char line[400];
    size_t n = (size_t)snprintf(line, sizeof line, "  rt t=%lus", (unsigned long)uptime_s);
    for (uint32_t i = 0; i < RT_TASK_COUNT && n + 1u < sizeof line; ++i) {
        line[n++] = ' ';
        const int w = rt_monitor_format(line + n, sizeof line - n, RT_TASKS[i].name, &s_mon[i]);
        if (w < 0) break;
        n += (size_t)w;
    }
    if (n >= sizeof line) n = sizeof line - 1u;
    line[n] = '\0';
    printf("%s\n", line);

    printf("  heap livre=%lu B | pilha livre (bytes):", (unsigned long)esp_get_free_heap_size());
    for (uint32_t i = 0; i < RT_TASK_COUNT; ++i) {
        printf(" %s=%lu", RT_TASKS[i].name, (unsigned long)uxTaskGetStackHighWaterMark(s_task[i]));
    }
    if (s_net_task) printf(" %s=%lu", RT_NET_TASK.name, (unsigned long)uxTaskGetStackHighWaterMark(s_net_task));
    printf("\n");
}

static bool hard_tasks_clean(void)
{
    for (uint32_t i = 0; i < RT_TASK_COUNT; ++i) {
        if (!RT_TASKS[i].hard) continue;
        if (s_mon[i].misses != 0u || s_mon[i].jobs == 0u) return false;
    }
    return true;
}

static void report_task(void *arg)
{
    (void)arg;
    rt_monitor_t *mon = &s_mon[RT_TASK_REPORT];
    TickType_t last = xTaskGetTickCount();
    bool announced = false;

    for (;;) {
        const uint64_t start = now_us();
        const uint32_t uptime_s = (uint32_t)(start / US_PER_S);
        proc_snapshot_t snap = {0};
        const bool have = (xQueuePeek(s_q_snapshot, &snap, 0) == pdTRUE);

        print_measurements(&snap, uptime_s);
        if (have) {
            print_energy_and_goal(&snap);
            print_anomalies(&snap);
        }
        print_network();
        print_rt_line(uptime_s);

        const uint32_t total = snap.pzem_ok + snap.pzem_fail;
        const bool pzem_ok = snap.pzem_ok > 0u && (uint64_t)snap.pzem_ok * 100u >= (uint64_t)total * PZEM_MIN_OK_PCT;
        const bool clean = hard_tasks_clean();
        if (clean && pzem_ok && uptime_s >= RT_PASS_SECONDS) {
            if (!announced) printf("  >>> RT PASS <<< %u s sem estouro de deadline\n", RT_PASS_SECONDS);
            announced = true;
            printf("  RT: OK\n");
        } else if (!clean) {
            printf("  RT: ESTOURO DE DEADLINE (ou tarefa sem jobs)\n");
        } else if (!pzem_ok) {
            printf("  RT: aguardando PZEM com >= %u%% de leituras validas\n", PZEM_MIN_OK_PCT);
        } else {
            printf("  RT: OK ate agora, faltam %lu s para o aceite\n", (unsigned long)(RT_PASS_SECONDS - uptime_s));
        }

        rt_monitor_job_done(mon, start, now_us());
        if (xTaskDelayUntil(&last, period_ticks(RT_TASK_REPORT)) == pdFALSE) {
            rt_monitor_releases_lost(mon, 1);
        }
    }
}

/* ------------------------------------------------------------------ init -- */
static TaskHandle_t create_task_on_core(TaskFunction_t fn, const char *name, uint32_t stack_bytes,
                                        UBaseType_t prio, BaseType_t core)
{
    TaskHandle_t handle = NULL;
    if (xTaskCreatePinnedToCore(fn, name, stack_bytes, NULL, prio, &handle, core) != pdPASS) {
        fatal(name);
    }
    return handle;
}

static TaskHandle_t create_task_or_die(TaskFunction_t fn, const char *name, uint32_t stack_bytes,
                                       UBaseType_t prio)
{
    return create_task_on_core(fn, name, stack_bytes, prio, APP_CORE);
}

/*
 * Rede: confere a configuração do menuconfig. Sem SSID ou URL válidos, a net
 * nem é criada e o relatório mostra o motivo. Com eles, a net liga o Wi-Fi
 * no núcleo 0 e o init espera ela avisar antes de ligar o timer do PZEM:
 * a partida do rádio (calibração, alocações) termina antes do primeiro job medido.
 */
static void start_network(void)
{
    telemetry_status_t net;
    memset(&net, 0, sizeof net);
    telemetry_stats_init(&net.stats);
    net.cfg = telemetry_check_config(CONFIG_ET_WIFI_SSID, CONFIG_ET_WIFI_PASSWORD, CONFIG_ET_BACKEND_URL);
    xQueueOverwrite(s_q_net, &net);
    if (net.cfg != TELEM_CFG_OK) {
        printf("[init] rede desligada: %s\n", telemetry_cfg_reason(net.cfg));
        return;
    }
    s_init_task = xTaskGetCurrentTaskHandle();
    s_net_task = create_task_on_core(net_task, RT_NET_TASK.name, STACK_NET, RT_NET_TASK.priority, NET_CORE);
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NET_START_WAIT_MS)) == 0u) {
        printf("[init] o Wi-Fi demorou mais de %u ms para iniciar; a aquisicao segue sem esperar\n", NET_START_WAIT_MS);
    }
}

/*
 * A inicialização roda numa tarefa presa no núcleo 1, com a maior prioridade
 * do firmware: os drivers alocam as interrupções no núcleo de quem os
 * instala, então UARTs e timer atendem no núcleo 1, junto das tarefas
 * (a análise de tempo real é de um núcleo só). Ela cria tudo antes que
 * qualquer tarefa rode, sobe a rede no núcleo 0 (start_network) e só então
 * liga a fonte de liberação (timer do PZEM).
 */
static void init_task(void *arg)
{
    (void)arg;
    board_init_console();
    board_init_led();
    board_init_pzem_uart();

    s_q_samples = xQueueCreate(SAMPLE_QUEUE_LEN, sizeof(sample_msg_t));
    s_q_snapshot = xQueueCreate(1, sizeof(proc_snapshot_t));
    s_q_pzem_release = xQueueCreate(1, sizeof(uint64_t));
    s_q_net = xQueueCreate(1, sizeof(telemetry_status_t));
    if (!s_q_samples || !s_q_snapshot || !s_q_pzem_release || !s_q_net) fatal("filas");

    for (uint32_t i = 0; i < RT_TASK_COUNT; ++i) rt_monitor_init(&s_mon[i], RT_TASKS[i].deadline_us);

    static const struct {
        TaskFunction_t fn;
        uint32_t       stack;
    } entry[RT_TASK_COUNT] = {
        [RT_TASK_PROC]   = {proc_task, STACK_SMALL},
        [RT_TASK_PZEM]   = {pzem_task, STACK_SMALL},
        [RT_TASK_STATUS] = {status_task, STACK_SMALL},
        [RT_TASK_REPORT] = {report_task, STACK_REPORT},
    };
    for (uint32_t i = 0; i < RT_TASK_COUNT; ++i) {
        s_task[i] = create_task_or_die(entry[i].fn, RT_TASKS[i].name, entry[i].stack, RT_TASKS[i].priority);
    }

    start_network();

    if (!acq_timer_start(RT_TASKS[RT_TASK_PZEM].period_us, on_pzem_release)) fatal("sem timer livre para o PZEM");

    printf("[init] aquisicao iniciada no nucleo %d\n", (int)xPortGetCoreID());
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* app_main roda no núcleo 0; o firmware vai todo para o núcleo 1. */
    create_task_or_die(init_task, "init", STACK_REPORT, PRIO_INIT);
}
