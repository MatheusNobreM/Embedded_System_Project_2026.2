/**
 * Teste de host da conversão do sensor de temperatura interno (ADC4).
 * Referência: fórmula do datasheet do RP2040 calculada em double.
 */
#include <stdlib.h>
#include "adc_dma.h"
#include "temp_sensor.h"
#include "tap.h"

/* T = 27 - (V - 0,706) / 0,001721, V = media * 3,3 / 4096 — em m°C. */
static double reference_mc(double mean_raw)
{
    const double v = mean_raw * 3.3 / 4096.0;
    return (27.0 - (v - 0.706) / 0.001721) * 1000.0;
}

static uint16_t s_block[ADC_DMA_BLOCK_SAMPLES];

static void fill(uint16_t value)
{
    for (unsigned i = 0; i < ADC_DMA_BLOCK_SAMPLES; ++i) s_block[i] = value;
}

static void test_reference_points(void)
{
    fill(876);
    int32_t t27 = temp_sensor_block_to_mc(s_block, ADC_DMA_BLOCK_SAMPLES);
    TAP_CHECK(abs(t27 - 27100) <= 300, "leitura 876 -> %d m°C (esperado ~27100 ± 300)", (int)t27);

    fill(784);
    int32_t t70 = temp_sensor_block_to_mc(s_block, ADC_DMA_BLOCK_SAMPLES);
    TAP_CHECK(abs(t70 - 70200) <= 300, "leitura 784 -> %d m°C (esperado ~70200 ± 300)", (int)t70);

    TAP_CHECK(t70 > t27, "mais quente deve dar leitura menor (coeficiente negativo)");
}

static void test_matches_datasheet_formula(void)
{
    for (uint16_t raw = 700; raw <= 950; raw += 25) {
        fill(raw);
        int32_t got = temp_sensor_block_to_mc(s_block, ADC_DMA_BLOCK_SAMPLES);
        double want = reference_mc(raw);
        TAP_CHECK(abs(got - (int32_t)want) <= 20, "leitura %u -> %d m°C, fórmula dá %.0f",
                  (unsigned)raw, (int)got, want);
    }
}

static void test_block_mean_keeps_fraction(void)
{
    /* Metade 876, metade 877: média 876,5. Média inteira daria 876 (~0,23 °C a mais). */
    for (unsigned i = 0; i < ADC_DMA_BLOCK_SAMPLES; ++i) s_block[i] = (uint16_t)(876u + (i & 1u));
    int32_t got = temp_sensor_block_to_mc(s_block, ADC_DMA_BLOCK_SAMPLES);
    double want = reference_mc(876.5);
    TAP_CHECK(abs(got - (int32_t)want) <= 20, "bloco 876/877 -> %d m°C, esperado %.0f", (int)got, want);
}

static void test_invalid_inputs(void)
{
    TAP_CHECK(temp_sensor_block_to_mc(s_block, 0) == TEMP_SENSOR_INVALID_MC, "bloco vazio deve ser inválido");
    TAP_CHECK(temp_sensor_block_to_mc(NULL, 10) == TEMP_SENSOR_INVALID_MC, "ponteiro nulo deve ser inválido");
    fill(0xF36C);   /* lixo nos 4 bits altos + 876 nos 12 bits */
    int32_t got = temp_sensor_block_to_mc(s_block, ADC_DMA_BLOCK_SAMPLES);
    TAP_CHECK(abs(got - 27100) <= 300, "bits altos devem ser ignorados (%d m°C)", (int)got);
}

int main(void)
{
    tap_run("AC-003 pontos de referência: 876 ~ 27,1 °C e 784 ~ 70,2 °C", test_reference_points);
    tap_run("AC-003 conversão segue a fórmula do datasheet em toda a faixa", test_matches_datasheet_formula);
    tap_run("AC-003 média do bloco preserva a fração do LSB", test_block_mean_keeps_fraction);
    tap_run("bloco vazio, nulo ou com bits altos", test_invalid_inputs);
    return tap_done();
}
