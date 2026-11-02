#include "rt_monitor.h"

#include <stdio.h>
#include <string.h>

void rt_monitor_init(rt_monitor_t *m, uint32_t deadline_us)
{
    memset(m, 0, sizeof *m);
    m->deadline_us = deadline_us;
}

bool rt_monitor_job_done(rt_monitor_t *m, uint64_t release_us, uint64_t done_us)
{
    const uint64_t r64 = done_us > release_us ? done_us - release_us : 0u;
    const uint32_t r = r64 > UINT32_MAX ? UINT32_MAX : (uint32_t)r64;

    if (r > m->max_response_us) m->max_response_us = r;
    m->jobs++;
    if (r > m->deadline_us) {
        m->misses++;
        return false;
    }
    return true;
}

void rt_monitor_releases_lost(rt_monitor_t *m, uint32_t n)
{
    m->misses += n;
}

int rt_monitor_format(char *buf, size_t size, const char *name, const rt_monitor_t *m)
{
    return snprintf(buf, size, "%s:jobs=%lu,miss=%lu,rmax=%luus,d=%luus", name,
                    (unsigned long)m->jobs, (unsigned long)m->misses,
                    (unsigned long)m->max_response_us, (unsigned long)m->deadline_us);
}
