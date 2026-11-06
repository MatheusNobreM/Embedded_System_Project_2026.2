/**
 * @file adc_frame.h
 * @brief Decodificação e redução dos quadros do ADC contínuo do ESP32.
 *
 * C puro (sem ESP-IDF): testável no host.
 *
 * Formato TYPE1 do ESP32: cada conversão ocupa 2 bytes little-endian, com o
 * canal nos bits 15..12 e a leitura de 12 bits nos bits 11..0. O controlador
 * digital do ADC do ESP32 não amostra abaixo de 20 kHz, então a média de
 * ADC_FRAME_DECIMATION conversões do canal escolhido vira uma amostra de
 * 1 kHz. Um grupo que começa num quadro termina no seguinte (a soma parcial
 * fica guardada no redutor).
 */
#ifndef ADC_FRAME_H
#define ADC_FRAME_H

#include <stdint.h>

#define ADC_FRAME_BYTES_PER_CONV  2u
#define ADC_FRAME_DECIMATION      20u     /* 20 kHz -> 1 kHz */
#define ADC_FRAME_CHANNEL_SHIFT   12u
#define ADC_FRAME_DATA_MASK       0x0FFFu

typedef struct {
    uint8_t  channel;    /* canal que interessa; os outros são descartados */
    uint32_t sum;        /* soma do grupo em formação */
    uint32_t count;      /* conversões no grupo em formação */
    uint32_t dropped;    /* amostras prontas que não couberam na saída */
} adc_decimator_t;

void adc_decimator_init(adc_decimator_t *d, uint8_t channel);

/**
 * Consome len bytes de um quadro e escreve em out as amostras completas
 * (média arredondada de ADC_FRAME_DECIMATION conversões do canal), até
 * max_out. Retorna quantas escreveu. Byte final avulso (meia conversão) é
 * ignorado; amostras além de max_out são contadas em d->dropped.
 */
uint32_t adc_decimator_feed(adc_decimator_t *d, const uint8_t *frame, uint32_t len,
                            uint16_t *out, uint32_t max_out);

#endif /* ADC_FRAME_H */
