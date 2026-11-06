#include "pzem_link.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"

#include "board_resources.h"

#define POLL_STEP_MS 10u

pzem_parse_result_t pzem_link_read(pzem_measurement_t *out)
{
    uint8_t req[PZEM_REQ_LEN];
    uint8_t resp[PZEM_RESP_LEN];
    size_t got = 0;

    pzem_build_read_request(BOARD_PZEM_MODBUS_ADDR, req);
    (void)uart_flush_input(BOARD_PZEM_UART);                    /* lixo de quadros anteriores ou ruído */
    (void)uart_write_bytes(BOARD_PZEM_UART, req, sizeof req);   /* 8 bytes cabem na FIFO TX */

    /* A resposta (25 bytes ≈ 26 ms a 9600 bps) cai no buffer do driver pela
     * IRQ da UART. uart_read_bytes bloqueia a tarefa no FreeRTOS até chegar o
     * que falta ou passar o passo; os passos curtos deixam reconhecer a
     * resposta de exceção Modbus (5 bytes) sem esperar o tempo todo. */
    for (uint32_t waited = 0; waited <= PZEM_LINK_MAX_WAIT_MS && got < PZEM_RESP_LEN;
         waited += POLL_STEP_MS) {
        const int n = uart_read_bytes(BOARD_PZEM_UART, resp + got, (uint32_t)(PZEM_RESP_LEN - got),
                                      pdMS_TO_TICKS(POLL_STEP_MS));
        if (n > 0) got += (size_t)n;
        if (got == 5u && resp[1] == (PZEM_FUNC_READ_IN | 0x80u)) break;
    }
    return pzem_parse_response(BOARD_PZEM_MODBUS_ADDR, resp, got, out);
}
