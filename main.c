 * Application 5 — Dual-core IPC pipeline
 *
 * Scaffold level: ~65% (the pipeline logic is yours; the infrastructure is provided).
 *
 * Scaffold Code - AI useage:
 *   Addition of the USE_WEBSERVER compile-time switch and a working serial
 *     monitor task on Core 0, plus per-task heartbeat counters
 *   Logic to allow for switching between a serial monitor and the (student-built)
 *     web monitor, so the pipeline runs in Wokwi with no Wi-Fi by default
 *   Commenting of code including human readable summaries
 *
 * The three IPC primitives are CREATED and wired; the pipeline LOGIC is yours:
 *   Queue             — fixed-size FIFO between producer & consumer
 *   Task notification — fast 1-to-1 signal (ISR or task → specific task)
 *   Event group       — N-way rendezvous (wait for a set of bits)
 * Required by the assignment: at least one use of each, EACH defended.
 *
 * What this scaffold gives you:
 *   - Producer / consumer / coordinator / responder task skeletons on Core 1.
 *   - The event-group rendezvous + coordinator→responder notification, wired.
 *   - A button ISR → responder direct-notification path.
 *   - Per-task heartbeat counters and a Core-0 monitor that prints queue depth,
 *     event-group bits, and heartbeats once a second (USE_WEBSERVER=0).
 *
 * What you do:
 *   1. Producer body — themed data items, queue send with a back-pressure policy.
 *   2. Consumer body — receive with timeout, themed processing.
 *   3. Web monitor — port App 1's HTTP code into webmonitor_task (USE_WEBSERVER=1).
 *   4. Size the queue (depth + item size) and defend it.
 *   5. Theme-rename (YOURTHEME): task names, log strings, the meaning of a data item.
 *
 * What you DON'T need to change:
 *   - The event-group / notification plumbing, the button ISR, or the monitor.
 *   - The heartbeat counters (already incremented at the end of each task loop).
 *
 * ============================================================
 *  RUN MODE  (serial monitor vs. web monitor)
 * ============================================================
 *
 * USE_WEBSERVER selects the Core-0 observability plane. The Core-1 pipeline is
 * identical in both modes.
 *
 *   USE_WEBSERVER = 0  -> Serial monitor (provided, working). Prints queue depth,
 *                         event bits, and heartbeats once a second. No Wi-Fi, so
 *                         the pipeline runs in Wokwi out of the box.
 *   USE_WEBSERVER = 1  -> Web monitor. Runs webmonitor_task instead — the stub you
 *                         fill in with App 1's HTTP server (deliverable #3). Needs
 *                         the Wi-Fi REQUIRES already in this folder's CMakeLists.
 *
 * Start on USE_WEBSERVER=0 to get the pipeline moving in the simulator, then flip
 * to 1 once you have implemented the web monitor.
 *
 * ============================================================
 * Theme: avionics
 * ============================================================
 */

#ifndef USE_WEBSERVER
#define USE_WEBSERVER 0
#endif

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"

#if USE_WEBSERVER
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#endif

#define BUTTON_GPIO GPIO_NUM_18
#if USE_WEBSERVER
#define HTTP_PORT 80
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASS ""
#endif

#define CONFIG_LOG_DEFAULT_LEVEL_INFO 1
#define CONFIG_LOG_MAXIMUM_LEVEL  5

static const char *TAG = "app5";
typedef struct {
    uint32_t timestamp_ms;
    uint32_t sequence;
    int sensor_id;
    int value;
} avionics_data_t;

/* ---------- IPC objects (created in app_main, used everywhere) ---------- */
static QueueHandle_t      data_q;   
     /* TODO: choose depth + item size for YOUR pipeline */
static EventGroupHandle_t evt_group;
static TaskHandle_t       responder_handle;

/* Event-group bit definitions */
#define EV_BIT_DATA_PRODUCED  (1 << 0)
#define EV_BIT_DATA_PROCESSED (1 << 1)

/* Per-task heartbeats — proof of life for the monitor. Single 32-bit reads are
 * atomic on Xtensa, so the monitor can read these without a lock (App 6's topic). */
static volatile uint32_t hb_prod, hb_cons, hb_coord, hb_resp;
static int64_t producer_wcet_us = 0;
static int64_t consumer_wcet_us = 0;
static int64_t monitor_wcet_us = 0;
static avionics_data_t last_item;
static volatile bool last_item_valid = false;
/* ---------- Producer task (Core 1) ----------
 * TODO(YOU): generate themed data. Push it into data_q. Set EV_BIT_DATA_PRODUCED.
 */
