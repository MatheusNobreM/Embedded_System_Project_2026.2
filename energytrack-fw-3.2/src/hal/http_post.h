/**
 * @file http_post.h
 * @brief POST de um corpo JSON para a URL do backend (cliente HTTP do ESP-IDF).
 *
 * Um único cliente, reaproveitado a cada envio: se o backend aceita,
 * a conexão TCP continua aberta entre um POST e outro. A chamada bloqueia a
 * tarefa (no FreeRTOS, sem espera ocupada) por no máximo HTTP_POST_TIMEOUT_MS
 * em cada etapa (conectar, enviar, receber). Use só da tarefa net (núcleo 0).
 *
 * https:// funciona com o pacote de certificados raiz do ESP-IDF (CAs públicas).
 */
#ifndef HTTP_POST_H
#define HTTP_POST_H

#include <stdbool.h>

#define HTTP_POST_TIMEOUT_MS 3000

/** Cria o cliente para a URL (já conferida por telemetry_check_config). */
bool http_post_init(const char *url);

/**
 * Envia body (len bytes) com Content-Type: application/json.
 * @param status recebe o código HTTP da resposta (0 sem resposta)
 * @return true se houve resposta HTTP (qualquer código); false se a conexão
 *         falhou ou o tempo esgotou.
 */
bool http_post_json(const char *body, int len, int *status);

#endif /* HTTP_POST_H */
