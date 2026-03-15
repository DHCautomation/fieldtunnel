# FieldTunnel — Claude Code Conventions

## Project Overview
Multi-protocol BAS gateway firmware + website.
GitHub: github.com/DHCautomation/fieldtunnel
Website: fieldtunnel.com (GitHub Pages from repo root)

## Hardware
- Device 1: ATOM Echo, MAC F91C, COM4 (dev device)
- Device 2: ATOM Echo, MAC 3C40, WiFi only (field test)
- RS485: TX=G26, RX=G32, SP485EEN-L auto-direction
- DO NOT USE: G19, G22, G23, G33 (I2S audio)

## Firmware Location
C:\dev\fieldtunnel\firmware\proto-atom\

## Flash Scripts — USE THESE IN ORDER:
1. flash.ps1        — normal build + flash (USE THIS)
2. flash_only.ps1   — flash existing bin only (fastest)
3. flash_clean.ps1  — DANGER: wipes partition table
                      only use when changing sdkconfig
                      Device 2 needs USB reflash after

## MANDATORY AFTER EVERY FLASH:
flash.ps1 auto-runs these — verify they happened:
1. Copy bin to releases/fieldtunnel-X.X.X.bin
2. Update releases/latest.json with new version
3. git add releases/ firmware/
4. git commit with version in message
5. git push

If auto-release failed, run manually:
  $ver = "X.X.X"
  cp build/fieldtunnel-proto.bin ../../releases/fieldtunnel-$ver.bin
  Update releases/latest.json
  git add . && git commit -m "release: vX.X.X" && git push

## Version Bumping Rules
- Bug fix:     0.3.1 → 0.3.2
- New feature: 0.3.x → 0.4.0
- Breaking:    0.x.x → 1.0.0
- Always bump FW_VERSION in main/gateway.h
- Always update releases/latest.json
- Always commit the .bin to releases/

## Device Management
- Device 1 (F91C): USB COM4, dev + testing
- Device 2 (3C40): 192.168.10.130, field test
  → Update Device 2 via OTA only (never USB again)
  → Download bin from releases/
  → Upload via web UI at 192.168.10.130

## OTA Update Flow
1. Flash Device 1 via USB (flash.ps1)
2. Auto-release copies bin + updates latest.json
3. git push → GitHub Pages updates fieldtunnel.com
4. Device 2 Check for Updates → sees new version
5. Device 2 downloads from fieldtunnel.com
6. Device 2 reboots with new firmware

## Website
- Main page: index.html (repo root)
- Firmware page: releases/index.html
- Version manifest: releases/latest.json
- Bin files: releases/fieldtunnel-X.X.X.bin
- After every release update releases/index.html
  to show new version in the releases table

## Protocol Modes
- Mode 0: Modbus TCP Gateway (default)
- Mode 1: Raw TCP Tunnel (Temco firmware etc)
- Mode 2: BACnet MS/TP (UART stack, UDP 47808)
- Mode 3: SunSpec Solar (coming soon)

## FreeRTOS Tasks
- wifi_task:        APSTA, NAT bridge, mDNS
- rs485_task:       UART1 RS485, yields when mode==2
- tcp_server_task:  Port 502, routes by mode
- http_server_task: Port 80, REST API + SPIFFS
- bacnet_task:      UDP 47808 + MS/TP, mode==2 only

## Pending Work
- [ ] Temco RS485 test (IDs 21+22, temp+RH)
- [ ] BACnet MS/TP real device test
- [ ] SunSpec solar auto-discovery
- [ ] Device discovery page (scan bus for IDs)
- [ ] BACnet device template library

## ESP-IDF Version
v5.5.1 at C:\Users\admin\esp\v5.5.1\esp-idf
