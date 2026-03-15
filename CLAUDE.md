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

## Current Firmware State (v0.3.3)
- Modbus TCP Gateway: working
- Raw TCP Tunnel: working
- BACnet MS/TP: UART stack working (needs real device test)
- SunSpec Solar: coming soon
- Context-aware UI per protocol: working
- Mode switch confirmation dialog: working
- Unsaved changes warning: working
- BACnet extended settings: working
  (Device ID, Network, Slot/Reply/Usage timeouts, Retry, Name)
- Modbus extended settings: working
  (Max TCP Clients, RTU Retry)
- NAT bridge + dynamic DNS: working
- OTA firmware updates: working
- Auto-release pipeline: working
  flash.ps1 -> bin + latest.json + index.html -> git push
- Check for Updates -> one-click Update Now: working

## Device Status
Device 1 (F91C): 192.168.10.125, USB COM4, v0.3.3
Device 2 (3C40): 192.168.10.130, 12V WiFi, v0.3.2
  -> OTA Device 2 to v0.3.3 tomorrow

## Pending Work
- [ ] OTA Device 2 to v0.3.3
- [ ] Temco RS485 test
      Device 2 wired to Temco IDs 21+22 via Tail485
      Test 1: Raw TCP Tunnel mode
        Open Temco software
        Point at 192.168.10.130:502
        Connect device ID 21 and 22
        Try firmware update through gateway
      Test 2: Modbus TCP mode
        Open CAS Scanner
        Point at 192.168.10.130:502
        Scan IDs 21 and 22
        Read all registers
        Screenshot + note register map
        This completes v0.4.0
- [ ] BACnet MS/TP real device test
- [ ] Device discovery page (scan bus for slave IDs)
- [ ] SunSpec solar auto-discovery
- [ ] Compatible devices section on main website
- [ ] PCB design (separate chat in progress)
      KiCad schematic + layout
      JLCPCB SMT target <$80 CAD/5 boards
      Air W1 (WiFi) + Pro E1 (WiFi+ETH) from one PCB
      Hammond or 3D print PC-ABS enclosure

## Tomorrow Session Start
Paste this at start of Claude Code session:

"Read CLAUDE.md first.
 Device 1 (F91C) on USB COM4 running v0.3.3.
 Device 2 (3C40) at 192.168.10.130 running v0.3.2.

 Step 1: OTA Device 2 to v0.3.3
   Open 192.168.10.130 -> Firmware -> Check for Updates
   Hit Update Now
   Confirm v0.3.3 after reboot

 Step 2: Temco RS485 test
   Switch Device 2 to Raw TCP Tunnel mode
   Connect Temco software to 192.168.10.130:502
   Test device IDs 21 and 22
   Then switch to Modbus TCP
   CAS Scanner -> scan -> read registers
   Report register map

 This is v0.4.0 milestone."

## ESP-IDF Version
v5.5.1 at C:\Users\admin\esp\v5.5.1\esp-idf
