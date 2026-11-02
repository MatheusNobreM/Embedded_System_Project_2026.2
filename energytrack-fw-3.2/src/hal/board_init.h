/**
 * @file board_init.h
 * @brief Inicialização da Pico W: UART0 do PZEM, chip Wi-Fi e LED da placa.
 *
 * Cada função inicializa UM recurso e não cria tarefas: a posse em tempo de
 * execução (qual tarefa usa o quê) é decidida pela aplicação.
 */
#ifndef BOARD_INIT_H
#define BOARD_INIT_H

#include <stdbool.h>

/** UART0 a 9600 bps 8N1 com FIFO ligada, nos pinos do mapa. */
void board_init_pzem_uart(void);

/**
 * Liga o CYW43 (o LED da placa é um GPIO dele). Chamar de dentro de uma
 * tarefa, com o escalonador rodando. Retorna false se o chip não subir.
 */
bool board_init_cyw43(void);

/** Acende/apaga o LED da placa. Sem efeito se o CYW43 não subiu. */
void board_led_set(bool on);

#endif /* BOARD_INIT_H */
