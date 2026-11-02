/**
 * Teste de host do monitor de deadline, da análise de tempo de resposta e da
 * tabela de tarefas do firmware.
 *
 * Também imprime, como comentários TAP, a tabela ("# task: ...") e o
 * resultado da análise ("# rta: ...") — o teste do diagrama e o teste do
 * log de bancada usam essas linhas.
 */
#include <stdio.h>
#include <string.h>
#include "rt_monitor.h"
#include "rt_sched.h"
#include "rt_tasks.h"
#include "tap.h"

#define MS 1000u
#define SET_SIZE (RT_TASK_COUNT + 1u)   /* tarefas do firmware + carga de interrupções */

static rt_sched_task_t to_sched(const rt_task_param_t *t)
{
    rt_sched_task_t s = {
        .c_us = t->budget_us, .s_us = t->suspend_us, .t_us = t->period_us,
        .d_us = t->deadline_us, .priority = t->priority,
    };
    return s;
}

/* Tabela do firmware no formato da análise, na ordem de rt_task_id_t, com a carga de ISR por último. */
static void firmware_set(rt_sched_task_t out[SET_SIZE])
{
    for (size_t i = 0; i < RT_TASK_COUNT; ++i) out[i] = to_sched(&RT_TASKS[i]);
    out[RT_TASK_COUNT] = to_sched(&RT_ISR_LOAD);
}

/* ----------------------------------------------------------- monitor -- */
static void test_monitor_counts_late_jobs(void)
{
    rt_monitor_t m;
    rt_monitor_init(&m, 50 * MS);
    TAP_CHECK(rt_monitor_job_done(&m, 1000000, 1000000 + 12 * MS), "12 ms cumpre deadline de 50 ms");
    TAP_CHECK(rt_monitor_job_done(&m, 2000000, 2000000 + 50 * MS), "exatamente no deadline ainda cumpre");
    TAP_CHECK(m.misses == 0 && m.jobs == 2, "sem estouro até aqui (jobs=%u miss=%u)", (unsigned)m.jobs, (unsigned)m.misses);
    TAP_CHECK(!rt_monitor_job_done(&m, 3000000, 3000000 + 51 * MS), "51 ms estoura o deadline");
    TAP_CHECK(m.misses == 1 && m.jobs == 3, "estouro contado (jobs=%u miss=%u)", (unsigned)m.jobs, (unsigned)m.misses);
    TAP_CHECK(m.max_response_us == 51 * MS, "maior resposta = %u µs (esperado 51000)", (unsigned)m.max_response_us);
    rt_monitor_job_done(&m, 4000000, 4000000 + 5 * MS);
    TAP_CHECK(m.max_response_us == 51 * MS, "o máximo continua registrado depois de um job rápido");
}

static void test_monitor_counts_lost_releases(void)
{
    rt_monitor_t m;
    rt_monitor_init(&m, 500 * MS);
    rt_monitor_releases_lost(&m, 2);
    TAP_CHECK(m.misses == 2 && m.jobs == 0, "2 liberações perdidas viram 2 estouros (miss=%u)", (unsigned)m.misses);
}

static void test_monitor_format(void)
{
    rt_monitor_t m;
    rt_monitor_init(&m, 50 * MS);
    rt_monitor_job_done(&m, 0, 212);
    char buf[96];
    rt_monitor_format(buf, sizeof buf, "proc", &m);
    TAP_CHECK(strcmp(buf, "proc:jobs=1,miss=0,rmax=212us,d=50000us") == 0, "formato do log: '%s'", buf);
    printf("# rtline: rt t=600s %s\n", buf);
}

/* --------------------------------------------------------------- RTA -- */
static void test_rta_textbook(void)
{
    /* (C, T) = (1, 4), (2, 6), (3, 10) com D = T: R = 1, 3, 10. */
    const rt_sched_task_t set[] = {
        { .c_us = 1, .t_us = 4,  .d_us = 4,  .priority = 3 },
        { .c_us = 2, .t_us = 6,  .d_us = 6,  .priority = 2 },
        { .c_us = 3, .t_us = 10, .d_us = 10, .priority = 1 },
    };
    uint32_t r[3];
    TAP_CHECK(rt_sched_rta(set, 3, r), "conjunto clássico é escalonável");
    TAP_CHECK(r[0] == 1 && r[1] == 3 && r[2] == 10, "R = %u, %u, %u (esperado 1, 3, 10)",
              (unsigned)r[0], (unsigned)r[1], (unsigned)r[2]);
}

