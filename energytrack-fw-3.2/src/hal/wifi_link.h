/**
 * @file wifi_link.h
 * @brief Wi-Fi do ESP32 em modo estação: conecta, reconecta sozinho e diz se há IP.
 *
 * Chame wifi_link_start() de uma tarefa presa no NÚCLEO 0 (a tarefa net): o
 * driver aloca a interrupção do rádio no núcleo de quem o inicia, e o núcleo
 * 1 fica só com aquisição e processamento.
 *
 * Nada aqui grava na flash: a configuração fica na RAM (WIFI_STORAGE_RAM) e,
 * pelo sdkconfig.defaults, o Wi-Fi não usa a NVS nem guarda a calibração do
 * rádio. Gravar na flash pausaria os dois núcleos.
 */
#ifndef WIFI_LINK_H
#define WIFI_LINK_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Liga a pilha de rede e o Wi-Fi e começa a conectar (não espera o IP).
 * Depois disso o driver reconecta sozinho quando a rede cai.
 * ssid (até 32) e password (vazia ou 8 a 63) já conferidos por telemetry_check_config.
 * @return false se algum passo da inicialização falhou (a rede fica desligada).
 */
bool wifi_link_start(const char *ssid, const char *password);

/** Verdadeiro enquanto a estação tem IP. */
bool wifi_link_is_up(void);

/** IP atual ("192.168.0.23") ou "" sem conexão. */
void wifi_link_ip(char *out, size_t len);

/** Identificador da placa para o backend: "et-" + MAC do Wi-Fi (12 dígitos hex). */
void wifi_link_device_id(char *out, size_t len);

#endif /* WIFI_LINK_H */
