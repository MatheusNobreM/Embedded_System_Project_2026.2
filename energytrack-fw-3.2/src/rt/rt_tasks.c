#include "rt_tasks.h"

#define MS 1000u

/*
 * Orçamentos no núcleo 1 do ESP32 (240 MHz), com folga:
 *  proc   4 ms  uma leitura do PZEM: energia, meta, anomalias e a fotografia.
 *               Período = intervalo mínimo entre duas amostras do PZEM: liberadas
 *               a cada 1 s e prontas até 500 ms depois (deadline do pzem), ficam
 *               a pelo menos 1 s - 500 ms = 500 ms uma da outra.
 *  pzem   3 ms  CPU; dorme até 220 ms esperando a resposta pela UART (21 x 10 ms)
 *  status 1 ms  lê o snapshot e escreve no GPIO2; 50 ms amostra bem o pisca de 5 Hz
 *  report 30 ms printf de ~1 KB para o buffer do driver da UART0 (a IRQ esvazia a 115200 bps)
 */
const rt_task_param_t RT_TASKS[RT_TASK_COUNT] = {
    /*                  nome      prio período    deadline   orçamento suspensão  hard */
    [RT_TASK_PROC]   = { "proc",   5,  500 * MS,   50 * MS,  4 * MS,   0,        true  },
    [RT_TASK_PZEM]   = { "pzem",   3, 1000 * MS,  500 * MS,  3 * MS,   220 * MS, true  },
    [RT_TASK_STATUS] = { "status", 2,   50 * MS,   50 * MS,  1 * MS,   0,        false },
    [RT_TASK_REPORT] = { "report", 1, 2000 * MS, 2000 * MS, 30 * MS,   0,        false },
};

/* Núcleo 1: tick de 1 kHz e IRQs das UARTs (PZEM e console) e do gptimer:
 * orçamento de 50 µs a cada 1 ms (5% da CPU), com folga. */
const rt_task_param_t RT_ISR_LOAD = { "isr", 255, 1 * MS, 1 * MS, 50, 0, true };
