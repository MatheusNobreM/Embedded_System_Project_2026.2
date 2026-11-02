#include "pzem_modbus.h"

uint16_t pzem_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

void pzem_build_read_request(uint8_t addr, uint8_t out[PZEM_REQ_LEN])
{
    out[0] = addr;
    out[1] = PZEM_FUNC_READ_IN;
    out[2] = 0x00;                    /* registrador inicial 0x0000 */
    out[3] = 0x00;
    out[4] = 0x00;                    /* quantidade = 10 */
    out[5] = PZEM_REG_COUNT;
    uint16_t crc = pzem_crc16(out, 6);
    out[6] = (uint8_t)(crc & 0xFFu);  /* Modbus: CRC com byte baixo primeiro */
    out[7] = (uint8_t)(crc >> 8);
}

/* Registrador i (big-endian) a partir do início dos dados. */
static uint16_t reg_at(const uint8_t *data, unsigned i)
{
    return (uint16_t)((data[2u * i] << 8) | data[2u * i + 1u]);
}

/* Grandezas de 32 bits: palavra baixa no registrador i, alta no i+1. */
static uint32_t reg32_at(const uint8_t *data, unsigned i)
{
    return (uint32_t)reg_at(data, i) | ((uint32_t)reg_at(data, i + 1u) << 16);
}

pzem_parse_result_t pzem_parse_response(uint8_t addr, const uint8_t *frame, size_t len,
                                        pzem_measurement_t *out)
{
    /* Resposta de exceção tem 5 bytes: addr, func|0x80, código, crc_lo, crc_hi */
    if (len >= 5u && frame[0] == addr && frame[1] == (PZEM_FUNC_READ_IN | 0x80u)) {
        uint16_t crc = pzem_crc16(frame, 3);
        if (frame[3] == (uint8_t)(crc & 0xFFu) && frame[4] == (uint8_t)(crc >> 8)) {
            return PZEM_PARSE_EXCEPTION;
        }
        return PZEM_PARSE_BAD_CRC;
    }
    if (len < PZEM_RESP_LEN) {
        return PZEM_PARSE_SHORT;
    }

    uint16_t crc = pzem_crc16(frame, PZEM_RESP_LEN - 2u);
    if (frame[PZEM_RESP_LEN - 2u] != (uint8_t)(crc & 0xFFu) ||
        frame[PZEM_RESP_LEN - 1u] != (uint8_t)(crc >> 8)) {
        return PZEM_PARSE_BAD_CRC;
    }
    if (frame[0] != addr || frame[1] != PZEM_FUNC_READ_IN || frame[2] != 2u * PZEM_REG_COUNT) {
        return PZEM_PARSE_BAD_HEADER;
    }

    const uint8_t *d = &frame[3];
    out->voltage_dv = reg_at(d, 0);
    out->current_ma = reg32_at(d, 1);
    out->power_dw   = reg32_at(d, 3);
    out->energy_wh  = reg32_at(d, 5);
    out->freq_dhz   = reg_at(d, 7);
    out->pf_centi   = reg_at(d, 8);
    out->alarm      = reg_at(d, 9);
    return PZEM_PARSE_OK;
}
