/**
 * @file board_init.h
 * @brief Inicialização do ESP32 DevKit: console, UART2 do PZEM e LED da placa.
 *
 * Cada função inicializa UM recurso e não cria tarefas: a posse em tempo de
 * execução (qual tarefa usa o quê) é decidida pela aplicação. As interrupções
 * dos drivers ficam no núcleo que chama a função — chame de uma tarefa presa
 * no núcleo 1, como as do firmware.
 *
 * Erro de inicialização aborta (ESP_ERROR_CHECK): sem esses recursos o
 * firmware não tem o que fazer.
 */
#ifndef BOARD_INIT_H
#define BOARD_INIT_H

#include <stdbool.h>

/**
 * Instala o driver na UART0 do console e liga o printf a ele: a saída vai
 * para um buffer esvaziado por interrupção, sem espera ocupada na tarefa.
 */
void board_init_console(void);

/** UART2 a 9600 bps 8N1 nos pinos do mapa, com buffer de recepção. */
void board_init_pzem_uart(void);

/** GPIO do LED como saída, apagado. */
void board_init_led(void);

/** Acende/apaga o LED da placa. */
void board_led_set(bool on);

#endif /* BOARD_INIT_H */
