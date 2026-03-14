# FieldTunnel

**Bridge the gap. Every protocol.**

FieldTunnel is a family of wireless BAS protocol gateways by [DHC Automation](https://dhcautomation.com). Each model bridges field-bus protocols (Modbus RTU, BACnet MS/TP) to IP networks (Modbus TCP, BACnet IP) over WiFi or Ethernet, with native [SlateBAS](https://slatebas.com) integration.

## Product Line

| Model | Name | Interface | RS485 | Status |
|-------|------|-----------|-------|--------|
| W1 | FieldTunnel Air | WiFi | 1x | Available |
| W2 | FieldTunnel Air Duo | WiFi | 2x | Coming Soon |
| E1 | FieldTunnel Pro | Ethernet + WiFi | 1x | Coming Soon |
| L1 | FieldTunnel LON | LON + WiFi | -- | Coming Soon |

## Key Specs

- < 2 ms bridge latency
- 247 nodes per bus
- 9-24V DC power
- OTA firmware updates
- SlateBAS native

## Repository Structure

```
hardware/          Hardware design files and notes
  proto-atom/      ATOM Echo + Tail485 prototype
  ft1-esp32s3/     ESP32-S3 + MAX3485 v1 product
firmware/          ESP-IDF firmware projects
  proto-atom/      Prototype firmware (ESP32)
  ft1-esp32s3/     Production firmware (ESP32-S3)
website/           fieldtunnel.com marketing site
docs/              Product documentation
```

## Development

Firmware is built with [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/).

```bash
cd firmware/proto-atom
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## License

Proprietary -- DHC Automation 2026
