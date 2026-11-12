#include "http_post.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

static esp_http_client_handle_t s_client;

bool http_post_init(const char *url)
{
    const esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_POST_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* só é usado em https:// */
    };
    s_client = esp_http_client_init(&cfg);
    if (!s_client) return false;
    return esp_http_client_set_header(s_client, "Content-Type", "application/json") == ESP_OK;
}

bool http_post_json(const char *body, int len, int *status)
{
    *status = 0;
    if (!s_client) return false;
    if (esp_http_client_set_post_field(s_client, body, len) != ESP_OK) return false;
    if (esp_http_client_perform(s_client) != ESP_OK) {
        esp_http_client_close(s_client);   /* conexão quebrada: o próximo envio abre outra */
        return false;
    }
    *status = esp_http_client_get_status_code(s_client);
    return true;
}
