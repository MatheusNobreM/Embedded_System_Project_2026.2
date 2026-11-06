/**
 * @file adc_dma.h
 * @brief Aquisição contínua do LM35 pelo ADC1 do ESP32, com DMA.
 *
 * O controlador digital do ADC converte o canal do LM35 a 20 kHz (o mínimo
 * do ESP32) e o DMA copia as conversões para a RAM, sem CPU, em quadros de
 * 10 ms. A cada quadro, a ISR do driver reduz cada 20 conversões a uma
 * amostra de 1 kHz (adc_frame.h) e enche dois buffers de 100 amostras em
 * ping-pong. A cada bloco completo chama o callback da aplicação (contexto
 * de interrupção) com o índice do buffer pronto. Esse buffer fica estável
 * até o outro encher: a tarefa consumidora tem um bloco inteiro (100 ms) de
 * prazo — é o deadline da tarefa adc.
 *
 * Este header não depende do ESP-IDF (o teste de host usa as constantes).
 */
#ifndef ADC_DMA_H
#define ADC_DMA_H

#include <stdbool.h>
#include <stdint.h>

#include "adc_frame.h"

#define ADC_DMA_SAMPLE_HZ      1000u
#define ADC_DMA_BLOCK_SAMPLES  100u    /* 100 amostras = 100 ms por bloco */
#define ADC_DMA_CONV_HZ        (ADC_DMA_SAMPLE_HZ * ADC_FRAME_DECIMATION)   /* 20 kHz */

/** Chamado na ISR: buffer_index (0/1) acabou de encher no instante t_us. true = acordou tarefa de prioridade maior. */
typedef bool (*adc_dma_block_cb)(uint32_t buffer_index, uint64_t t_us);

/** Configura o ADC1 no canal do LM35, a calibração e o DMA, e dispara. false = falhou. */
bool adc_dma_start(adc_dma_block_cb on_block);

/** Buffer de ADC_DMA_BLOCK_SAMPLES leituras brutas de 12 bits (já reduzidas a 1 kHz). */
const uint16_t *adc_dma_buffer(uint32_t buffer_index);

/**
 * Converte o bloco em mV pela calibração de fábrica do ADC (eFuse, line
 * fitting). Chamar da tarefa, não da ISR. false = calibração indisponível.
 */
bool adc_dma_block_to_mv(uint32_t buffer_index, uint16_t mv_out[ADC_DMA_BLOCK_SAMPLES]);

/** Amostras reduzidas que se perderam (não deveria passar de 0). */
uint32_t adc_dma_dropped(void);

#endif /* ADC_DMA_H */
