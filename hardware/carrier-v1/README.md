# Carrier v1 design

Carrier v1 is a low-voltage board for the XIAO ESP32-C5. It proves the modular
design with one relay output, one button input, a sensor connector and battery
power. **This is a design specification. No schematic or PCB exists yet.**

## Scope

| In v1 | Left out of v1 |
|---|---|
| 5 V USB-C or 5 V screw terminal input | Mains input |
| One relay driver for low-voltage loads | Switching mains loads |
| One button input | Zigbee |
| Qwiic-style I2C sensor connector | Backup battery charging beyond the XIAO's own charger |
| Single-cell Li-ion or LiPo through the XIAO battery pads | Non-rechargeable cells |
| Carrier ID EEPROM | |

Mains goes on a later carrier, after a safety review. It needs a pre-certified
isolated AC-DC module, a fuse, creepage and clearance rules, and an enclosure.

## Pin allocation

| XIAO pin | GPIO | Use | Notes |
|---|---|---|---|
| D0 | 1 | Spare analog input | Low-power capable |
| D1 | 0 | Button input | Low-power capable, used by `button-sleepy` |
| D2 | 25 | Relay driver | Used by `relay-router` |
| D3 | 7 | Spare GPIO | |
| D4 | 23 | I2C SDA | Shared by the sensor connector and ID EEPROM |
| D5 | 24 | I2C SCL | Shared by the sensor connector and ID EEPROM |
| D6 | 11 | Debug UART TX | Test pad |
| D7 | 12 | Debug UART RX | Test pad |
| D8 to D10 | 8, 9, 10 | Spare SPI | Expansion header |
| n/a | 6, 26 | Battery sensing | On the XIAO, reserved |
| n/a | 27 | User LED | On the XIAO, reserved |
| n/a | 28 | Boot button | On the XIAO, reserved |

## Circuit requirements

- **Relay:** drive the coil through a logic-level MOSFET with a flyback diode.
  Add a pull-down on the gate so the relay stays off during reset and boot.
- **Button:** connect to ground and use the internal pull-up. Add ESD protection
  if the button is wired off the board.
- **I2C:** fit pull-ups on the carrier once, with a jumper to disable them.
- **Sensor power:** switch the sensor connector's 3.3 V rail with a load switch.
  Otherwise, sensors drain the battery while the device sleeps.
- **ID EEPROM:** a small I2C EEPROM stores the carrier type and revision, so
  firmware can refuse to drive a carrier it does not match.
- **Power input:** add reverse-polarity and over-voltage protection on the screw
  terminal. Check how the XIAO shares power between 5 V, USB and battery before
  connecting them together.
- **Antenna:** keep the U.FL antenna and its cable clear of the relay and
  ground planes. Use an antenna rated for the bands in use.

## Device roles

| Role | Power | Thread behaviour | Examples |
|---|---|---|---|
| Router | Mains, optional short backup battery | Always on, routes for others | Relays, plugs |
| Sleepy | Battery | Sleeps, never routes | Buttons, sensors |

A router on backup battery should keep working only for short outages. Decide
what relays do during an outage before designing the backup circuit.

## Checks before ordering a PCB

1. Confirm Thread works on the XIAO C5 with the supplied antenna.
2. Measure sleep current on a real board with a power analyser.
3. Confirm the relay stays off during reset, boot, OTA and rollback.
4. Confirm D1 wakes the device from light sleep.
5. Check every part's current availability, as none have been selected yet.
