# Weather Station — Build Progress

## Hardware

| Component | Part | Bus | Address | Status |
|-----------|------|-----|---------|--------|
| MCU | ESP32-S3-WROOM-1-N8 (8 MB flash) | — | — | — |
| Display | NHD-2.4-240320CF-CTXI#-F (ILI9341) | 8-bit 8080 parallel | — | untested |
| Air Quality | Sensirion SEN66 | I2C | 0x6B | untested |
| Baro/Temp | Bosch BMP851 | I2C | 0x76 | untested |
| IMU | ST ISM330DHC | I2C | 0x6A | untested |
| Power | USB or Li-Ion, PWRGD line | GPIO | — | untested |

## Architecture Layers

```
┌──────────────────────────────────────────────────────────────────┐
│                         App Modes (Kconfig)                      │
│           CONFIG_APP_MODE = DEBUG | PRODUCTION                   │
├────────────┬─────────────┬────────────┬──────────────────────────┤
│ debug_app  │ sensor_task │ disp_task  │  net_task   │  ota_task  │
│  (hw test) │             │ (LVGL UI)  │  (upload)   │            │
├────────────┴─────────────┴────────────┴─────────────┴────────────┤
│                    esp_event_loop  (event bus)                    │
│        SENSOR_DATA_EVT  │  BUTTON_EVT  │  WIFI_EVT  │  OTA_EVT  │
├────────────────────┬─────────────────────────────────────────────┤
│   data_store       │         net_mgr          │    ota_mgr       │
│  (ring buf→flash)  │  (WiFi reconnect + HTTP) │  (esp_https_ota) │
├────────────────────┴──────────────────────────┴──────────────────┤
│                         HAL layer                                │
│   hal_sensors  (real → drivers  OR  mock → synthetic data)       │
│   hal_display  (real → ili9341  OR  mock → UART log)             │
│   hal_storage  (real → LittleFS OR  mock → RAM ring buf)         │
├──────────────────────────────────────────────────────────────────┤
│   drv_ili9341  │  drv_sen66  │  drv_bmp851  │  drv_ism330dhc    │
└──────────────────────────────────────────────────────────────────┘
```

## Build Phases

### Phase 0 — Debug / Hardware Test App
- [x] Add Kconfig.projbuild with APP_MODE choice
- [x] Create events.h (event base + data types)
- [x] Create debug_app.c (sequential hw test, PASS/FAIL per peripheral)
- [x] Refactor main.c to dispatch debug vs production
- [x] Build test (idf-build compiles clean, 265 KB, 83% flash free)
- [x] QEMU smoke test (QEMU needs libpixman-1 — install with `sudo apt install libpixman-1-0`)

### Phase 1 — HAL + QEMU Mock
- [x] Create hal_sensors component (hal_sensors.h)
- [x] Implement hal_sensors_real.c (calls drivers)
- [x] Implement hal_sensors_mock.c (synthetic data)
- [x] Create hal_display component (hal_display.h)
- [x] Implement hal_display_real.c (calls drv_ili9341)
- [x] Implement hal_display_mock.c (UART text output)
- [x] Add Kconfig HAL_USE_MOCK toggle
- [x] Refactor debug_app.c to use HAL
- [x] Extract board_config component (board.h + app_config.h)
- [x] Build test: mock+debug (210 KB, 86% free), real+debug (265 KB, 83% free)
- [x] Commit

### Phase 2 — Event Bus Refactor
- [x] Create events.c with ESP_EVENT_DEFINE_BASE for all event bases
- [x] Create sensor_task (reads HAL, posts SENSOR_EVT_DATA via esp_event)
- [x] Create button_task (polls GPIOs with 60 ms debounce, posts BUTTON_EVT_*)
- [x] Create production_app (event loop + task startup + event handlers)
- [x] Add sdkconfig.production overlay
- [x] Build test: all 4 modes pass (real/mock × debug/production)
- [x] Commit

### Phase 3 — Data Store (History)
- [x] Create data_store component with real/mock backends
- [x] Real: LittleFS-backed binary ring buffer (6 MB partition, 2048 entries max)
- [x] Mock: statically allocated RAM ring buffer
- [x] Custom partition table (partitions.csv): 2 MB app + 6 MB LittleFS
- [x] Integrate into production_app: sensor events auto-stored
- [x] IDF component dependency: joltwallet/littlefs ^1.14
- [x] Build test: all 4 modes pass (real/mock × debug/production)
- [ ] Add SNTP time sync (deferred to Phase 5 with network)
- [x] Commit

### Phase 4 — LVGL UI
- [ ] Integrate esp_lvgl_port
- [ ] Design dashboard page (live readings)
- [ ] Design history page (scroll through stored data)
- [ ] Wire button events to page navigation
- [ ] QEMU test with mock display
- [ ] Commit

### Phase 5 — Network Manager + Upload
- [ ] Create net_mgr component (WiFi STA, reconnect)
- [ ] Add Kconfig for SSID/password/server URL
- [ ] HTTP POST telemetry on interval
- [ ] Subscribe to SENSOR_DATA events for upload
- [ ] QEMU test (WiFi unavailable, verify graceful fallback)
- [ ] Commit

### Phase 6 — OTA
- [ ] Create ota_mgr component (esp_https_ota wrapper)
- [ ] Version check on boot or periodic
- [ ] Kconfig for OTA server URL
- [ ] Commit

## Component Directory

```
components/
  drv_ili9341/          ILI9341 8080 parallel display driver
  drv_sen66/            Sensirion SEN66 air quality driver
  drv_bmp851/           Bosch BMP851 baro/temp driver
  drv_ism330dhc/        ST ISM330DHC IMU driver
  hal_sensors/          Sensor HAL (real or mock backend)
  hal_display/          Display HAL (real or mock backend)
  data_store/           Ring buffer (LittleFS real / RAM mock)
  net_mgr/              WiFi manager + HTTP uploader
  ota_mgr/              OTA update manager

main/
  main.c                Init + dispatch by app mode
  debug_app.h/.c        Phase 0 hardware test
  production_app.h/.c   Event loop + task startup
  sensor_task.h/.c      Periodic HAL sensor read → esp_event
  button_task.h/.c      GPIO poll + debounce → esp_event
  events.h/.c           Event base definitions + data types
  Kconfig.projbuild     APP_MODE, HAL_USE_MOCK, WiFi creds, etc.
```

## Decisions Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-03-01 | Use esp_event over raw FreeRTOS queues | Decoupled pub/sub; adding subscribers doesn't touch publisher code |
| 2026-03-01 | HAL with compile-time mock via Kconfig | QEMU has no I2C/LCD_CAM; mock at HAL boundary is cleanest seam |
| 2026-03-01 | LittleFS over SPIFFS for data_store | Better wear leveling, directory support, power-loss resilience |
| 2026-03-01 | Separate debug_app vs production_app | Debug app does sequential hw test; production runs concurrent tasks |
