/**
 * Teste de host da aquisição do LM35: quadro do ADC contínuo do ESP32
 * (formato TYPE1), redução 20 kHz -> 1 kHz e conversão mV -> m°C.
 * Referência: escala do LM35, 10 mV/°C, calculada em double.
 */
#include <stdlib.h>
#include "adc_dma.h"
#include "adc_frame.h"
#include "temp_sensor.h"
#include "tap.h"

#define LM35_CH  6u
#define OTHER_CH 3u

static uint16_t s_block[ADC_DMA_BLOCK_SAMPLES];
static uint8_t s_frame[2u * 400u];

static double reference_mc(double mean_mv)
{
    return mean_mv / 10.0 * 1000.0;
}

static void fill(uint16_t mv)
{
    for (unsigned i = 0; i < ADC_DMA_BLOCK_SAMPLES; ++i) s_block[i] = mv;
}

/* Grava uma conversão TYPE1 (canal nos 4 bits altos, leitura nos 12 baixos). */
static uint32_t put_conv(uint8_t *frame, uint32_t pos, uint8_t channel, uint16_t raw)
{
    const uint16_t word = (uint16_t)((channel << ADC_FRAME_CHANNEL_SHIFT) | (raw & ADC_FRAME_DATA_MASK));
    frame[pos] = (uint8_t)(word & 0xFFu);
    frame[pos + 1u] = (uint8_t)(word >> 8);
    return pos + ADC_FRAME_BYTES_PER_CONV;
}

static void test_reference_points(void)
{
    fill(250);
    const int32_t t25 = temp_sensor_block_to_mc(s_block, ADC_DMA_BLOCK_SAMPLES);
    TAP_CHECK(abs(t25 - 25000) <= 100, "250 mV -> %d m°C (esperado 25000 ± 100)", (int)t25);

    fill(700);
    const int32_t t70 = temp_sensor_block_to_mc(s_block, ADC_DMA_BLOCK_SAMPLES);
    TAP_CHECK(abs(t70 - 70000) <= 100, "700 mV -> %d m°C (esperado 70000 ± 100)", (int)t70);

    TAP_CHECK(t70 > t25, "mais tensão deve dar mais temperatura (coeficiente positivo)");
}

static void test_matches_lm35_scale(void)
{
    for (uint16_t mv = 20; mv <= 1500; mv += 37) {
        fill(mv);
        const int32_t got = temp_sensor_block_to_mc(s_block, ADC_DMA_BLOCK_SAMPLES);
        const double want = reference_mc(mv);
        TAP_CHECK(abs(got - (int32_t)want) <= 1, "%u mV -> %d m°C, a escala dá %.0f", (unsigned)mv, (int)got, want);
    }
}

static void test_block_mean_keeps_fraction(void)
{
    /* Metade 250 mV, metade 251 mV: média 250,5 mV. Média inteira daria 250 (0,05 °C a menos). */
    for (unsigned i = 0; i < ADC_DMA_BLOCK_SAMPLES; ++i) s_block[i] = (uint16_t)(250u + (i & 1u));
    const int32_t got = temp_sensor_block_to_mc(s_block, ADC_DMA_BLOCK_SAMPLES);
    const double want = reference_mc(250.5);
    TAP_CHECK(abs(got - (int32_t)want) <= 1, "bloco 250/251 mV -> %d m°C, esperado %.0f", (int)got, want);
}

static void test_frame_keeps_only_lm35_channel(void)
{
    /* 40 conversões do LM35 (raw 1000) intercaladas com 40 de outro canal (raw 4000). */
    uint32_t pos = 0;
    for (unsigned i = 0; i < 40u; ++i) {
        pos = put_conv(s_frame, pos, LM35_CH, 1000);
        pos = put_conv(s_frame, pos, OTHER_CH, 4000);
    }
    adc_decimator_t d;
    adc_decimator_init(&d, LM35_CH);
    uint16_t out[8] = {0};
    const uint32_t n = adc_decimator_feed(&d, s_frame, pos, out, 8);
    TAP_CHECK(n == 2u, "40 conversões do canal deveriam dar 2 amostras, deram %u", (unsigned)n);
    TAP_CHECK(out[0] == 1000u && out[1] == 1000u, "amostras %u/%u: o outro canal vazou na média",
              (unsigned)out[0], (unsigned)out[1]);
    TAP_CHECK(d.count == 0u && d.dropped == 0u, "não deveria sobrar grupo nem perder amostra");
}

