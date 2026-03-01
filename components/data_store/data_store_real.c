/**
 * data_store_real.c — LittleFS-backed ring buffer for real hardware.
 *
 * Stores sensor snapshots in a binary file on a LittleFS partition.
 * A small header at the start of the file tracks head position and count.
 * Writes are batched — call data_store_flush() to persist.
 *
 * File layout:
 *   [header: 8 bytes] [entry 0] [entry 1] ... [entry N-1]
 *
 * Header:
 *   uint32_t head   — next write index
 *   uint32_t count  — number of valid entries
 */

#include "data_store.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_littlefs.h"

static const char *TAG = "data_store";

#define MOUNT_POINT   "/data"
#define STORE_FILE    MOUNT_POINT "/sensors.bin"
#define PARTITION     "storage"

typedef struct {
    uint32_t head;
    uint32_t count;
} store_header_t;

#define HEADER_SIZE   sizeof(store_header_t)
#define ENTRY_SIZE    sizeof(hal_sensor_data_t)

static store_header_t s_hdr;
static bool s_mounted = false;
static uint32_t s_writes_since_flush = 0;

#define FLUSH_INTERVAL 10  /* flush header to disk every N writes */

static off_t entry_offset(uint32_t index)
{
    return (off_t)(HEADER_SIZE + index * ENTRY_SIZE);
}

static esp_err_t write_header(void)
{
    FILE *f = fopen(STORE_FILE, "r+b");
    if (!f) return ESP_FAIL;
    fseek(f, 0, SEEK_SET);
    size_t written = fwrite(&s_hdr, 1, HEADER_SIZE, f);
    fclose(f);
    return (written == HEADER_SIZE) ? ESP_OK : ESP_FAIL;
}

esp_err_t data_store_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path       = MOUNT_POINT,
        .partition_label = PARTITION,
        .format_if_mount_failed = true,
        .dont_mount      = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_mounted = true;

    size_t total = 0, used = 0;
    esp_littlefs_info(PARTITION, &total, &used);
    ESP_LOGI(TAG, "LittleFS mounted: total=%u KB, used=%u KB",
             (unsigned)(total / 1024), (unsigned)(used / 1024));

    /* Try to read existing header */
    FILE *f = fopen(STORE_FILE, "rb");
    if (f) {
        size_t rd = fread(&s_hdr, 1, HEADER_SIZE, f);
        fclose(f);
        if (rd == HEADER_SIZE && s_hdr.count <= DATA_STORE_MAX_ENTRIES
                              && s_hdr.head  <  DATA_STORE_MAX_ENTRIES) {
            ESP_LOGI(TAG, "Loaded existing store: %lu entries, head=%lu",
                     (unsigned long)s_hdr.count, (unsigned long)s_hdr.head);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Invalid header, re-creating store file");
    }

    /* Create new store file */
    f = fopen(STORE_FILE, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create store file");
        return ESP_FAIL;
    }
    s_hdr.head  = 0;
    s_hdr.count = 0;
    fwrite(&s_hdr, 1, HEADER_SIZE, f);
    fclose(f);

    ESP_LOGI(TAG, "Created new store file (%d entries max)", DATA_STORE_MAX_ENTRIES);
    return ESP_OK;
}

esp_err_t data_store_append(const hal_sensor_data_t *data)
{
    if (!s_mounted || !data) return ESP_ERR_INVALID_STATE;

    FILE *f = fopen(STORE_FILE, "r+b");
    if (!f) return ESP_FAIL;

    fseek(f, entry_offset(s_hdr.head), SEEK_SET);
    size_t written = fwrite(data, 1, ENTRY_SIZE, f);
    fclose(f);

    if (written != ENTRY_SIZE) return ESP_FAIL;

    s_hdr.head = (s_hdr.head + 1) % DATA_STORE_MAX_ENTRIES;
    if (s_hdr.count < DATA_STORE_MAX_ENTRIES) {
        s_hdr.count++;
    }

    s_writes_since_flush++;
    if (s_writes_since_flush >= FLUSH_INTERVAL) {
        write_header();
        s_writes_since_flush = 0;
    }

    return ESP_OK;
}

size_t data_store_count(void)
{
    return s_hdr.count;
}

esp_err_t data_store_read(size_t index, hal_sensor_data_t *data)
{
    if (!s_mounted || !data) return ESP_ERR_INVALID_STATE;
    if (index >= s_hdr.count) return ESP_ERR_INVALID_ARG;

    uint32_t oldest = (s_hdr.head + DATA_STORE_MAX_ENTRIES - s_hdr.count)
                       % DATA_STORE_MAX_ENTRIES;
    uint32_t actual = (oldest + (uint32_t)index) % DATA_STORE_MAX_ENTRIES;

    FILE *f = fopen(STORE_FILE, "rb");
    if (!f) return ESP_FAIL;

    fseek(f, entry_offset(actual), SEEK_SET);
    size_t rd = fread(data, 1, ENTRY_SIZE, f);
    fclose(f);

    return (rd == ENTRY_SIZE) ? ESP_OK : ESP_FAIL;
}

esp_err_t data_store_read_latest(hal_sensor_data_t *data)
{
    if (!s_mounted || !data) return ESP_ERR_INVALID_STATE;
    if (s_hdr.count == 0) return ESP_ERR_NOT_FOUND;

    uint32_t latest = (s_hdr.head + DATA_STORE_MAX_ENTRIES - 1)
                       % DATA_STORE_MAX_ENTRIES;

    FILE *f = fopen(STORE_FILE, "rb");
    if (!f) return ESP_FAIL;

    fseek(f, entry_offset(latest), SEEK_SET);
    size_t rd = fread(data, 1, ENTRY_SIZE, f);
    fclose(f);

    return (rd == ENTRY_SIZE) ? ESP_OK : ESP_FAIL;
}

esp_err_t data_store_flush(void)
{
    if (!s_mounted) return ESP_ERR_INVALID_STATE;
    return write_header();
}

esp_err_t data_store_deinit(void)
{
    if (!s_mounted) return ESP_OK;

    write_header();
    esp_vfs_littlefs_unregister(PARTITION);
    s_mounted = false;
    ESP_LOGI(TAG, "Data store de-initialised");
    return ESP_OK;
}
