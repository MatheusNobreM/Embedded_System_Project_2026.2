/**
 * @file pzem_modbus.h
 * @brief Montagem e validação de quadros Modbus-RTU do PZEM-004T v3.
 *
 * Código C puro (sem SDK, sem FreeRTOS): testável no host.
 * Aqui só fica o protocolo; o transporte (UART) está em pzem_link.c.
 */
#ifndef PZEM_MODBUS_H
#define PZEM_MODBUS_H

#include <stddef.h>
#include <stdint.h>

#define PZEM_REQ_LEN        8u   /* addr, func, reg_hi, reg_lo, qty_hi, qty_lo, crc_lo, crc_hi */
#define PZEM_RESP_LEN       25u  /* addr, func, nbytes(20), 10 registradores, crc_lo, crc_hi   */
#define PZEM_FUNC_READ_IN   0x04u
#define PZEM_REG_COUNT      10u

/** Medição em inteiros com escala fixa (sem float no caminho crítico). */
typedef struct {
    uint16_t voltage_dv;    /* 0,1 V   */
    uint32_t current_ma;    /* 0,001 A */
    uint32_t power_dw;      /* 0,1 W   */
    uint32_t energy_wh;     /* 1 Wh    */
    uint16_t freq_dhz;      /* 0,1 Hz  */
    uint16_t pf_centi;      /* 0,01    */
    uint16_t alarm;         /* 0xFFFF = alarme ativo */
} pzem_measurement_t;

typedef enum {
    PZEM_PARSE_OK = 0,
    PZEM_PARSE_SHORT,       /* menos bytes que o esperado (timeout/ruído) */
    PZEM_PARSE_BAD_CRC,
    PZEM_PARSE_BAD_HEADER,  /* endereço, função ou contagem inesperados */
    PZEM_PARSE_EXCEPTION    /* PZEM respondeu com exceção Modbus (func | 0x80) */
} pzem_parse_result_t;

/** CRC-16/MODBUS (poly 0xA001 refletido, init 0xFFFF). */
uint16_t pzem_crc16(const uint8_t *data, size_t len);

/** Monta "ler 10 registradores de entrada a partir de 0x0000". */
void pzem_build_read_request(uint8_t addr, uint8_t out[PZEM_REQ_LEN]);

/** Valida e decodifica a resposta. Em erro, *out não é alterado. */
pzem_parse_result_t pzem_parse_response(uint8_t addr, const uint8_t *frame, size_t len,
                                        pzem_measurement_t *out);

#endif /* PZEM_MODBUS_H */
