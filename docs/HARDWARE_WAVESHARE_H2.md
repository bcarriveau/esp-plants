# Waveshare + M5Stack Gateway H2 hardware bring-up

## Confirmed board interfaces

### Waveshare ESP32-S3-Touch-LCD-7

- Native USB uses ESP32-S3 GPIO19/20 and is kept for firmware upload/debug.
- UART1 and UART2 are the same ESP32-S3 UART0 on GPIO43/44, selected by the
  physical UART switch.
- UART2 is the direct external UART header.
- The I2C external header has a physical 3.3 V / 5 V level/power selector.
- The existing ESP Aircraft Radar project already proves the display/touch
  stack and panel timing used by `firmware/waveshare-hub`.

### M5Stack Unit Gateway H2 (U195)

- ESP32-H2-MINI-1-N2, 2 MB flash.
- Grove/Port C requires 5 V power and carries UART.
- M5Stack's H2 NCP documentation configures the module-side UART as RX GPIO23
  and TX GPIO24; this bring-up firmware uses those same H2 pins.
- The H2 Type-C port is kept for H2 flashing and H2 debug output.

## Development USB rule

For the Waveshare, use the **native ESP32-S3 USB-C** (GPIO19/20 path) for both
firmware upload and `Serial` diagnostics. Do not use the CH343/UART1 Type-C port
as the normal programming/monitor connection. UART0 GPIO43/44 is reserved for
UART2 PlantLink to the H2 while ESP PLANTS is running.

For the M5Stack H2, use its own Type-C port for H2 firmware upload and USB debug.
Its Grove UART RX/TX remains dedicated to PlantLink.

## Runtime cable plan

Do **not** power the M5 H2 from the Waveshare UART2 3.3 V pin.

Use two Waveshare pigtails and the M5 Grove lead to make one removable harness:

```text
Waveshare UART2 TXD (S3 GPIO43)  --->  M5 H2 UART_RX
Waveshare UART2 RXD (S3 GPIO44)  <---  M5 H2 UART_TX
Waveshare I2C VCC, selector=5V   --->  M5 H2 5V
Waveshare I2C GND                --->  M5 H2 GND
```

Set the Waveshare UART selector to **UART2** for normal ESP PLANTS operation.

The two computers remain independently flashable:

```text
PC USB-C -> Waveshare native USB -> Waveshare firmware + USB serial debug
PC USB-C -> M5 H2 Type-C         -> H2 firmware + H2 serial debug
```

## Important M5 cable-color warning

Current M5Stack Unit Gateway H2 documentation labels Port C as:

```text
Black = GND
Red   = 5V
Yellow = UART_RX
White  = UART_TX
```

An older M5 product page showed Yellow/White reversed. Do not trust wire color
alone when the module arrives. Verify the current case/PCB/pin labeling or
continuity before making the final crossover harness.

## First physical test

1. Flash the Waveshare over its native USB port.
2. Flash the H2 over its own Type-C port.
3. Set the Waveshare I2C selector to 5 V.
4. Set the Waveshare UART selector to UART2.
5. Connect 5V/GND plus crossed TX/RX.
6. Power the Waveshare.
7. Confirm the display changes H2 LINK from `WAITING FOR H2` to `ONLINE`.
8. Confirm both USB serial logs remain usable independently.
9. Press `ADD SENSOR` and confirm H2 debug reports a 120-second permit-join
   window.
10. Only then pair one ZG-303Z and capture its real Zigbee/Tuya traffic.