static void test_rta_self_suspension_as_jitter(void)
{
    /* t1 se suspende 5: R1 = 6, J1 = 5. t2: R = 6 + ceil((R + 5) / 10) * 1 -> 8 (sem jitter daria 7). */
    const rt_sched_task_t set[] = {
        { .c_us = 1, .s_us = 5, .t_us = 10, .d_us = 10, .priority = 2 },
        { .c_us = 6,            .t_us = 10, .d_us = 10, .priority = 1 },
    };
    uint32_t r[2];
    TAP_CHECK(rt_sched_rta(set, 2, r), "conjunto com suspensão é escalonável");
    TAP_CHECK(r[0] == 6 && r[1] == 8, "R = %u, %u (esperado 6, 8)", (unsigned)r[0], (unsigned)r[1]);
}

static void test_rta_firmware_table_fits(void)
{
    rt_sched_task_t set[SET_SIZE];
    uint32_t r[SET_SIZE];
    firmware_set(set);
    const bool ok = rt_sched_rta(set, SET_SIZE, r);
    TAP_CHECK(ok, "a tabela de tarefas do firmware deveria ser escalonável");
    for (size_t i = 0; i < RT_TASK_COUNT; ++i) {
        TAP_CHECK(r[i] <= RT_TASKS[i].deadline_us, "%s: R = %u µs > D = %u µs", RT_TASKS[i].name,
                  (unsigned)r[i], (unsigned)RT_TASKS[i].deadline_us);
        printf("# rta: %s R=%luus D=%luus\n", RT_TASKS[i].name, (unsigned long)r[i],
               (unsigned long)RT_TASKS[i].deadline_us);
    }
    const uint32_t u = rt_sched_utilization_permille(set, SET_SIZE);
    TAP_CHECK(u < 1000u, "utilização %u‰ deveria ficar abaixo de 100%%", (unsigned)u);
    printf("# rta: utilizacao=%lu permil\n", (unsigned long)u);
}

static void test_rta_detects_overload(void)
{
    rt_sched_task_t set[SET_SIZE];
    uint32_t r[SET_SIZE];
    firmware_set(set);
    set[RT_TASK_PROC].c_us = 60 * MS;          /* proc gastando mais que o deadline de 50 ms */
    TAP_CHECK(!rt_sched_rta(set, SET_SIZE, r), "proc com 60 ms deveria ser acusado");
    TAP_CHECK(r[RT_TASK_PROC] > RT_TASKS[RT_TASK_PROC].deadline_us, "R do proc deveria passar do deadline");

    const rt_sched_task_t heavy[] = {
        { .c_us = 6, .t_us = 10, .d_us = 10, .priority = 2 },
        { .c_us = 5, .t_us = 10, .d_us = 10, .priority = 1 },   /* 110% de utilização */
    };
    uint32_t r2[2];
    TAP_CHECK(!rt_sched_rta(heavy, 2, r2), "110%% de utilização não é escalonável");
}

/* ------------------------------------------------- tabela de tarefas -- */
static void test_task_table_rules(void)
{
    for (size_t i = 0; i < RT_TASK_COUNT; ++i) {
        const rt_task_param_t *t = &RT_TASKS[i];
        TAP_CHECK(t->deadline_us <= t->period_us, "%s: deadline acima do período", t->name);
        TAP_CHECK(t->priority >= 1 && t->priority <= 6, "%s: prioridade %u fora de 1..6 (7 é o init)", t->name,
                  (unsigned)t->priority);
        for (size_t j = 0; j < RT_TASK_COUNT; ++j) {
            const rt_task_param_t *u = &RT_TASKS[j];
            if (i == j) continue;
            TAP_CHECK(t->priority != u->priority, "%s e %s com a mesma prioridade", t->name, u->name);
            if (t->hard && !u->hard)
                TAP_CHECK(t->priority > u->priority, "hard %s abaixo da soft %s", t->name, u->name);
            if (t->hard && u->hard && t->deadline_us < u->deadline_us)
                TAP_CHECK(t->priority > u->priority, "deadline monotônico violado: %s x %s", t->name, u->name);
        }
        printf("# task: %s,%u,%lu,%lu,%lu,%lu,%s\n", t->name, (unsigned)t->priority,
               (unsigned long)t->period_us, (unsigned long)t->deadline_us, (unsigned long)t->budget_us,
               (unsigned long)t->suspend_us, t->hard ? "hard" : "soft");
    }
}

int main(void)
{
    tap_run("AC-016 job depois do deadline conta estouro e registra a maior resposta", test_monitor_counts_late_jobs);
    tap_run("AC-016 liberação perdida conta estouro", test_monitor_counts_lost_releases);
    tap_run("formato da linha de tempo real no log", test_monitor_format);
    tap_run("AC-017 RTA reproduz o exemplo clássico", test_rta_textbook);
    tap_run("AC-017 RTA trata suspensão como jitter", test_rta_self_suspension_as_jitter);
    tap_run("AC-017 tabela do firmware cabe nos deadlines", test_rta_firmware_table_fits);
    tap_run("AC-017 conjunto sobrecarregado é acusado", test_rta_detects_overload);
    tap_run("tabela: prioridades únicas, hard acima de soft, deadline monotônico", test_task_table_rules);
    return tap_done();
}
