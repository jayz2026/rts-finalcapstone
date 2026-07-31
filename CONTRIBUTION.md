# Contributing

Thank you for your interest in contributing to this project.

This repository contains my final capstone project for Real-Time Systems at the University of Central Florida.

## Development Guidelines

- Use ESP-IDF coding conventions.
- Keep FreeRTOS tasks modular and well documented.
- Preserve deterministic timing behavior.
- Verify all IPC mechanisms (queues, event groups, and direct task notifications) after making changes.
- Measure WCET after modifying task logic.

## Testing

Before submitting changes:

- Verify the project builds successfully.
- Test the application in Wokwi or on compatible ESP32 hardware.
- Ensure the producer, consumer, coordinator, and responder tasks execute correctly.
- Confirm graceful degradation and heartbeat monitoring still operate correctly.