static void test_frame_decimation_20_to_1(void)
{
    /* 20 conversões de 1000 a 1019: média 1009,5 -> arredonda para 1010. */
    uint32_t pos = 0;
    for (uint16_t i = 0; i < ADC_FRAME_DECIMATION; ++i) pos = put_conv(s_frame, pos, LM35_CH, (uint16_t)(1000u + i));
    adc_decimator_t d;
    adc_decimator_init(&d, LM35_CH);
    uint16_t out[2] = {0};
    TAP_CHECK(adc_decimator_feed(&d, s_frame, pos, out, 2) == 1u, "20 conversões deveriam dar 1 amostra");
    TAP_CHECK(out[0] == 1010u, "média arredondada de 1000..1019 = 1010, deu %u", (unsigned)out[0]);

    /* Grupo partido entre dois quadros: 30 conversões de 100, depois 10 de 200. */
    adc_decimator_init(&d, LM35_CH);
    pos = 0;
    for (unsigned i = 0; i < 30u; ++i) pos = put_conv(s_frame, pos, LM35_CH, 100);
    TAP_CHECK(adc_decimator_feed(&d, s_frame, pos, out, 2) == 1u && out[0] == 100u, "primeiro quadro: 1 amostra de 100");
    TAP_CHECK(d.count == 10u, "10 conversões deveriam ficar guardadas, ficaram %u", (unsigned)d.count);
    pos = 0;
    for (unsigned i = 0; i < 10u; ++i) pos = put_conv(s_frame, pos, LM35_CH, 200);
    TAP_CHECK(adc_decimator_feed(&d, s_frame, pos, out, 2) == 1u && out[0] == 150u,
              "grupo completado no quadro seguinte deveria dar 150, deu %u", (unsigned)out[0]);
}

static void test_one_block_per_100ms_at_20khz(void)
{
    /* 100 ms a 20 kHz = 2000 conversões em 10 quadros de 10 ms -> exatamente 1 bloco. */
    adc_decimator_t d;
    adc_decimator_init(&d, LM35_CH);
    uint32_t samples = 0;
    for (unsigned f = 0; f < 10u; ++f) {
        uint32_t pos = 0;
        for (unsigned i = 0; i < ADC_DMA_CONV_HZ / 100u; ++i) pos = put_conv(s_frame, pos, LM35_CH, 870);
        uint16_t out[16];
        samples += adc_decimator_feed(&d, s_frame, pos, out, 16);
    }
    TAP_CHECK(ADC_DMA_CONV_HZ == 20000u, "conversão a %u Hz; o mínimo do ESP32 é 20 kHz", (unsigned)ADC_DMA_CONV_HZ);
    TAP_CHECK(samples == ADC_DMA_BLOCK_SAMPLES, "100 ms deveriam dar %u amostras, deram %u",
              (unsigned)ADC_DMA_BLOCK_SAMPLES, (unsigned)samples);
}

static void test_invalid_inputs(void)
{
    TAP_CHECK(temp_sensor_block_to_mc(s_block, 0) == TEMP_SENSOR_INVALID_MC, "bloco vazio deve ser inválido");
    TAP_CHECK(temp_sensor_block_to_mc(NULL, 10) == TEMP_SENSOR_INVALID_MC, "ponteiro nulo deve ser inválido");

    adc_decimator_t d;
    adc_decimator_init(&d, LM35_CH);
    TAP_CHECK(adc_decimator_feed(&d, NULL, 10, NULL, 0) == 0u, "quadro nulo não gera amostra");

    /* 41 bytes = 20 conversões + 1 byte avulso: o byte avulso é ignorado. */
    uint32_t pos = 0;
    for (unsigned i = 0; i < ADC_FRAME_DECIMATION; ++i) pos = put_conv(s_frame, pos, LM35_CH, 500);
    s_frame[pos] = 0xFF;
    uint16_t out[2];
    TAP_CHECK(adc_decimator_feed(&d, s_frame, pos + 1u, out, 2) == 1u && out[0] == 500u, "byte avulso no fim");

    /* Saída cheia: a amostra que não cabe é contada como perdida. */
    adc_decimator_init(&d, LM35_CH);
    pos = 0;
    for (unsigned i = 0; i < 2u * ADC_FRAME_DECIMATION; ++i) pos = put_conv(s_frame, pos, LM35_CH, 500);
    TAP_CHECK(adc_decimator_feed(&d, s_frame, pos, out, 1) == 1u && d.dropped == 1u, "amostra sem espaço deve contar como perdida");
}

int main(void)
{
    tap_run("AC-003 pontos de referência: 250 mV = 25,0 °C e 700 mV = 70,0 °C", test_reference_points);
    tap_run("AC-003 conversão segue 10 mV/°C em toda a faixa do LM35", test_matches_lm35_scale);
    tap_run("AC-003 média do bloco preserva a fração do mV", test_block_mean_keeps_fraction);
    tap_run("AC-003 quadro TYPE1: só as conversões do canal do LM35 entram", test_frame_keeps_only_lm35_channel);
    tap_run("AC-003 média de 20 conversões por amostra, inclusive entre quadros", test_frame_decimation_20_to_1);
    tap_run("ADC a 20 kHz reduzido a 1 kHz enche um bloco a cada 100 ms", test_one_block_per_100ms_at_20khz);
    tap_run("bloco vazio, nulo, byte avulso e saída cheia", test_invalid_inputs);
    return tap_done();
}
