#include "acq_timer.h"

#include "driver/gptimer.h"
#include "esp_timer.h"

#define TIMER_RESOLUTION_HZ 1000000u   /* 1 tique = 1 µs */

static gptimer_handle_t s_timer;
static acq_timer_cb s_on_release;

static bool on_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    (void)timer;
    (void)edata;
    (void)user_ctx;
    /* Mesma base de tempo das tarefas (esp_timer_get_time pode ser chamada na ISR). */
    return s_on_release((uint64_t)esp_timer_get_time());
}

bool acq_timer_start(uint32_t period_us, acq_timer_cb on_release)
{
    s_on_release = on_release;

    const gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = TIMER_RESOLUTION_HZ,
    };
    if (gptimer_new_timer(&cfg, &s_timer) != ESP_OK) return false;

    const gptimer_event_callbacks_t cbs = {.on_alarm = on_alarm};
    const gptimer_alarm_config_t alarm = {
        .alarm_count = period_us,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,   /* o hardware zera o contador no alarme: taxa fixa */
    };
    return gptimer_register_event_callbacks(s_timer, &cbs, NULL) == ESP_OK &&
           gptimer_set_alarm_action(s_timer, &alarm) == ESP_OK &&
           gptimer_enable(s_timer) == ESP_OK &&
           gptimer_start(s_timer) == ESP_OK;
}
