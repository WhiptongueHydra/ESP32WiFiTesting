#include <stdio.h>
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "nvs_flash.h"
#include "config.h" // Includes WiFi credentials


// I think ESP IDF prevents concurrent radio accesses automatically
// anyways through internal synchronisation - but I am still going to
// add a mutex on the radio mainly just to learn about FreeRTOS
// synchronisation structures and shit.
SemaphoreHandle_t 	radioMutex;
EventGroupHandle_t	scanWaitGroup; // Just for when scan is done atm


void wifi_closedown(){
	// Stop wifi stack gracefully
	ESP_ERROR_CHECK(esp_wifi_clear_ap_list());
	ESP_ERROR_CHECK(esp_wifi_disconnect());
	ESP_ERROR_CHECK(esp_wifi_stop());
	ESP_ERROR_CHECK(esp_wifi_deinit());
}



// Might end up making this loop indefinitely in the background
void ap_scanner(){
	// Mutex should be used whilst it accesses the radio
	// OR ALTERNATIVELY CONNECTING THREAD SHOULD BE BLOCKED whilst
	// AP not found.

	printf("AP_SCANNER(): Awaiting radio access...\n");
	if(xSemaphoreTake(radioMutex, portMAX_DELAY) == pdTRUE){
		printf("AP_SCANNER(): Radio access granted.\n");

		printf("AP_SCANNER(): Scanning for nearby APs...\n");
		ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));
		xSemaphoreGive(radioMutex);
		printf("AP_SCANNER(): Scan complete.\n");

		uint16_t numAPs;
		ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&numAPs));
		printf("AP_SCANNER(): %d APs in vicinity.\n", numAPs);

		wifi_ap_record_t scanResults[numAPs];
		ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&numAPs, scanResults));

		uint8_t ap;
		printf("AP_SCANNER(): Comparing APs to credentials...\n");
		for(ap=0;ap<numAPs;ap++){
			if(strcmp((char*)scanResults[ap].ssid, WIFI_SSID) == 0){
				printf("AP_SCANNER(): Match found. Connecting to %s...\n", WIFI_SSID);
				xEventGroupSetBits(scanWaitGroup, BIT0);
			}
		}

		// COMMENT THIS WHEN CONNETION STUFF ADDED
		// wifi_closedown();
	}

	for(;;){
		vTaskDelay(pdMS_TO_TICKS(100000));
	}
}



// Making this a thread too incase reconnections need to happen etc.
// can make the beginning of the loop stall until either the first
// connection needs to occur - or connection drops and reconnection
// must happen.
void ap_connect(){
	// Need to start this thread when flags are set. Just going
	// to do it for when scan is done at first.
	printf("AP_CONNECT(): Awaiting scan completion...\n");
	EventBits_t connectFlags;
	connectFlags = xEventGroupWaitBits(scanWaitGroup, BIT0, pdTRUE, pdFALSE, portMAX_DELAY);

	if(connectFlags & BIT0){
		printf("AP_CONNECT(): Configuring credentials...\n");
		wifi_config_t cfgWifi = {0};
		strcpy((char *)cfgWifi.sta.ssid, WIFI_SSID);
		strcpy((char *)cfgWifi.sta.password, WIFI_PASS);

		ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfgWifi));

		printf("AP_CONNECT(): Awaiting radio access...\n");
		if(xSemaphoreTake(radioMutex, portMAX_DELAY) == pdTRUE){
			printf("AP_CONNECT(): Access granted.\n");

			ESP_ERROR_CHECK(esp_wifi_connect());
			xSemaphoreGive(radioMutex);

			vTaskDelay(pdMS_TO_TICKS(10000));
			wifi_closedown();
		}
	}

	for(;;){
		vTaskDelay(pdMS_TO_TICKS(100000));
	}
}


// Apparently need this basic event handler to print out wifi event shit
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        printf("EVENT: STA_START\n");

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        printf("EVENT: STA_CONNECTED\n");

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconn = (wifi_event_sta_disconnected_t*) event_data;
        printf("EVENT: STA_DISCONNECTED\n");
        printf("Reason: %d\n", disconn->reason);

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        printf("EVENT: GOT_IP\n");
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        printf("IP Address: " IPSTR "\n", IP2STR(&event->ip_info.ip));
    }
}




void app_main(void)
{
	// Synchonisation tools
	radioMutex = xSemaphoreCreateMutex();
	scanWaitGroup = xEventGroupCreate();

	// NETIF setup (didnt even know I needed this, also need event handling apparently
	// but supposedly this fixes it)
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_t *netif = esp_netif_create_default_wifi_sta();
	if (netif == NULL) {
    		printf("Failed to create default Wi-Fi STA interface\n");
	}

	// Event handling setup
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));


	// Wifi Setup
	ESP_ERROR_CHECK(nvs_flash_init());
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());

	// Set up new threads
	xTaskCreate(ap_scanner, "AP_SCANNER", 4096, NULL, 1, NULL);
	xTaskCreate(ap_connect, "AP_CONNECT", 8192, NULL, 1, NULL);

	// Terminate launcher thread
	vTaskDelete(NULL);
}
