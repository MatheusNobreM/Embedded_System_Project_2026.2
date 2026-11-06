/**
 * @file pzem_link.h
 * @brief Transação Modbus com o PZEM pela UART2 (transporte).
 *
 * Única dona da UART2 em tempo de execução: a tarefa do PZEM.
 */
#ifndef PZEM_LINK_H
#define PZEM_LINK_H

#include <stdint.h>
#include "pzem_modbus.h"

/**
 * Envia a requisição e dorme no driver da UART (bloqueio do FreeRTOS, sem
 * espera ocupada) enquanto a resposta chega; depois decodifica.
 * Tempo máximo: ~26 ms de quadro + PZEM_LINK_MAX_WAIT_MS.
 */
pzem_parse_result_t pzem_link_read(pzem_measurement_t *out);

#define PZEM_LINK_MAX_WAIT_MS 200u

#endif /* PZEM_LINK_H */
