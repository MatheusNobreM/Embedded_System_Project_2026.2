#include "pzem_link.h"

#include <stddef.h>
#include "board_resources.h"

#define POLL_STEP_MS 10u

static void drain_rx(void)
{
    while (uart_is_readable(BOARD_PZEM_UART)) {
        (void)uart_getc(BOARD_PZEM_UART);   /* lixo de quadros anteriores ou ruído */
    }
}

pzem_parse_result_t pzem_link_read(pzem_sleep_fn sleep_ms, pzem_measurement_t *out)
{
    uint8_t req[PZEM_REQ_LEN];
    uint8_t resp[PZEM_RESP_LEN];
    size_t got = 0;

    pzem_build_read_request(BOARD_PZEM_MODBUS_ADDR, req);
    drain_rx();
    uart_write_blocking(BOARD_PZEM_UART, req, sizeof req);   /* 8 bytes cabem na FIFO TX */

    /* A resposta (25 bytes ≈ 26 ms a 9600 bps) cabe na FIFO RX de 32 bytes:
     * a tarefa dorme e lê em lotes, sem espera ocupada e sem IRQ de UART. */
    for (uint32_t waited = 0; waited <= PZEM_LINK_MAX_WAIT_MS && got < PZEM_RESP_LEN;
         waited += POLL_STEP_MS) {
        sleep_ms(POLL_STEP_MS);
        while (got < PZEM_RESP_LEN && uart_is_readable(BOARD_PZEM_UART)) {
            resp[got++] = (uint8_t)uart_getc(BOARD_PZEM_UART);
        }
        /* Resposta de exceção Modbus tem só 5 bytes. */
        if (got == 5u && resp[1] == (PZEM_FUNC_READ_IN | 0x80u)) break;
    }
    return pzem_parse_response(BOARD_PZEM_MODBUS_ADDR, resp, got, out);
}