static void producer_task(void *arg)
{
    int tick = 0;
    avionics_data_t packet;
    for (;;) {
        /* TODO: build a themed data item.
         * Suggested struct: { uint32_t timestamp_ms; int value; } */
         int64_t start = esp_timer_get_time();
         packet.timestamp_ms = esp_timer_get_time() / 1000;
         packet.sequence = tick;
         packet.sensor_id = 1;
         packet.value = 30000 + tick;

        /* TODO: push into queue with a timeout. Decide back-pressure policy. */
       BaseType_t sent = xQueueSend(data_q, &packet, pdMS_TO_TICKS(10));

if (sent == pdPASS)
{
    ESP_LOGI(TAG,
             "[producer] seq=%lu sensor=%d altitude=%d ft",
             (unsigned long)packet.sequence,
             packet.sensor_id,
             packet.value);
}
else
{
    ESP_LOGW(TAG,
             "[producer] Telemetry queue full. Packet dropped.");
}
        xEventGroupSetBits(evt_group, EV_BIT_DATA_PRODUCED);
int64_t end = esp_timer_get_time();

if ((end - start) > producer_wcet_us)
{
    producer_wcet_us = end - start;
}
        tick++;
        hb_prod++;
        vTaskDelay(pdMS_TO_TICKS(50));   /* 20 Hz producer */
    }
}

/* ---------- Consumer task (Core 1) ----------
 * TODO(YOU): pop from queue, process, set EV_BIT_DATA_PROCESSED. */
static void consumer_task(void *arg)
{
  avionics_data_t received_packet;
    for (;;) {
        /* TODO(YOU): replace this placeholder wait with a real
         *   xQueueReceive(data_q, &item, timeout) plus themed processing.
         * The delay below only keeps the scaffold from busy-spinning (and
         * starving Core 1's idle task) until you wire the queue up — a real
         * xQueueReceive blocks on its own, so delete this line when you add it. */
         int64_t start = esp_timer_get_time();
        BaseType_t received = xQueueReceive(
        data_q,
        &received_packet,
        pdMS_TO_TICKS(100)
        );
        if (received != pdPASS)
        {
        continue;
        }

        /* TODO: process the item (themed work). */
       /* Simulated sensor-fusion correction */
          received_packet.value += 15;

          ESP_LOGI(TAG,
         "[fusion] seq=%lu fused_altitude=%d ft time=%lu ms",
         (unsigned long)received_packet.sequence,
         received_packet.value,
         (unsigned long)received_packet.timestamp_ms);
        last_item = received_packet;
        last_item_valid = true;
        xEventGroupSetBits(evt_group, EV_BIT_DATA_PROCESSED);
        int64_t end = esp_timer_get_time();

if ((end - start) > consumer_wcet_us)
{
    consumer_wcet_us = end - start;
}
        hb_cons++;
    }
}

/* ---------- Coordinator task (Core 1) ----------
 * Waits for BOTH event bits to be set, then signals the responder via direct
 * task notification.
 */
static void coordinator_task(void *arg)
{
    const EventBits_t wait_mask = EV_BIT_DATA_PRODUCED | EV_BIT_DATA_PROCESSED;
    for (;;) {
        EventBits_t got = xEventGroupWaitBits(evt_group, wait_mask,
                                              pdTRUE,   /* clear on exit */
                                              pdTRUE,   /* wait for ALL */
                                              portMAX_DELAY);
        if ((got & wait_mask) == wait_mask) {
          ESP_LOGI(TAG, "[telemetry] Sensor read and fusion complete — telemetry frame ready");
            /* TODO: do whatever the "cycle complete" event means for your theme.
             * Then notify the responder. */
            // Just getting started and want to see your button presses?
            // Comment out the line below. -mb 
            xTaskNotifyGive(responder_handle);
            hb_coord++;
        }
    }
}

/* ---------- Responder task (Core 1) ----------
 * Wakes via direct task notification from coordinator OR from button ISR.
 */
static void responder_task(void *arg)
{
    for (;;) {
        uint32_t n = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (n == 0) continue;
        ESP_LOGI(TAG,"[cockpit] Flight computer updated (notifications=%lu)",(unsigned long)n);
        /* TODO: themed action. */
        hb_resp++;
    }
}

/* ---------- Button ISR — notify responder directly ---------- */
static volatile int64_t last_edge_us;
static void IRAM_ATTR button_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (now - last_edge_us < 200000) return;
    last_edge_us = now;

    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(responder_handle, &woken);
    portYIELD_FROM_ISR(woken);
}

