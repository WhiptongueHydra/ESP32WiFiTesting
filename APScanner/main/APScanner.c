#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"


// Just made this thread to closedown the app
void wifi_stop(){
	// Optional one to clear any scanned APs
	ESP_ERROR_CHECK(esp_wifi_clear_ap_list());

	// Stops wifi and deinits the stack
	ESP_ERROR_CHECK(esp_wifi_stop());
	ESP_ERROR_CHECK(esp_wifi_deinit());

	for(;;){
		vTaskDelay(pdMS_TO_TICKS(100000));
	}
}

void scan_stations(){
	printf("Scanning for nearby APs...\n");

	// Scan for stations, bool set true to block thread
	ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));

	printf("Scan complete.\n");

	uint16_t numApsDetected;
	ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&numApsDetected));

	printf("\n%d access points detected:\n", numApsDetected);

	wifi_ap_record_t apScanResults[numApsDetected];
	ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&numApsDetected, apScanResults));

	uint8_t record;

	for(record=0;record<numApsDetected;record++){
		printf("%d.\t%s:\t\t%d dBm\n", (record+1), apScanResults[record].ssid, apScanResults[record].rssi);
	}

	printf("\n\n");


	// Closes wifi down
	xTaskCreate(wifi_stop, "Terminates wifi", 2048, NULL, 1, NULL);

	for(;;){
		vTaskDelay(pdMS_TO_TICKS(100000));
	}
}


void app_main(){
	// Sets up NVS flash because wifi shit needs it
	ESP_ERROR_CHECK(nvs_flash_init());

	// Need some wifi struct to intialize the stack
	wifi_init_config_t wifiInitStruct = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifiInitStruct));

	// Set mode to station
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	// Start stack
	ESP_ERROR_CHECK(esp_wifi_start());

	xTaskCreate(scan_stations, "Scan for Stations", 2048, NULL, 1, NULL);

	vTaskDelete(NULL);
}

