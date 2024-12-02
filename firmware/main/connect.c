#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include <wifi_provisioning/manager.h>

//#ifdef CONFIG_PROVISION_TRANSPORT_BLE
#include <wifi_provisioning/scheme_ble.h>
//#endif /* CONFIG_PROVISION_TRANSPORT_BLE */

#include "qrcode.h"
#include <string.h>

#include "config.h"

static const char *TAG = "connect";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define PROV_QR_VERSION         "v1"
#define PROV_TRANSPORT_BLE      "ble"
#define QRCODE_BASE_URL         "https://espressif.github.io/esp-jumpstart/qrcode.html"
esp_err_t print_ip_info(){
    esp_netif_t *netif = esp_netif_get_default_netif();
    esp_netif_ip_info_t ip_info = {0};
    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(netif, &ip_info), TAG, "get ip_info failed");
    printf("ip     : "IPSTR"\n", IP2STR(&ip_info.ip));
    printf("netmask: "IPSTR"\n", IP2STR(&ip_info.netmask));
    printf("gateway: "IPSTR"\n", IP2STR(&ip_info.gw));
    return ESP_OK;
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
#ifdef CONFIG_RESET_PROV_MGR_ON_FAILURE
    static int s_prov_retries;
#endif
    if(event_base == WIFI_EVENT) {

        switch(event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (s_retry_num < CONFIG_MAXIMUM_RETRY) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "retry connecting to the AP");
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
                ESP_LOGI(TAG,"connect to the AP fail");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if(event_base == WIFI_PROV_EVENT ) {

           switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
#ifdef CONFIG_RESET_PROV_MGR_ON_FAILURE
                s_prov_retries++;
                if (s_prov_retries >= CONFIG_PROV_MGR_MAX_RETRY_CNT) {
                    ESP_LOGI(TAG, "Failed to connect with provisioned AP, resetting provisioned credentials");
                    wifi_prov_mgr_reset_sm_state_on_failure();
                    s_prov_retries = 0;
                }
#endif
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
#ifdef CONFIG_RESET_PROV_MGR_ON_FAILURE
                s_prov_retries = 0;
#endif
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
        
#ifdef CONFIG_PROVISION_TRANSPORT_BLE
    } else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {

        switch (event_id) {
            case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
                ESP_LOGI(TAG, "BLE transport: Connected!");
                break;
            case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
                ESP_LOGI(TAG, "BLE transport: Disconnected!");
                break;
            default:
                break;
        }
#endif
    } else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        // Although security level=0, in case of panic, we add this event handler.
        switch (event_id) {
            case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
                ESP_LOGI(TAG, "Secured session established!");
                break;
            case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
                ESP_LOGE(TAG, "Received invalid security parameters for establishing secure session!");
                break;
            case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
                ESP_LOGE(TAG, "Received incorrect username and/or PoP for establishing secure session!");
                break;
            default:
                break;
        }
    } 
}

esp_err_t wifi_sta_do_connect() {
    wifi_config_t wifi_config = {
        .sta = {
            //.ssid = CONFIG_WIFI_SSID, 
            //.password = CONFIG_WIFI_PASSWORD, 
            //.threshold.rssi = -127, 
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, 
        },
    };

    // get config ssid and password
    size_t ssid_len = 32;
    size_t password_len = 64;
    ESP_RETURN_ON_ERROR(config_get_str_with_default("wifi_ssid", (char *)wifi_config.sta.ssid, &ssid_len, CONFIG_WIFI_SSID), TAG, "get_config for wifi_ssid failed");
    ESP_RETURN_ON_ERROR(config_get_str_with_default("wifi_password", (char *)wifi_config.sta.password, &password_len, CONFIG_WIFI_PASSWORD), TAG, "get_config for wifi_ssid failed");


    // set station mode
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    // set wifi config
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");

    // starting esp_wifi
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");


    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", wifi_config.sta.ssid);
        //ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
        //         wifi_config.sta.ssid, wifi_config.sta.password);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", wifi_config.sta.ssid);
        //ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
        //         wifi_config.sta.ssid, wifi_config.sta.password);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    return ESP_OK;
}

esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                          uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf) {
        ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}

esp_err_t wifi_start() {
    s_wifi_event_group = xEventGroupCreate();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    esp_netif_create_default_wifi_sta();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                &event_handler,
                NULL,
                &instance_any_id),
            TAG, "register WIFI_EVENT:ANY_ID event failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                IP_EVENT,
                IP_EVENT_STA_GOT_IP,
                &event_handler,
                NULL,
                &instance_got_ip),
            TAG, "register IP_EVENT:STA_GOT_IP event failed");

    return ESP_OK;
}