#if USE_WEBSERVER
/* ---------- Web monitor task (Core 0)  [USE_WEBSERVER = 1] ----------
 * TODO(YOU) — deliverable #3. Port the HTTP server from App 1/2 here:
 *   - nvs_flash_init(); esp_netif_init(); esp_event_loop_create_default();
 *   - wifi_init_sta();  (STA connect — serial prints "Got IP: 10.13.37.x")
 *   - start_webserver(); register a root handler that renders:
 *       * uxQueueMessagesWaiting(data_q)     — live queue depth
 *       * xEventGroupGetBits(evt_group)      — event-group bit state
 *       * hb_prod / hb_cons / hb_coord / hb_resp  — per-task heartbeats
 * The Wi-Fi headers are already included above under USE_WEBSERVER, and the
 * Wi-Fi/HTTP components are in this folder's CMakeLists REQUIRES.
 * Auto-refresh ~1 Hz; faster and your handler shows up in latency measurements.
 */
 static esp_err_t handle_root(httpd_req_t *req)
{
    static const char html[] =
        "<!DOCTYPE html>"
        "<html lang=\"en\">"
        "<head>"
        "<meta charset=\"utf-8\">"
        "<title>Avionics IPC Monitor</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; background:#0b1320; "
        "color:#e8f0ff; padding:2rem; }"
        "h1 { color:#63b3ff; }"
        ".grid { display:grid; grid-template-columns:repeat(2, 1fr); "
        "gap:1rem; max-width:700px; }"
        ".card { background:#17243a; padding:1rem; border-radius:10px; "
        "border:1px solid #34506f; }"
        ".label { color:#9cb5d1; font-size:0.9rem; }"
        ".value { font-size:2rem; font-weight:bold; margin-top:0.4rem; }"
        "</style>"
        "</head>"

        "<body>"
        "<h1>Avionics IPC Pipeline Monitor</h1>"

        "<div class=\"grid\">"

        "<div class=\"card\">"
        "<div class=\"label\">Queue Depth</div>"
        "<div id=\"queue_depth\" class=\"value\">--</div>"
        "</div>"

        "<div class=\"card\">"
        "<div class=\"label\">Event Bits</div>"
        "<div id=\"event_bits\" class=\"value\">--</div>"
        "</div>"

        "<div class=\"card\">"
        "<div class=\"label\">Producer Heartbeat</div>"
        "<div id=\"hb_prod\" class=\"value\">--</div>"
        "</div>"

        "<div class=\"card\">"
        "<div class=\"label\">Consumer Heartbeat</div>"
        "<div id=\"hb_cons\" class=\"value\">--</div>"
        "</div>"

        "<div class=\"card\">"
        "<div class=\"label\">Coordinator Heartbeat</div>"
        "<div id=\"hb_coord\" class=\"value\">--</div>"
        "</div>"

        "<div class=\"card\">"
        "<div class=\"label\">Responder Heartbeat</div>"
        "<div id=\"hb_resp\" class=\"value\">--</div>"
        "</div>"

        "</div>"

        "<script>"
        "async function poll(){"
        "try {"
        "const response = await fetch('/state', {cache:'no-store'});"
        "const state = await response.json();"

        "document.getElementById('queue_depth').textContent = "
        "state.queue_depth;"

        "document.getElementById('event_bits').textContent = "
        "'0x' + state.event_bits.toString(16).padStart(2, '0');"

        "document.getElementById('hb_prod').textContent = state.hb_prod;"
        "document.getElementById('hb_cons').textContent = state.hb_cons;"
        "document.getElementById('hb_coord').textContent = state.hb_coord;"
        "document.getElementById('hb_resp').textContent = state.hb_resp;"

        "} catch(error) {"
        "console.log('Monitor update failed');"
        "}"
        "}"

        "setInterval(poll, 1000);"
        "poll();"
        "</script>"

        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

 static esp_err_t handle_state(httpd_req_t *req)
{
    UBaseType_t depth = uxQueueMessagesWaiting(data_q);
    EventBits_t bits = xEventGroupGetBits(evt_group);

    char buf[256];

    int n = snprintf(
        buf,
        sizeof(buf),
        "{\"queue_depth\":%u,"
        "\"event_bits\":%u,"
        "\"hb_prod\":%lu,"
        "\"hb_cons\":%lu,"
        "\"hb_coord\":%lu,"
        "\"hb_resp\":%lu}",
        (unsigned)depth,
        (unsigned)bits,
        (unsigned long)hb_prod,
        (unsigned long)hb_cons,
        (unsigned long)hb_coord,
        (unsigned long)hb_resp
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, n);

    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = HTTP_PORT;
    cfg.core_id = 0;                    /* networking on Core 0 */
    cfg.task_priority = 5;
    cfg.stack_size = 8192;

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &cfg) == ESP_OK)
    {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = handle_root,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t state = {
            .uri = "/state",
            .method = HTTP_GET,
            .handler = handle_state,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &state);

        ESP_LOGI(TAG, "HTTP server started on port %d", HTTP_PORT);
    }
    else
    {
        ESP_LOGE(TAG, "HTTP server failed to start");
    }

    return server;
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t base,
                               int32_t id,
                               void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting...");
        esp_wifi_connect();
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;

        ESP_LOGI(TAG,
                 "Got IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        start_webserver();
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "[webmon] esp_wifi_start completed");
}




