#include "rt_sched.h"

static uint64_t ceil_div(uint64_t a, uint64_t b)
{
    return (a + b - 1u) / b;
}

bool rt_sched_rta(const rt_sched_task_t *tasks, size_t n, uint32_t *response_us)
{
    if (n > RT_SCHED_MAX_TASKS) return false;

    /* Ordem de análise: prioridade decrescente, para já conhecer R_j das maiores. */
    size_t order[RT_SCHED_MAX_TASKS];
    bool   known[RT_SCHED_MAX_TASKS] = {false};
    for (size_t i = 0; i < n; ++i) order[i] = i;
    for (size_t i = 1; i < n; ++i) {
        size_t k = order[i], j = i;
        while (j > 0 && tasks[order[j - 1]].priority < tasks[k].priority) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = k;
    }

    bool all_ok = true;
    for (size_t oi = 0; oi < n; ++oi) {
        const size_t i = order[oi];
        const rt_sched_task_t *ti = &tasks[i];
        uint64_t r = (uint64_t)ti->c_us + ti->s_us;

        for (;;) {
            uint64_t next = (uint64_t)ti->c_us + ti->s_us;
            for (size_t j = 0; j < n; ++j) {
                if (j == i || tasks[j].priority < ti->priority || tasks[j].t_us == 0u) continue;
                uint64_t jitter = 0;
                if (tasks[j].s_us > 0u) {
                    /* Empate de prioridade ainda sem R_j: jitter pessimista até o deadline. */
                    const uint64_t rj = known[j] ? response_us[j] : tasks[j].d_us;
                    jitter = rj > tasks[j].c_us ? rj - tasks[j].c_us : 0u;
                }
                next += ceil_div(r + jitter, tasks[j].t_us) * tasks[j].c_us;
            }
            if (next > ti->d_us) {
                response_us[i] = next > UINT32_MAX ? UINT32_MAX : (uint32_t)next;
                all_ok = false;
                break;
            }
            if (next == r) {
                response_us[i] = (uint32_t)r;
                break;
            }
            r = next;
        }
        known[i] = true;
    }
    return all_ok;
}

uint32_t rt_sched_utilization_permille(const rt_sched_task_t *tasks, size_t n)
{
    uint64_t permille = 0;
    for (size_t i = 0; i < n; ++i) {
        if (tasks[i].t_us == 0u) continue;
        permille += ((uint64_t)tasks[i].c_us * 1000u) / tasks[i].t_us;
    }
    return (uint32_t)permille;
}
