# App 5 scaffold — dual-core IPC pipeline

Scaffold level: **~65%** (the pipeline logic is yours; the observability infrastructure is provided).

## What's wired

- Queue (`data_q`), event group (`evt_group`), task notification target (`responder_handle`)
- Producer / consumer / coordinator / responder task skeletons on Core 1
- Button ISR &rarr; direct task notification path
- Per-task heartbeat counters (`hb_prod` / `hb_cons` / `hb_coord` / `hb_resp`)
- A Core-0 monitor behind one `#define`: a working serial monitor (`USE_WEBSERVER=0`) or the web-monitor stub you complete (`USE_WEBSERVER=1`)

## Run modes (`USE_WEBSERVER`)

`USE_WEBSERVER` picks the Core-0 observability plane; the Core-1 pipeline is identical either way. One line at the top of `main.c`:

```c
#define USE_WEBSERVER 0   /* 0 = serial monitor (provided), 1 = web monitor (you build) */
```

- `USE_WEBSERVER = 0` — **serial monitor, provided and working.** Prints queue depth (`uxQueueMessagesWaiting`), event-group bits, and the four heartbeats once a second. No Wi-Fi, so the pipeline runs in Wokwi out of the box. This is your baseline.
- `USE_WEBSERVER = 1` — **web monitor, your deliverable.** Runs `webmonitor_task` instead, where you port App 1's HTTP server and render the same fields over HTTP.

Start on `0` to get the pipeline moving in the simulator, then flip to `1` once the web monitor is in.

## What you build

1. **Producer body** — themed data items, queue send with a back-pressure policy
2. **Consumer body** — receive with timeout, themed processing (delete the placeholder `vTaskDelay` once your `xQueueReceive` blocks for you)
3. **Web monitor** — port App 1's HTTP code into `webmonitor_task` (the `USE_WEBSERVER=1` path), add live queue depth + event-bit readout
4. **Queue sizing** — pick depth + item size, and defend it
5. **Theme** — names, log messages, the meaning of "a data item"

The coordinator &rarr; responder rendezvous is already wired (just verify it), and the heartbeat counters are already provided and fed into the serial monitor.

## Explain in README

- **Queue depth**: how did you size it? Compute the worst-case burst your producer can deliver before the consumer catches up. Show the math.
- **Back-pressure policy**: queue full means... drop oldest? drop newest? block producer? log + drop? Justify.
- **Event-group vs N semaphores**: explain why the event group is the better fit for the producer-consumer rendezvous.
- **Direct notification vs binary semaphore**: measure wake latency on both paths (you have the App 3 helper). Numbers in your README.

## Where to copy from

- App 1's HTTP server: reuse `wifi_init_sta()`, `handle_root()`, `start_webserver()` patterns - drop them into `webmonitor_task` under `USE_WEBSERVER=1`
- App 3's latency-measurement helper: drop it in for the notification path

Heartbeat counters are already provided (`hb_prod` / `hb_cons` / `hb_coord` / `hb_resp`), so you can skip that copy from App 2 and go straight to reading them in your web page/terminal.

## Engineering analysis prompts (graded, see above for full question)

1. Why pin the web server to Core 0 vs Core 1? (The scaffold puts the monitor on Core 0 - defend or challenge that.)

The terminal monitor was pinned to Core 0 so that monitoring tasks do not interfere with the real-time avionics pipeline running on Core 1. The producer, consumer, coordinator, and responder all execute on Core 1 and are responsible for processing sensor data with predictable timing. By placing the terminal monitor on Core 0, logging and monitoring operations run independently of the real-time pipeline, reducing scheduling contention and helping maintain consistent task execution. If the monitor were placed on Core 1, its periodic logging could compete with the real-time tasks for CPU time, increasing latency, introducing timing jitter, delaying telemetry processing, and potentially causing the queue to build up more quickly.

2. Queue depth  & pressure - why did you select the size you did? (AI use)

The queue depth was selected based on the producer's data generation rate and the amount of temporary backlog the system should tolerate. The producer generates one data packet every 50 ms, which corresponds to a rate of 1 / 0.050 s = 20 packets/s (20 Hz). The queue was configured with a depth of 8 packets, allowing it to absorb 8 packets ÷ 20 packets/s = 0.4 s (400 ms) of temporary congestion before overflowing. This provides enough buffering to handle brief delays in the consumer task while preventing excessive memory usage or long processing delays. If the queue remains full for more than the producer's 10 ms send timeout, the newest packet is dropped and a warning is logged, ensuring the real-time producer does not block indefinitely.


3. Explain the pros/cons to Event group vs N semaphores.

An event group is better when one task must wait for several independent conditions to become true, such as waiting for both the sensor-read and fusion stages to finish before preparing telemetry. Multiple status bits can be stored in one event group, and the coordinator can wait for all required bits at once. Separate semaphores are better when each event represents ownership of a resource, when events must be counted, or when different tasks need to block on separate signals. In this system, the event group is simpler because the coordinator only needs to verify that multiple processing stages are complete, rather than manage several individual semaphores.


4. Direct notification vs binary semaphore — with measured numbers.

Direct task notifications were selected because they provide the lowest-overhead mechanism for one-to-one task signaling in FreeRTOS. Unlike binary semaphores, direct notifications do not require a separate kernel object, resulting in lower memory usage and faster wake-up times. Binary semaphores are more appropriate when multiple tasks need to share the same synchronization object or when ownership of a resource must be managed. In this project, the coordinator only needed to signal a single responder task, making direct task notifications the most efficient IPC primitive.

## Viewing the web monitor (`USE_WEBSERVER=1`, if you implement it on real hw)

The scaffold ships `webmonitor_task` as a stub. After you port App 1's HTTP code in and flip `USE_WEBSERVER` to `1`, the workflow is identical to App 1/2:

- Wi-Fi connects &rarr; serial monitor prints `Got IP: 10.13.37.x`
- A network indicator appears in Wokwi's simulator panel once port 80 is up
- Click "Open in new tab" to view the page

For App 5 specifically, the page should show live **queue depth** (`uxQueueMessagesWaiting(data_q)`), **event-group bit state** (`xEventGroupGetBits(evt_group)`), and the per-task heartbeats. The auto-refresh interval is your choice &mdash; 1 Hz is reasonable; faster than that and you'll see your own HTTP handler showing up in the latency measurements.

Until the web monitor is implemented, run on `USE_WEBSERVER=0`: the serial monitor prints `[monitor] q_depth=… evt=… hb: prod=… cons=… coord=… resp=…` once a second, and every `[responder] notified (count=N)` line confirms the IPC pipeline is moving.

## Honor code

You're now reusing infrastructure from Apps 1&ndash;3. That's NOT cheating &mdash; that's engineering. Cite where each block came from in the README.