static void webmonitor_task(void *arg)
{
    ESP_LOGI(TAG, "[webmon] Starting avionics web monitor");

    wifi_init_sta();

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#else
/* ---------- Serial monitor task (Core 0)  [USE_WEBSERVER = 0] ----------
 * Provided and working. Prints the same state the web monitor will show, so the
 * pipeline is observable in Wokwi with no Wi-Fi. This is your baseline; the web
 * monitor (USE_WEBSERVER=1) renders the identical fields over HTTP.
 */
static void serial_monitor_task(void *arg)
{
    for (;;)
    {
        int64_t start = esp_timer_get_time();

        UBaseType_t depth = uxQueueMessagesWaiting(data_q);
        EventBits_t bits = xEventGroupGetBits(evt_group);

        ESP_LOGI(TAG,
                 "[monitor] q_depth=%u evt=0x%02x "
                 "hb: prod=%lu cons=%lu coord=%lu resp=%lu",
                 (unsigned)depth,
                 (unsigned)bits,
                 (unsigned long)hb_prod,
                 (unsigned long)hb_cons,
                 (unsigned long)hb_coord,
                 (unsigned long)hb_resp);

        ESP_LOGI(TAG,
                 "[WCET] Producer = %lld us",
                 producer_wcet_us);

        ESP_LOGI(TAG,
                 "[WCET] Consumer = %lld us",
                 consumer_wcet_us);

        if (last_item_valid)
        {
            ESP_LOGI(TAG,
                     "[monitor] last_item: seq=%lu sensor=%d altitude=%d ft time=%lu ms",
                     (unsigned long)last_item.sequence,
                     last_item.sensor_id,
                     last_item.value,
                     (unsigned long)last_item.timestamp_ms);
        }

        int64_t end = esp_timer_get_time();

        if ((end - start) > monitor_wcet_us)
        {
            monitor_wcet_us = end - start;
        }

        ESP_LOGI(TAG,
                 "[WCET] Monitor = %lld us",
                 monitor_wcet_us);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif /* USE_WEBSERVER */

/* ---------- app_main ---------- */
void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "==== App 5 [avionics] starting — IPC pipeline ====");

#if USE_WEBSERVER
    ESP_LOGI(TAG, "Monitor: WEB (USE_WEBSERVER=1) — implement webmonitor_task (Core 0)");
#else
    ESP_LOGI(TAG, "Monitor: SERIAL (USE_WEBSERVER=0) — Core-0 summary once/sec, no Wi-Fi");
#endif

    /* TODO: pick queue length + item size. Defend in README.
     * Hint: producer at 20 Hz, consumer at unknown rate — what burst size? */
    data_q = xQueueCreate(/*depth=*/ 8, /*item size=*/ sizeof(avionics_data_t));

    evt_group = xEventGroupCreate();

    /* Tasks on Core 1 (real-time plane). 4096-byte stacks: any task that calls
     * ESP_LOGI needs headroom for the vprintf formatting (2048 overflows). */
    xTaskCreatePinnedToCore(responder_task,   "cockpit_alert",   4096, NULL, 12, &responder_handle, APP_CPU_NUM);
    xTaskCreatePinnedToCore(producer_task,    "sensor_tx",       4096, NULL,  8, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(consumer_task,    "flight_data",     4096, NULL,  8, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(coordinator_task, "telemetry_coord", 4096, NULL,  9, NULL, APP_CPU_NUM);
   
    /* Observability plane on Core 0 (networking plane) */
#if USE_WEBSERVER
    xTaskCreatePinnedToCore(webmonitor_task,    "webmon",  8192, NULL, 4, NULL, PRO_CPU_NUM);
#else
    xTaskCreatePinnedToCore(serial_monitor_task, "monitor", 4096, NULL, 4, NULL, PRO_CPU_NUM);
#endif

    /* Button ISR */
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&cfg);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL);
}
