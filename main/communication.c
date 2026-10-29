#include <stdbool.h>
#include <string.h>

#include "communication.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#define MAX_REMOTE_NODES 16
#define SYNC_PACKET_MAGIC 0x53434331u

static const char *TAG = "smart_system_comm";
static portMUX_TYPE remote_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
	uint32_t magic;
	uint8_t room_id;
	uint8_t reserved[3];
	int32_t occupancy;
} sync_packet_t;

typedef struct {
	uint8_t mac[6];
	uint8_t room_id;
	int32_t occupancy;
	TickType_t last_seen;
	bool active;
} remote_node_t;

static remote_node_t remote_nodes[MAX_REMOTE_NODES];

static void espnow_receive(const esp_now_recv_info_t *info, const uint8_t *data,
						   int data_length)
{
	if (data_length != sizeof(sync_packet_t)) {
		return;
	}
	sync_packet_t packet;
	memcpy(&packet, data, sizeof(packet));
	if (packet.magic != SYNC_PACKET_MAGIC) {
		return;
	}
	const uint8_t *mac_address = info->src_addr;
	portENTER_CRITICAL(&remote_lock);
	size_t slot = MAX_REMOTE_NODES;
	for (size_t index = 0; index < MAX_REMOTE_NODES; ++index) {
		if (remote_nodes[index].active &&
			memcmp(remote_nodes[index].mac, mac_address, 6) == 0) {
			slot = index;
			break;
		}
		if (!remote_nodes[index].active && slot == MAX_REMOTE_NODES) {
			slot = index;
		}
	}
	if (slot < MAX_REMOTE_NODES) {
		memcpy(remote_nodes[slot].mac, mac_address, 6);
		remote_nodes[slot].room_id = packet.room_id;
		remote_nodes[slot].occupancy = packet.occupancy;
		remote_nodes[slot].last_seen = xTaskGetTickCount();
		remote_nodes[slot].active = true;
	}
	portEXIT_CRITICAL(&remote_lock);
}

esp_err_t communication_init(uint8_t room_id)
{
	(void)room_id;
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();
#if CONFIG_SMART_SYSTEM_ENABLE_WEB
	esp_netif_create_default_wifi_ap();
#endif
	wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
#if CONFIG_SMART_SYSTEM_ENABLE_WEB
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
#else
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
#endif
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
	ESP_ERROR_CHECK(esp_now_init());
	ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_receive));

	esp_now_peer_info_t peer = {0};
	memset(peer.peer_addr, 0xFF, sizeof(peer.peer_addr));
	peer.channel = 1;
	peer.ifidx = WIFI_IF_STA;
	peer.encrypt = false;
	ESP_ERROR_CHECK(esp_now_add_peer(&peer));
	ESP_LOGI(TAG, "ESP-NOW occupancy synchronization active on channel 1");
	return ESP_OK;
}

void communication_broadcast_occupancy(uint8_t room_id, int32_t occupancy)
{
	static const uint8_t broadcast_address[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	sync_packet_t packet = {
		.magic = SYNC_PACKET_MAGIC,
		.room_id = room_id,
		.occupancy = occupancy,
	};
	esp_err_t result = esp_now_send(broadcast_address, (const uint8_t *)&packet,
									sizeof(packet));
	if (result != ESP_OK && result != ESP_ERR_ESPNOW_NO_MEM) {
		ESP_LOGW(TAG, "Occupancy broadcast failed: %s", esp_err_to_name(result));
	}
}

int32_t communication_get_global_occupancy(uint8_t room_id, int32_t local_occupancy)
{
	int32_t total = local_occupancy;
	TickType_t now = xTaskGetTickCount();
	portENTER_CRITICAL(&remote_lock);
	for (size_t index = 0; index < MAX_REMOTE_NODES; ++index) {
		if (remote_nodes[index].active && remote_nodes[index].room_id == room_id &&
			(TickType_t)(now - remote_nodes[index].last_seen) < pdMS_TO_TICKS(10000)) {
			total += remote_nodes[index].occupancy;
		}
	}
	portEXIT_CRITICAL(&remote_lock);
	return total > 0 ? total : 0;
}

size_t communication_get_remote_nodes(remote_node_info_t *nodes, size_t max_nodes)
{
	if (nodes == NULL || max_nodes == 0) {
		return 0;
	}
	size_t found = 0;
	TickType_t now = xTaskGetTickCount();
	portENTER_CRITICAL(&remote_lock);
	for (size_t index = 0; index < MAX_REMOTE_NODES && found < max_nodes; ++index) {
		if (!remote_nodes[index].active) {
			continue;
		}
		memcpy(nodes[found].mac, remote_nodes[index].mac, sizeof(nodes[found].mac));
		nodes[found].room_id = remote_nodes[index].room_id;
		nodes[found].occupancy = remote_nodes[index].occupancy;
		nodes[found].age_ms =
			(uint32_t)((TickType_t)(now - remote_nodes[index].last_seen) *
					   portTICK_PERIOD_MS);
		nodes[found].active =
			(TickType_t)(now - remote_nodes[index].last_seen) < pdMS_TO_TICKS(10000);
		++found;
	}
	portEXIT_CRITICAL(&remote_lock);
	return found;
}