#include "esp_log.h"
#include "nvs_flash.h"
/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* WIFI */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "esp_http_client.h"
#include "cJSON.h"

//wifi
#define SSID      "Faisala12"
#define PASS      "Boboiboy12"

#define CONNECTED_BIT BIT0
#define FAIL_BIT      BIT1
//url Firebase Anda
#define urlFirebase "https://status-kunci-pintu-default-rtdb.firebaseio.com/data.json"

static uint8_t own_addr_type;
static const char *TAG = "BLE-Server";
static const char *TAG1 = "NIMBLE";

void ble_app_advertise(void);
static EventGroupHandle_t s_wifi_event_group;
void ble_store_config_init(void);

static uint8_t own_addr_type;
uint32_t data;
bool sPintu;
bool nsPintu;
size_t free_heap;

static int
gatt_svr_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        /* Insert code to handle read request here */
        if (sPintu == 0){
            os_mbuf_append(ctxt->om, "Pintu Tertutup", sizeof("Pintu Tertutup"));
        }
        else{
            os_mbuf_append(ctxt->om, "Pintu Terbuka", sizeof("Pintu Terbuka"));
        }
    } 
    else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        //printf("write %.*s\n", ctxt->om->om_len, ctxt->om->om_data);
        uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);

        if(data_len != sizeof(uint32_t)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        
        memcpy(&data, ctxt->om->om_data, data_len);
        if (data == 0){
            sPintu = false;
        }  
        else{
            sPintu = true;
        }       
        //printf("Received data : %lu ; %d\n", data, sPintu);

    }

    return 0;
}
//kirim data ke firebase
void send_data_firebase () {
    esp_http_client_config_t config_patch = {
        .url = urlFirebase,
        .method = HTTP_METHOD_PATCH};

    esp_http_client_handle_t patch = esp_http_client_init(&config_patch);

    esp_http_client_set_header(patch, "Content-Type", "application/json");

    cJSON *patch_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(patch_json, "\"Pintu\"", nsPintu);

    char *patch_str = cJSON_Print(patch_json);
    esp_http_client_set_post_field(patch, patch_str, strlen(patch_str));

    esp_err_t err = esp_http_client_perform(patch);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "PATCH request successful\n");
        free_heap = esp_get_free_heap_size();
        printf("Remaining free heap memory: %d bytes\n", free_heap);
    } else {
        ESP_LOGE(TAG, "PATCH request failed: %s\n", esp_err_to_name(err));
        free_heap = esp_get_free_heap_size();
        printf("Remaining free heap memory: %d bytes\n", free_heap);
    } 
    esp_http_client_cleanup(patch);
    cJSON_Delete(patch_json);
    free(patch_str);
}
//kirim ke server thingspeak
/*
void send_thingspeak(){

    esp_http_client_config_t http_cfg = {
        .url = "https://api.thingspeak.com/update",
        .method = HTTP_METHOD_POST
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_http_client_set_header(client, "content-type", "application/x-www-form-urlencoded");

    char* post_data = (char*) malloc(sizeof(char) * 40);
    sprintf(post_data, "api_key=EOTNWO4C76VB6OQF&field3=%d", nsPintu);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK){
        printf("Sent data, Succes\n");
        free_heap = esp_get_free_heap_size();
        printf("Remaining free heap memory: %d bytes\n", free_heap);
    }
    else{
        printf("failed\n");
        free_heap = esp_get_free_heap_size();
        printf("Remaining free heap memory: %d bytes\n", free_heap);
    }
    esp_http_client_cleanup(client);
    free(post_data);

}
*/
void send_data_thingspeak(void*pvParameters){
    nsPintu = !sPintu;
    for(;;){
        if (nsPintu != sPintu){
            printf("Masuk\n");
            nsPintu = sPintu;
            send_data_firebase();      
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        /* Insert the definition of the service here */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xABF0),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid = BLE_UUID16_DECLARE(0xABF1),
            .access_cb = gatt_svr_chr_access_cb,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        }, {
            0, /* No more characteristics in this service */
        } },
    },

    {
        0, /* No more services */
    },
};
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        printf("Wifi Start ...... \n");
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED){
        printf("wifi Reconnecting \n");
        esp_wifi_disconnect();
        esp_wifi_connect();

    } else if (event_id == WIFI_EVENT_STA_CONNECTED){
        printf("Mengirimkan data ke thingspeak\n");
        vTaskDelay(5000/portTICK_PERIOD_MS);
        xTaskCreate(send_data_thingspeak, "Send_data", 3072,NULL,1,NULL);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));

    wifi_config_t config_wifi = {
        .sta = {
            .ssid = SSID,
            .password = PASS
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config_wifi) );
    ESP_ERROR_CHECK(esp_wifi_start());
    
    printf("waiting WIFI\n");

    xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | FAIL_BIT , pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);

}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    // Advertise if connected
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG1, "BLE GAP EVENT CONNECT %s", event->connect.status == 0 ? "OK!" : "FAILED!");
        if (event->connect.status != 0)
        {
            printf("handle: %d", event->connect.conn_handle);
            ble_app_advertise();
        }
        else{
            ESP_LOGI(TAG1, "BLE GAP EVENT CONNECT %d",event->connect.conn_handle);
            
            ble_app_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG1, "BLE GAP EVENT DISCONNECT %d", event->disconnect.reason);
        /* Connection terminated; resume advertising */
        break;
    // Advertise again after completion of the event
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG1, "BLE GAP EVENT");
        ble_app_advertise();
        break;
    default:
        break;
    }
    return 0;
}


void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *device_name;
    int rc;

    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    device_name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
        return;
    }
}
static void
bleprph_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

void ble_app_on_sync(void)
{
    
    ble_hs_id_infer_auto(0, &own_addr_type); // Determines the best address type automatically
    /* Printing ADDR */
    uint8_t addr_val[6] = {0};
    ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    ESP_LOGI(TAG1, "Device Address:%02x:%02x:%02x:%02x:%02x:%02x",
             addr_val[5],
             addr_val[4],
             addr_val[3],
             addr_val[2],
             addr_val[1],
             addr_val[0]);
    /* Begin advertising. */
    ble_app_advertise();                     // Define the BLE connection

}

// The infinite task
void host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    int rc;

    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    nimble_port_init();
    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = bleprph_on_reset;
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    //ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    rc = ble_svc_gap_device_name_set("Banana");
    assert(rc == 0);
    
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_store_config_init();

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

}
