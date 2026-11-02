/**
 * @file pzem_link.h
 * @brief Transação Modbus com o PZEM pela UART0 (transporte).
 *
 * Única dona da uart0 em tempo de execução: a tarefa do PZEM.
 */
#ifndef PZEM_LINK_H
#define PZEM_LINK_H

#include <stdint.h>
#include "pzem_modbus.h"

/** Espera não ocupada injetada pela aplicação (ex.: vTaskDelay). */
typedef void (*pzem_sleep_fn)(uint32_t ms);

/**
 * Envia a requisição, dorme enquanto a resposta chega na FIFO e decodifica.
 * Tempo máximo: ~25 ms de quadro + PZEM_LINK_MAX_WAIT_MS.
 */
pzem_parse_result_t pzem_link_read(pzem_sleep_fn sleep_ms, pzem_measurement_t *out);

#define PZEM_LINK_MAX_WAIT_MS 200u

#endif /* PZEM_LINK_H */
