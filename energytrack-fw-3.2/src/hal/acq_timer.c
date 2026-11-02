#include "acq_timer.h"

#include "pico/time.h"

static repeating_timer_t s_timer;
static acq_timer_cb s_on_release;

static bool on_alarm(repeating_timer_t *rt)
{
    (void)rt;
    s_on_release(time_us_64());
    return true;   /* continua repetindo */
}

bool acq_timer_start(uint32_t period_us, acq_timer_cb on_release)
{
    s_on_release = on_release;
    /* Atraso negativo = intervalo entre INÍCIOS de callback: taxa fixa, sem deriva. */
    return add_repeating_timer_us(-(int64_t)period_us, on_alarm, NULL, &s_timer);
}
