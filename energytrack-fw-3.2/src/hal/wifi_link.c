#include "wifi_link.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define BIT_GOT_IP  BIT0
#define IP_TEXT_LEN 16u

static EventGroupHandle_t s_events;
static char s_ip[IP_TEXT_LEN];                       /* escrito pelo laço de eventos, lido pela net */
static portMUX_TYPE s_ip_lock = portMUX_INITIALIZER_UNLOCKED;

static void set_ip(const char *ip)
{
    char copy[IP_TEXT_LEN];
    snprintf(copy, sizeof copy, "%s", ip);           /* formata fora da seção crítica */
    taskENTER_CRITICAL(&s_ip_lock);
    memcpy(s_ip, copy, sizeof s_ip);
    taskEXIT_CRITICAL(&s_ip_lock);
}

/* Roda na tarefa do laço de eventos padrão do ESP-IDF (núcleo 0). */
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, BIT_GOT_IP);
        set_ip("");
        esp_wifi_connect();   /* tenta de novo; sem a rede, o driver varre os canais (~2 s) e avisa outra vez */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        char ip[IP_TEXT_LEN];
        snprintf(ip, sizeof ip, IPSTR, IP2STR(&ev->ip_info.ip));
        set_ip(ip);
        xEventGroupSetBits(s_events, BIT_GOT_IP);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        xEventGroupClearBits(s_events, BIT_GOT_IP);
        set_ip("");
    }
}

static size_t min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

bool wifi_link_start(const char *ssid, const char *password)
{
    s_events = xEventGroupCreate();
    if (!s_events) return false;

    /* O driver do Wi-Fi fala bastante no nível INFO; o estado da rede sai na linha "rede:" do relatório. */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);

    if (esp_netif_init() != ESP_OK) return false;
    const esp_err_t loop = esp_event_loop_create_default();   /* tarefa do laço presa no núcleo 0 */
    if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE) return false;
    if (!esp_netif_create_default_wifi_sta()) return false;

    /* nvs_enable vem de CONFIG_ESP_WIFI_NVS_ENABLED, desligado no sdkconfig.defaults. */
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) return false;
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) return false;

    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL) != ESP_OK) return false;
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL) != ESP_OK) return false;
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_event, NULL, NULL) != ESP_OK) return false;

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.sta.ssid, ssid, min_size(strlen(ssid), sizeof cfg.sta.ssid));
    memcpy(cfg.sta.password, password, min_size(strlen(password), sizeof cfg.sta.password - 1u));
    cfg.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) return false;
    return esp_wifi_start() == ESP_OK;   /* WIFI_EVENT_STA_START dispara a primeira conexão */
}

bool wifi_link_is_up(void)
{
    return s_events && (xEventGroupGetBits(s_events) & BIT_GOT_IP) != 0;
}

void wifi_link_ip(char *out, size_t len)
{
    if (len == 0u) return;
    char copy[IP_TEXT_LEN];
    taskENTER_CRITICAL(&s_ip_lock);
    memcpy(copy, s_ip, sizeof copy);
    taskEXIT_CRITICAL(&s_ip_lock);
    snprintf(out, len, "%s", copy);
}

void wifi_link_device_id(char *out, size_t len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "et-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
