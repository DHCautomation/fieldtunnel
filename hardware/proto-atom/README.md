# Proto-ATOM: ATOM Echo + Tail485 Prototype

## Hardware

- **MCU:** M5Stack ATOM Echo (ESP32-PICO-D4, 4 MB flash)
- **RS485:** Tail485 add-on (SP485EEN-L, auto-direction)
- **Pins:** TX=G26, RX=G32
- **Reserved (I2S audio):** G19, G22, G23, G33

## Status

Arduino prototype validated Modbus TCP-to-RTU bridging on this hardware.
This configuration is used for development and testing only — not for production.

## Notes

- SP485EEN-L handles RS485 direction automatically (no DE/RE pin needed)
- USB-C provides power and serial console at 115200 baud
- WiFi STA + AP fallback tested and working
