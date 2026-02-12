extern "C" {
    #include <stdio.h>
    #include <string.h>
    #include <sys/param.h>
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/queue.h"
    #include "nvs_flash.h"
    #include "esp_err.h"
    
    // ESP32 drivers
    #include "driver/i2c.h"
    #include "driver/i2c_master.h"
    #include "driver/gpio.h"
    
    // wifi and network
    #include "esp_wifi.h"
    #include "esp_event.h"
    #include "esp_log.h"
    #include "esp_system.h"
    #include "lwip/sockets.h"
    #include "lwip/netdb.h"
}

#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22

#define WIFI_SSID      "1908"        
#define WIFI_PASS      "nenuCHEPPA007"    
#define SERVER_IP      "10.0.0.86"      
#define SERVER_PORT    9000

//global variables
unsigned short dig_T1;
short dig_T2;
short dig_T3;
typedef int BMP280_S32_t;
BMP280_S32_t t_fine;


i2c_master_dev_handle_t dev_handle = NULL;
bool bmp280_ready = false;

QueueHandle_t tempQueue = nullptr;
int tcp_socket = -1;
bool wifi_connected = false;


BMP280_S32_t bmp280_compensate_T_int32(BMP280_S32_t adc_T)
{
    BMP280_S32_t var1, var2, T;
    var1 = ((((adc_T>>3) - ((BMP280_S32_t)dig_T1<<1))) * ((BMP280_S32_t)dig_T2)) >> 11;
    var2 = (((((adc_T>>4) - ((BMP280_S32_t)dig_T1)) * ((adc_T>>4) - ((BMP280_S32_t)dig_T1))) >> 12) * ((BMP280_S32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    return T;
}


void bmp280_init(void)
{
    printf("initializing I2C and BMP280\n");
    
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io_conf.pin_bit_mask = (1ULL << SDA_PIN) | (1ULL << SCL_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    printf("GPIO configured (SDA: GPIO%d, SCL: GPIO%d)\n", SDA_PIN, SCL_PIN);
    printf("[internal pull-ups enabled\n");
    
    i2c_master_bus_config_t i2c_mst_config = {};
    i2c_mst_config.i2c_port = I2C_NUM_0;
    i2c_mst_config.sda_io_num = SDA_PIN;
    i2c_mst_config.scl_io_num = SCL_PIN;
    i2c_mst_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_mst_config.glitch_ignore_cnt = 7;
    i2c_mst_config.flags.enable_internal_pullup = 1;
    
    i2c_master_bus_handle_t bus_handle;
    esp_err_t ret = i2c_new_master_bus(&i2c_mst_config, &bus_handle);
    if (ret != ESP_OK) {
        printf("I2C bus init failed: 0x%x\n", ret);
        return;
    }

    uint8_t bmp_addr = 0;
    uint8_t test_addresses[] = {0x76, 0x77};
    
    printf("\n scanning for BMP280 \n");
    for (int i = 0; i < 2; i++) {
        printf("trying address: 0x%02x... ", test_addresses[i]);
        
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = test_addresses[i];
        dev_cfg.scl_speed_hz = 100000;
        
        i2c_master_dev_handle_t temp_handle;
        ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &temp_handle);
        if (ret != ESP_OK) {
            printf("failed to add device\n");
            continue;
        }
        
        unsigned char buffer[2];
        buffer[0] = 0xd0;
        ret = i2c_master_transmit(temp_handle, buffer, 1, 1000);
        if (ret == ESP_OK) {
            ret = i2c_master_receive(temp_handle, buffer, 1, 1000);
            if (ret == ESP_OK) {
                if (buffer[0] == 0x58) {
                    printf("BMP280 (Chip id: 0x%02x)\n", buffer[0]);
                    bmp_addr = test_addresses[i];
                    dev_handle = temp_handle;
                    break;
                } else {
                    printf("no chip id: 0x%02x (expected 0x58)\n", buffer[0]);
                }
            } else {
                printf("no response\n");
            }
        } else {
            printf("no response\n");
        }
    }
    
    if (bmp_addr == 0) {
        printf("BMP280 NOT FOUND\n");
        return;
    }

    unsigned char buffer[0x10];
    
    buffer[0] = 0xf4;
    buffer[1] = 0b10110111;
    ret = i2c_master_transmit(dev_handle, buffer, 2, 1000);
    if (ret != ESP_OK) {
        printf("failed to configure BMP280\n");
        return;
    }

    
    vTaskDelay(pdMS_TO_TICKS(100));

    buffer[0] = 0x88;
    ret = i2c_master_transmit(dev_handle, buffer, 1, 1000);
    if (ret != ESP_OK) {
        return;
    }
    
    ret = i2c_master_receive(dev_handle, buffer, 6, 1000);
    if (ret != ESP_OK) {

        return;
    }

    dig_T1 = buffer[0] | (buffer[1] << 8);
    dig_T2 = (short)(buffer[2] | (buffer[3] << 8));
    dig_T3 = (short)(buffer[4] | (buffer[5] << 8));
   
    printf("dig_T1: %u\n", dig_T1);
    printf("dig_T2: %d\n", dig_T2);
    printf("dig_T3: %d\n", dig_T3);
   
    if (dig_T1 == 0 || dig_T1 == 0xFFFF) {
        return;
    }
    
    bmp280_ready = true;
}

float bmp280_read_temperature_c(void)
{
    if (!bmp280_ready || dev_handle == NULL) {
        printf("BMP280 not initialized!\n");
        return -999.0;
    }
    
    unsigned char buffer[6];

    buffer[0] = 0xfa;
    esp_err_t ret = i2c_master_transmit(dev_handle, buffer, 1, 1000);
    if (ret != ESP_OK) {
        printf("failed to request temperature data (error: 0x%x)\n", ret);
        return -999.0;
    }
    
    ret = i2c_master_receive(dev_handle, buffer, 3, 1000);
    if (ret != ESP_OK) {
        printf("failed to read temperature data (error: 0x%x)\n", ret);
        return -999.0;
    }

    int T = (((int)buffer[2] & 0xf0) >> 4) | ((int)buffer[1] << 4) | ((int)buffer[0] << 12);
    

    if (T == 0 || T == 0x80000) {
        printf("invalid temperature ADC reading: 0x%x\n", T);
        return -999.0;
    }
    
    return (float)bmp280_compensate_T_int32(T) / 100.0;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        printf("connecting to wifi\n");
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        if (tcp_socket >= 0) {
            close(tcp_socket);
            tcp_socket = -1;
        }
        esp_wifi_connect();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        printf("IP address: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        wifi_connected = true;
    }
}

void wifi_init_sta(void)
{
    printf("initializing wifi\n");
    
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {};
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    
    printf("wifi connecting to: %s\n", WIFI_SSID);
}

void temperature_task(void *pvParameters)
{
    const TickType_t delayTicks = pdMS_TO_TICKS(500);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    if (!bmp280_ready) {
        printf("BMP280 not ready - task ending.\n");
        vTaskDelete(NULL);
        return;
    }
    
    while (true) {
        float temp_c = bmp280_read_temperature_c();
        
        if (temp_c != -999.0) { 
            printf("temperature: %.2f °C (%.2f °F)\n", temp_c, (temp_c * 9.0/5.0) + 32.0);
            
            if (tempQueue != nullptr) {
                if (xQueueSend(tempQueue, &temp_c, pdMS_TO_TICKS(100)) != pdPASS) {
                    printf("warning: queue full- data dropped.\n");
                }
            }
        } else {
            printf("invalid temperature reading\n");
        }
        
        vTaskDelay(delayTicks);
    }
}


void wifi_comm_task(void *pvParameters)
{
    float temp_c;
    char payload[128];
    
    
    while (true) {
        if (!wifi_connected || tcp_socket < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        if (xQueueReceive(tempQueue, &temp_c, pdMS_TO_TICKS(1000)) == pdPASS) {
            // JSON format
            snprintf(payload, sizeof(payload), 
                     "{\"temperature\":%.2f,\"timestamp\":%lu}\n",
                     temp_c, 
                     (unsigned long)xTaskGetTickCount());
            
            int bytes_sent = send(tcp_socket, payload, strlen(payload), 0);
            
            if (bytes_sent < 0) {
                printf("[wifi task send failed: errno %d. closing socket.\n", errno);
                close(tcp_socket);
                tcp_socket = -1;
            } else {
                printf("wifi task sent to PC: %.2f °C\n", temp_c);
            }
        }
    }
}

void network_mgmt_task(void *pvParameters)
{
    struct sockaddr_in dest_addr;
    
    while (true) {
        if (!wifi_connected) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        
        if (tcp_socket < 0) {
            memset(&dest_addr, 0, sizeof(dest_addr));
            dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(SERVER_PORT);

            tcp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (tcp_socket < 0) {
                printf("socket creation failed: errno %d\n", errno);
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }

            printf("network connecting to %s:%d...\n", SERVER_IP, SERVER_PORT);
            
            int err = connect(tcp_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err != 0) {
                printf("network onnection failed: errno %d\n", errno);
                close(tcp_socket);
                tcp_socket = -1;
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }

            printf("network successfully connected to server\n\n");
        }
       
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// MAIN FUNCTION

extern "C" void app_main(void)
{
    printf("\n\n");
    printf("  BMP280 Temperature Monitoring System  \n");
    printf("  CSYE 6550 IoT/Embedded Development    \n");
 
    printf("initializing NVS Flash\n");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        printf("erasing NVS Flash...\n");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        printf("NVS init failed: 0x%x\n", ret);
        return;
    }
    printf("NVS Flash initialized\n\n");

    printf("creating temperature queue \n");
    tempQueue = xQueueCreate(10, sizeof(float));
    if (tempQueue == nullptr) {
        printf("failed to create temperature queue\n");
        return;
    }
    printf("temperature queue created \n\n");
    
    bmp280_init();
    
    printf("starting wifi \n");
    wifi_init_sta();

  
    xTaskCreate(temperature_task, 
                "temperature_task", 
                4096, 
                nullptr, 
                5,
                nullptr);

    xTaskCreate(wifi_comm_task, 
                "wifi_comm_task", 
                4096, 
                nullptr, 
                4,
                nullptr);
    
    xTaskCreate(network_mgmt_task, 
                "network_mgmt_task", 
                4096, 
                nullptr, 
                3,
                nullptr);
}