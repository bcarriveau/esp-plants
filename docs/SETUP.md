# Setup and provisioning

## 1. Build the T5 hub

Open only:

```text
firmware/t5-hub
```

in VS Code / PlatformIO.

Build and flash it.

The T5 and XIAO projects intentionally use different toolchains; do not turn the
repository root into one combined PlatformIO project.

## 2. Enter T5 setup mode

Use the T5 function/side button to enter setup mode.

The T5 creates a local network named similar to:

```text
PlantMonitor-xxxx
```

The setup address is:

```text
http://192.168.4.1
```

The setup password is generated/stored by the T5.

## 3. Save home Wi-Fi

From the setup page:

1. enter the home Wi-Fi SSID,
2. enter the Wi-Fi password,
3. save.

The T5 persists these values in NVS.

The password is not intended to be displayed back as a convenience value.

## 4. Build the XIAO sensor

Open only:

```text
firmware/xiao-soil-sensor
```

Build and flash it.

An XIAO with no saved network configuration enters its bounded nearby
provisioning window.

## 5. Provision a sensor

Keep the XIAO near the T5 while the T5 is in setup mode.

The T5 should discover the sensor by stable sensor ID.

Use:

```text
Send Wi-Fi to Sensor
```

for the selected sensor.

The T5 sends the saved home-Wi-Fi configuration over the provisioning channel.

The XIAO stores the configuration in NVS and restarts.

## 6. Return the T5 to normal mode

Finish setup / return the T5 to home Wi-Fi.

Normal expected network behavior:

```text
XIAO joins home Wi-Fi
XIAO sends UDP v3 reading
T5 receives on UDP 42100
T5 returns application ACK
```

The T5 does not require a fixed IP because the sensor uses LAN broadcast for the
reading and learns the ACK source dynamically.

## 7. Rename the plant

Use the T5 setup page to assign the discovered sensor a human-readable plant
name.

The mapping is stored on the T5.

Changing a plant name should not require:

- reflashing the XIAO,
- changing the protocol,
- changing sensor code.

## 8. Calibrate the physical probe

Calibration is optional until you are ready to establish real dry/wet endpoints.

See [CALIBRATION.md](CALIBRATION.md).

## Normal use

### Scheduled unattended wake

The XIAO wakes, measures locally, and only starts Wi-Fi when a report condition
is met.

### Manual top-button wake

The XIAO enters a two-minute green-LED service mode with 5-second live sampling.

This is the preferred mode while physically moving, watering, wiping, or
calibrating the sensor.

## Resetting XIAO Wi-Fi provisioning

Do not use the old "hold during boot" behavior.

Current behavior:

1. wake the sensor normally,
2. while it is already awake in green service mode,
3. deliberately hold the top button for 10 seconds,
4. firmware erases saved home-Wi-Fi provisioning,
5. sensor returns to provisioning behavior.

## Troubleshooting

### XIAO sends but receives no ACK

Check that:

- the T5 is in normal home-Wi-Fi mode,
- the T5 is not still in its local setup/provisioning AP mode,
- both devices are on the same LAN/broadcast domain,
- UDP port 42100 is not blocked by network isolation.

### Serial port disappears after a reading

A native-USB ESP32-C6 can disappear from the host when it enters deep sleep.
That alone is not proof of a crash.

Use a service/debug mode when continuous serial visibility is required.

### Sensor is stable but appears quiet

That is expected in adaptive mode. A stable scheduled wake can measure and
return to sleep without turning Wi-Fi on.

The heartbeat prevents indefinite silence.
