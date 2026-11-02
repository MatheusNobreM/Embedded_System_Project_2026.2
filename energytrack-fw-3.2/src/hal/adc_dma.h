/**
 * @file adc_dma.h
 * @brief Aquisição contínua do sensor de temperatura interno por ADC + DMA.
 *
 * O ADC roda em modo livre, ritmado pelo próprio divisor de clock (1 kHz), e
 * dois canais de DMA encadeados enchem dois buffers em ping-pong, sem CPU.
 * A cada bloco completo a IRQ do DMA chama o callback da aplicação (contexto
 * de interrupção) com o índice do buffer pronto. Esse buffer fica estável
 * até o outro encher: a tarefa consumidora tem um bloco inteiro (100 ms)
 * de prazo — é o deadline da tarefa adc.
 */
#ifndef ADC_DMA_H
#define ADC_DMA_H

#include <stdbool.h>
#include <stdint.h>

#define ADC_DMA_SAMPLE_HZ      1000u
#define ADC_DMA_BLOCK_SAMPLES  100u    /* 100 amostras = 100 ms por bloco */

/** Chamado na IRQ do DMA: buffer_index (0/1) acabou de encher no instante t_us. */
typedef void (*adc_dma_block_cb)(uint32_t buffer_index, uint64_t t_us);

/** Configura ADC4, os dois canais de DMA e a IRQ, e dispara. false = sem canal de DMA livre. */
bool adc_dma_start(adc_dma_block_cb on_block);

/** Buffer de ADC_DMA_BLOCK_SAMPLES amostras de 12 bits. */
const uint16_t *adc_dma_buffer(uint32_t buffer_index);

#endif /* ADC_DMA_H */
