/**
 * FreeRTOSConfig.h — RP2040 (Pico W), um núcleo, FreeRTOS-Kernel >= V11.
 *
 * Só o que o firmware e o port do RP2040 usam; o resto fica no padrão do
 * kernel. Os firmwares usam tarefas, filas, xTaskDelayUntil e vTaskDelay.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <assert.h>

/* Escalonador: preemptivo por prioridade, tick de 1 ms, um núcleo */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0   /* o Cortex-M0+ não tem a instrução CLZ */
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    8   /* tarefas usam 1..5; o init usa 7 */
#define configMINIMAL_STACK_SIZE                ((configSTACK_DEPTH_TYPE)256)
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configNUMBER_OF_CORES                   1

/* Memória: tarefas e filas criadas em tempo de execução, heap_4 de 64 KB */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   (64 * 1024)
#define configSTACK_DEPTH_TYPE                  uint32_t

/* Diagnóstico: estouro de pilha e falta de heap viram panic() (hooks no main) */
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configASSERT(x)                         assert(x)

/* Integração com o pico-sdk: mutex e sleep do SDK cooperam com o escalonador */
#define configSUPPORT_PICO_SYNC_INTEROP         1
#define configSUPPORT_PICO_TIME_INTEROP         1

/*
 * Timers de software: o firmware não usa, mas o port do RP2040 sim — a
 * integração acima acorda tarefas com xEventGroupSetBitsFromISR, que roda
 * pela tarefa de timers. Ela só trabalha nesses raros avisos vindos de IRQ
 * (entra no orçamento de "interrupções e kernel" da análise de tempo real).
 */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            1024

/* Funções da API usadas pelo firmware (e pelo port do RP2040) */
#define INCLUDE_vTaskDelete                     1   /* a tarefa init se apaga */
#define INCLUDE_vTaskSuspend                    1   /* portMAX_DELAY = esperar para sempre */
#define INCLUDE_xTaskDelayUntil                 1   /* tarefas periódicas */
#define INCLUDE_vTaskDelay                      1   /* espera da resposta do PZEM */
#define INCLUDE_xTaskGetCurrentTaskHandle       1   /* exigida pelo port do RP2040 */
#define INCLUDE_xTimerPendFunctionCall          1   /* exigida pelo port do RP2040 (ver timers) */
#define INCLUDE_uxTaskGetStackHighWaterMark     1   /* pilha livre no relatório */

#endif /* FREERTOS_CONFIG_H */
