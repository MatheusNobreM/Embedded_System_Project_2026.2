#include "rt_tasks.h"

#define MS 1000u

/*
 * Orçamentos no núcleo 1 do ESP32 (240 MHz), com folga — os mesmos números da
 * Pico W (125 MHz), agora com ainda mais margem:
 *  proc   4 ms  cobre 1 bloco de ADC + 1 leitura do PZEM na mesma janela de 100 ms
 *  adc    1 ms  calibra 100 leituras (line fitting do eFuse), soma, converte e põe na fila
 *  pzem   3 ms  CPU; dorme até 220 ms esperando a resposta pela UART (21 x 10 ms)
 *  status 1 ms  lê o snapshot e escreve no GPIO2; 50 ms amostra bem o pisca de 5 Hz
 *  report 30 ms printf de ~1 KB para o buffer do driver da UART0 (a IRQ esvazia a 115200 bps)
 */
const rt_task_param_t RT_TASKS[RT_TASK_COUNT] = {
    /*                  nome      prio período    deadline   orçamento suspensão  hard */
    [RT_TASK_PROC]   = { "proc",   5,  100 * MS,   50 * MS,  4 * MS,   0,        true  },
    [RT_TASK_ADC]    = { "adc",    4,  100 * MS,  100 * MS,  1 * MS,   0,        true  },
    [RT_TASK_PZEM]   = { "pzem",   3, 1000 * MS,  500 * MS,  3 * MS,   220 * MS, true  },
    [RT_TASK_STATUS] = { "status", 2,   50 * MS,   50 * MS,  1 * MS,   0,        false },
    [RT_TASK_REPORT] = { "report", 1, 2000 * MS, 2000 * MS, 30 * MS,   0,        false },
};

/* Núcleo 1: tick de 1 kHz, IRQs das UARTs (PZEM e console), do DMA do ADC (a cada 10 ms,
 * com a redução 20:1) e do gptimer: orçamento de 50 µs a cada 1 ms (5% da CPU). */
const rt_task_param_t RT_ISR_LOAD = { "isr", 255, 1 * MS, 1 * MS, 50, 0, true };
