#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
//ble
#include "nimble/nimble_npl.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "blecent.h"
#include "esp_central.h"
//sensor
#include "driver/adc.h"
#include "driver/gpio.h"
#include "malloc.h"
#include "sdkconfig.h"

#define ADC_HALL_SENSOR ADC1_CHANNEL_0
#define tHold 3300
#define dOpen raw_adc_value < tHold
#define dClosed raw_adc_value > tHold
/* 16 Bit SPP Service UUID */
#define GATT_SPP_SVC_UUID                                  0xABF0

/* 16 Bit SPP Service Characteristic UUID */
#define GATT_SPP_CHR_UUID                                  0xABF1

QueueHandle_t queue;
int state;
int raw_adc_value;
bool status ;
int pintu;
uint16_t attr_handle;
uint16_t conn_handle;
// estatus = (status != true);

struct os_mbuf *om;


uint16_t attribute_handle[3];
void ble_store_config_init(void);
void discover_chr(uint16_t conn_handle);
void connect_ble(void);
static void ble_spp_client_set_handle(const struct peer *peer);
void send (void *pvParameters);





static void sensor (void *pvParameters){
    uint32_t value;
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_HALL_SENSOR, ADC_ATTEN_DB_0);
    for (;;) {
        printf("Waiting For DOOR Closed :\n");
        while(dOpen){
            raw_adc_value = adc1_get_raw(ADC_HALL_SENSOR);
            vTaskDelay(100/portTICK_PERIOD_MS);
        }
        status = false;
        value = 1;
        if(xQueueSendToBack(queue, &value, 0) != pdTRUE){
            printf("Failed to send value to queue\n");
        }
        printf("Waiting for door Open\n");
        while(dClosed){
            raw_adc_value = adc1_get_raw(ADC_HALL_SENSOR);
            vTaskDelay(100/portTICK_PERIOD_MS);
        }
        value = 0;
        if(xQueueSendToBack(queue, &value, 0) != pdTRUE){
            printf("Failed to send value to queue\n");
        }
        status = true;
        vTaskDelay(pdMS_TO_TICKS(100));
        
    }
}



static void ble_spp_client_set_handle(const struct peer *peer)
{
    //int rc;
    const struct peer_chr *chr;
    chr = peer_chr_find_uuid(peer,
                             BLE_UUID16_DECLARE(GATT_SPP_SVC_UUID),
                             BLE_UUID16_DECLARE(GATT_SPP_CHR_UUID));
    attribute_handle[peer->conn_handle - 1] = chr->chr.val_handle;
    attr_handle = chr->chr.val_handle;
    conn_handle = peer->conn_handle;
    
    xTaskCreate(send, "send data", 4096,NULL, 9, NULL);    
}

static void disc_on_complete_cb(const struct peer *peer, int status, void *arg){
    if(status != 0){
        MODLOG_DFLT(ERROR, "Error: Service discovery failed; status=%d "
                    "conn_handle=%d\n", status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    MODLOG_DFLT(INFO, "Service discovery complete; status=%d "
                "conn_handle=%d\n", status, peer->conn_handle);
    
    ble_spp_client_set_handle(peer);
    //kirim_data(peer);

}

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            MODLOG_DFLT(INFO, "Connection established , status=%d\n", event->connect.status);
            print_conn_desc(&desc);
            MODLOG_DFLT(INFO, "\n");
            rc = peer_add(event->connect.conn_handle);
            if (rc != 0) {
                MODLOG_DFLT(ERROR, "Failed to add peer; rc=%d\n", rc);
                return 0;
            }
            rc = peer_disc_all(event->connect.conn_handle, disc_on_complete_cb, NULL);
            if (rc != 0 ){
                MODLOG_DFLT(ERROR, "Failed to discovery rc: %d\n", rc);
                return 0;
            }
        }
        else{
            MODLOG_DFLT(ERROR, "Error: Connection failed; status=%d\n",
            event->connect.status);
            connect_ble();
        }
        break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI("GAP", "BLE GAP EVENT DISCONNECTED %s", event->connect.status == 1 ? "OK!" : "DISCONNECTED!");
            peer_delete(event->connect.conn_handle);
            connect_ble();
            break;
        case BLE_GAP_EVENT_CONN_UPDATE:
            printf("Connection updated");
            break;
        default:
            printf("Unknown gap event: %d", event->type);
            break;
    }

    return 0;
}


void connect_ble(void){ 
    struct ble_gap_conn_params c_param;
    ble_addr_t addr = {
        .type = BLE_OWN_ADDR_PUBLIC
    };
    addr.val[0] = 0xA2;  //A2
    addr.val[1] = 0x79;  //79
    addr.val[2] = 0x75;  //75
    addr.val[3] = 0x60;  //60
    addr.val[4] = 0x62;  //62
    addr.val[5] = 0xEC;  //EC

    memset(&c_param, 0, sizeof c_param);
    c_param.scan_itvl = 0x0100;
    c_param.scan_window = 0x0100;
    c_param.itvl_min = 0x0100;
    c_param.itvl_max = 0x0100;
    c_param.supervision_timeout = 0x0100;
    c_param.min_ce_len = 0x0100;
    c_param.max_ce_len = 0x0100;
    ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, 30000 , &c_param, ble_gap_event_cb, NULL);

}
void send (void *pvParameters){
    uint32_t value;
    int i;
    for(;;){
        if(xQueueReceive(queue, &value, portMAX_DELAY)==pdTRUE){
            uint8_t *mybuffer;
            mybuffer = malloc(sizeof(value));
            memset(mybuffer, 0, sizeof(value));
            memcpy(mybuffer, &value, sizeof(value));
            //printf("Attr handle = %d\n", attribute_handle[i]);
            int rc = ble_gattc_write_flat(conn_handle, attr_handle, mybuffer, sizeof(mybuffer), NULL, NULL);
            if (rc == 0){
                printf("Write succes\n");
            }
            else{
                printf("no\n");
            }
            free(mybuffer);             
            
            printf("Menerima data : %ld\n", value);
        }
    }

}

static void
ble_spp_client_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}


void ble_app_on_sync(void)
{
    int rc;
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    connect_ble();
}
void host_task(void *param)
{
   // connect_ble();
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
    nimble_port_freertos_deinit();
}
void app_main(void)
{
    
    int rc;
    queue = xQueueCreate(6000,sizeof(uint32_t));
    nvs_flash_init();
    nimble_port_init();
    rc = peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64);
    assert(rc == 0);
    ble_svc_gap_device_name_set("Halo-hi"); // 4 - Set device name characteristic
    ble_svc_gap_init();                             // 4 - Initialize GAP service
    ble_store_config_init();

    ble_hs_cfg.reset_cb = ble_spp_client_on_reset;
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    nimble_port_freertos_init(host_task);
    //connect_ble();
    xTaskCreate(sensor, "Hall Sensor", 2048, NULL, 9, NULL);
    //kirim_data();




}
