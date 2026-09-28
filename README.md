# Smart Agriculture Monitor

Autonomous IoT system for agricultural monitoring aimed at efficient irrigation management in citrus crops

The system measures soil moisture and temperature from two soil probes, as well as ambient temperature and humidity. Data is transmitted to a cloud platform via a SIM7070G cellular modem, and Deep Sleep is utilized to minimize power consumption.

<p align="center">
  <img src="https://github.com/user-attachments/assets/1867d891-9a4f-42f5-8e89-9db010226c25" width="350" alt="Foto del Sensor 1">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://github.com/user-attachments/assets/6e71adc1-9956-45c8-982f-e83d969d333d" width="350" alt="Foto del Sensor 2">
<img width="500" alt="Thniger io Dashboard" src="https://github.com/user-attachments/assets/dacd25cc-1d1b-458d-bb76-d93c26436a30" />
</p>


## Features

- XIAO ESP32-C6 as the main controller[cite: 1, 2].
- Two industrial soil moisture and temperature probes via RS485 / Modbus-RTU[cite: 1, 2].
- DHT22 environmental sensor[cite: 1, 2].
- SIM7070G cellular modem for cellular connectivity[cite: 1, 2].
- Hologram IoT SIM[cite: 1, 2].
- HTTP transmission via TCP socket to Thinger.io[cite: 1, 2].
- Powered by a 3W solar panel and an integrated 4000 mAh battery[cite: 1, 2].
- Peripheral power switching via relay and MOSFET[cite: 1, 2].
- RTC memory to preserve measurements between cycles[cite: 1, 2].
- Periodic 20-minute cycle with Deep Sleep[cite: 1, 2].

## Architecture

```text
                 3 W Solar Panel
                       │
               4000 mAh Battery
                       │
                 XL6019 / 5 V
                       │
                XIAO ESP32-C6
          ┌────────────┼─────────────┐
          │            │             │
        RS485         UART          GPIO
          │            │             │
   ┌──────┴──────┐  SIM7070G       DHT22
   │             │      │
Probe 1       Probe 2  LTE Network
                            │
                        Internet
                            │
                       Thinger.io
```[cite: 1, 2]

## Operating Cycle

```text
Wake-up
   ↓
Power peripherals
   ↓
Initialize sensors and modem
   ↓
Read DHT22
   ↓
Read Modbus Probe 1
   ↓
Read Modbus Probe 2
   ↓
Register / Check network
   ↓
Build JSON payload
   ↓
HTTP POST over TCP
   ↓
Close socket and power off peripherals
   ↓
Deep Sleep for 20 min
```[cite: 1, 2]

## Modbus Sensors

The probes are connected in parallel on the RS485 bus[cite: 1, 2]. To avoid Modbus collisions, each sensor must have a unique identifier[cite: 1, 2].

The report documents changing one of the sensors from address `0x01` to `0x02` using[cite: 1, 2]:

```text
01 06 07 D0 00 02 B2 DB
```[cite: 1, 2]

Register `0x07D0` corresponds to the sensor's Modbus ID according to the project documentation[cite: 1, 2].

The documented reading uses function `0x03` and two consecutive registers[cite: 1, 2]. Received values are interpreted with a scale factor of `0.1` as described in the implementation[cite: 1, 2].

## Pinout

The report contains two distinct pin descriptions across its explanatory sections[cite: 1, 2]. For this reason, **a single assignment is not silently forced**[cite: 1, 2]. The firmware configuration table centralizes the pins so they can be adjusted to match the actual prototype wiring[cite: 1, 2].

Edit `firmware/include/config.h` before compiling[cite: 1, 2].

## IoT Platform

The final report describes sending a JSON payload via HTTP over TCP to Thinger.io[cite: 1, 2]. The firmware in this version maintains that architecture[cite: 1, 2].

Credentials and endpoints are not included in the repository[cite: 1, 2]. They must be configured via `config.h` or an equivalent mechanism prior to use[cite: 1, 2].

## Power Consumption and Documented Results

The report details stable operation with transmissions every 20 minutes and approximately 1.5 kB per transmission[cite: 1, 2]. This translates to roughly 150 kB/day and 4.25 MB/month, with an estimated SIM cost of ~2.5 USD/month under the described conditions[cite: 1, 2].

## Repository Structure

```text
smart-agriculture-monitor/
├── firmware/
│   ├── include/
│   │   ├── config.h
│   │   ├── modbus.h
│   │   ├── modem.h
│   │   └── telemetry.h
│   └── src/
│       ├── main.cpp
│       ├── modbus.cpp
│       ├── modem.cpp
│       └── telemetry.cpp
│   └── platformio.ini
├── docs/
│   ├── architecture.md
│   ├── field-deployment.md
│   ├── modbus.md
│   └── power-management.md
├── hardware/
│   ├── pinout.md
│   └── wiring.md
├── tests/
│   └── test_modbus_crc.cpp
├── LICENSE
└── README.md
```[cite: 1, 2]

## Getting Started

1. Adjust the pins in `firmware/include/config.h` to match your hardware prototype[cite: 1, 2].
2. Enter your APN, credentials, and cloud service endpoint[cite: 1, 2].
3. Confirm that both probes have different Modbus IDs[cite: 1, 2].
4. Compile using PlatformIO[cite: 1, 2].
5. Test first without enabling Deep Sleep[cite: 1, 2].
6. Validate sensor readings and modem responses[cite: 1, 2].
7. Validate the HTTP POST on Thinger.io[cite: 1, 2].
8. Enable the 20-minute cycle and perform a field test[cite: 1, 2].

## Documented Future Improvements

The report proposes the future adoption of NB-IoT and switching the transport layer to UDP[cite: 1, 2]. This possibility is not part of the validated LTE operation of the final project[cite: 1, 2].

## License

MIT. See `LICENSE`[cite: 1, 2].