static void wifi_init_sta(void)
{
    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void wifi_prov_print_qr(const char *name, const char *username, const char *transport)
{
    if (!name || !transport) {
        ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
        return;
    }
    char payload[150] = {0};
 
    snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\"" \
                ",\"transport\":\"%s\"}",
                PROV_QR_VERSION, name, transport);
    
#ifdef CONFIG_PROV_SHOW_QR
    ESP_LOGI(TAG, "Scan this QR code from the provisioning application for Provisioning.");
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, payload);
#endif /* CONFIG_APP_WIFI_PROV_SHOW_QR */
    ESP_LOGI(TAG, "If QR code is not visible, copy paste the below URL in a browser.\n%s?data=%s", QRCODE_BASE_URL, payload);
}



// =========
static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "PROV_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}
esp_err_t wifi_provisioning() {
    s_wifi_event_group = xEventGroupCreate();
    esp_event_handler_instance_t instance_wifi_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_t instance_prov_tans_ble;
    esp_event_handler_instance_t instance_prov_any_id;
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                &event_handler,
                NULL,
                &instance_wifi_any_id),
            TAG, "register WIFI_EVENT:ANY_ID event failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        WIFI_PROV_EVENT,
         ESP_EVENT_ANY_ID,
          &event_handler,
           NULL, 
           &instance_prov_any_id),
        TAG, "register WIFI_PROV_EVENT:ANY_ID event failed");

    #ifdef CONFIG_PROVISION_TRANSPORT_BLE
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        PROTOCOMM_TRANSPORT_BLE_EVENT,
        ESP_EVENT_ANY_ID,
        &event_handler,
        NULL,
        &instance_prov_tans_ble),
        TAG, "register PROTOCOMM_TRANSPORT_BLE_EVENT:ANY_ID event failed");
    #endif
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &event_handler,
        NULL,
        &instance_got_ip),
        TAG, "register IP_EVENT:STA_GOT_IP event failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    


    wifi_prov_mgr_config_t prov_cfg = {
        /* What is the Provisioning Scheme that we want ?
         * wifi_prov_scheme_softap or wifi_prov_scheme_ble */
#ifdef CONFIG_PROVISION_TRANSPORT_BLE
        .scheme = wifi_prov_scheme_ble,
#endif /* CONFIG_PROVISION_TRANSPORT_BLE */
    };


    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_cfg));

    bool provisioned = false;
#ifdef CONFIG_RESET_PROVISIONED
    wifi_prov_mgr_reset_provisioning();
#else
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
#endif

    // start provisioning if device is not yet provisioned
    if(!provisioned) {
        ESP_LOGI(TAG, "Starting provisioning");

        char service_name[12];
        get_device_service_name(service_name, sizeof(service_name));

    

        //todo: sercurity level. for now, we use security level 0.
        wifi_prov_security_t security = WIFI_PROV_SECURITY_0;
        
        // wifi password in softAP mode (implement later)
        // no functionality in BLE mode
        const char *service_key = NULL;

#ifdef CONFIG_PROVISION_TRANSPORT_BLE
    
        uint8_t custom_service_uuid[] = {
            /* LSB <---------------------------------------
             * ---------------------------------------> MSB */
            0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
            0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
        };

        /* If your build fails with linker errors at this point, then you may have
         * forgotten to enable the BT stack or BTDM BLE settings in the SDK (e.g. see
         * the sdkconfig.defaults in the example project) */
        wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
#endif /* CONFIG_PROVISION_TRANSPORT_BLE */

    wifi_prov_mgr_endpoint_create("custom-data");

#ifdef CONFIG_BLE_REPROVISIONING
        wifi_prov_mgr_disable_auto_stop(1000);
#endif
    /* Start provisioning service */
    // NULL for pop as we select security level 0.
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, NULL, service_name, service_key));

    wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);

#ifdef CONFIG_PROVISION_TRANSPORT_BLE
    wifi_prov_print_qr(service_name, NULL, PROV_TRANSPORT_BLE);
#endif /* CONFIG_PROV_TRANSPORT_BLE */
    } else {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");

        /* We don't need the manager as device is already provisioned,
         * so let's release it's resources */
        wifi_prov_mgr_deinit();

        /* Start Wi-Fi station */
        wifi_init_sta();
    }

    /* Wait for Wi-Fi connection */
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, true, true, portMAX_DELAY);


    return ESP_OK;
}



esp_err_t wifi_stop() {

    return ESP_OK;
}

esp_err_t wifi_connect() {
    ESP_LOGI(TAG, "Start wifi connect");
    ESP_RETURN_ON_ERROR(wifi_provisioning(), TAG, "wifi start failed");
    //ESP_RETURN_ON_ERROR(wifi_start(), TAG, "wifi start failed");
    // ESP_RETURN_ON_ERROR(wifi_sta_do_connect(), TAG, "wifi connection failed");

    return ESP_OK;
}

