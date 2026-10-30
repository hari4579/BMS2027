# APARD32690 – BMS Master Controller

Embedded BMS master-controller firmware developed for the **7.2 kWh, 40S12P battery pack**. The project uses the Analog Devices AD-APARD32690-SL platform to interface with an ADBMS6830 battery cell-monitoring IC and receive distributed temperature measurements over CAN.

The master controller combines cell-voltage acquisition and temperature data collection, bringing both measurement paths together for monitoring through the serial console.

## 1. Project Overview

The APARD32690 acts as the central controller in the distributed Battery Management System.

It performs two primary functions:

- **Cell-voltage monitoring:** Communicates with the ADBMS6830 through SPI to initiate cell-voltage conversion and read cell-voltage register groups.
- **Temperature monitoring:** Receives temperature telemetry from distributed STM32-based temperature-monitoring boards over CAN1.

The collected data is organized and displayed through the UART console.

### System Configuration

| Parameter | Specification |
|---|---|
| Battery pack energy | 7.2 kWh |
| Battery configuration | 40S12P |
| Total cell positions | 480 |
| BMS master controller | AD-APARD32690-SL |
| Main MCU | MAX32690 |
| Cell-monitoring IC | ADBMS6830 |
| Temperature-monitoring architecture | Distributed STM32 boards |
| Temperature sensing capacity | 75 NTC channels (system design) |
| CAN communication | CAN1, 500 kbps |
| Cell-monitoring interface | SPI1 |
| Debug/console interface | UART0 |

## 2. System Architecture

The master controller receives information from two separate acquisition paths.

### Data Acquisition Architecture

```text
                 7.2 kWh Battery Pack
                       40S12P
                          |
             +------------+------------+
             |                         |
             v                         v
      Cell Voltage Monitoring    Temperature Monitoring
             |                         |
             v                         v
        ADBMS6830              Distributed STM32 Boards
             |                         |
       isoSPI Interface             CAN Bus
             |                       500 kbps
             v                         |
       ADBMS6822                      |
             |                         |
             |                         |
             v                         v
       SPI1 Interface               CAN1 Interface
             |                         |
             +------------+------------+
                          |
                          v
                +-------------------+
                |   APARD32690      |
                |   MAX32690        |
                |                   |
                | BMS Master Logic  |
                +-------------------+
                          |
                          v
                  UART0 Serial Console
                          |
                          v
                Combined Data Display
```

The architecture separates cell-voltage acquisition from temperature acquisition while bringing both data streams together at the master controller.

## 3. Hardware Interfaces

### 3.1 APARD32690 Master Controller

The AD-APARD32690-SL platform uses the MAX32690 microcontroller.

The master firmware handles:
- SPI communication with the battery-monitoring subsystem.
- CAN reception from temperature-monitoring boards.
- Collection and organization of measurement data.
- Serial-console output for monitoring and debugging.

### 3.2 ADBMS6830 Cell Monitoring

The ADBMS6830 is used for battery cell-voltage monitoring.

The firmware implements the command and read sequence required to communicate with the device.

The communication sequence includes:

1. isoSPI wake-up.
2. Cell-voltage ADC conversion command (ADCV).
3. Conversion wait.
4. Cell-voltage register read commands.
5. SPI response collection.
6. Cell-voltage data handling.

The firmware defines the following cell-voltage read commands:

| Command | Opcode |
|---|---|
| RDCVA | 0x0004 |
| RDCVB | 0x0006 |
| RDCVC | 0x0008 |
| RDCVD | 0x000A |
| RDCVE | 0x0009 |
| RDCVF | 0x000B |

The command PEC15 calculation is implemented in firmware.

The source also includes an optional data PEC10 verification feature, controlled through `bms_config.h`.

### 3.3 SPI1 Configuration

SPI1 is used for communication with the battery-monitoring interface.

| Parameter | Configuration |
|---|---|
| Peripheral | SPI1 |
| SPI mode | Mode 0 |
| Clock frequency | 2 MHz |
| Data width | 8 bits |
| Bit order | MSB first |
| Chip select | Manual GPIO |
| CS pin | P1.23 |

**SPI Pin Mapping**

| Signal | APARD32690 Pin |
|---|---|
| SCK | P1.26 |
| MISO | P1.28 |
| MOSI | P1.29 |
| CS | P1.23 |

Chip select is controlled manually through GPIO, allowing the firmware to maintain the required continuous CS-low interval during command and response transfers.

### 3.4 CAN1 Temperature Reception

CAN1 receives temperature telemetry from the distributed STM32 temperature-monitoring boards.

The receiver is configured for:

| Parameter | Configuration |
|---|---|
| Peripheral | CAN1 |
| Bit rate | 500 kbps |
| Frame type | Standard CAN identifier |
| Data length | 8 bytes maximum |
| Operating mode | Normal |
| Reception method | Polling |

The firmware uses an open standard-ID filter and polls the CAN peripheral for incoming messages.

### 3.5 UART0 Debug Interface

UART0 provides the serial-console interface for displaying BMS and temperature-monitoring information.

| Parameter | Configuration |
|---|---|
| Peripheral | UART0 |
| Baud rate | 115200 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | Disabled |
| Flow control | Disabled |

The console displays acquisition progress, SPI debug information, cell-voltage data, and temperature measurements.

## 4. Firmware Architecture

The firmware is organized into three main application components.

| File | Responsibility |
|---|---|
| `main.c` | Main application loop and combined data display |
| `bms_main.c` | ADBMS6830 initialization, SPI communication, and cell-data handling |
| `can_receiver.c` | CAN1 initialization and temperature-frame reception |
| `can_receiver.h` | CAN receiver interface declarations |
| `bms_config.h` | BMS configuration and debugging options |

### 4.1 Main Application – `main.c`

The main application coordinates temperature collection and cell-voltage acquisition.

The application follows a periodic collection sequence:

**Phase 1 – Temperature Collection**

- Clears the temperature-valid flags.
- Polls CAN1 for incoming frames.
- Collects the 13 temperature values.
- Checks whether all channels have been received.
- Uses a timeout if the complete set is not available.

**Phase 2 – Cell-Voltage Acquisition**

- Calls the BMS application.
- Initiates ADBMS6830 communication.
- Collects cell-voltage data.
- Stores the results in the local cell array.

**Phase 3 – Combined Data Display**

- Displays cell-voltage values.
- Displays the corresponding temperature readings.
- Calculates total voltage and temperature summary statistics.
- Prints the results through UART0.

The collection and display sequence is scheduled at approximately one-second intervals.

### 4.2 ADBMS6830 Driver – `bms_main.c`

The BMS driver implements the low-level communication sequence for the ADBMS6830.

Key functions include:

- `adbms_pec15()` – Calculates command PEC15.
- `adbms_pec10()` – Calculates read-data PEC10 when enabled.
- `adbms_wakeup()` – Generates the isoSPI wake-up sequence.
- `adbms_spi_frame()` – Handles command transmission and response reception.
- `adbms_adcv_start()` – Initiates cell-voltage conversion.
- `bms_init()` – Configures the chip-select GPIO.
- `bms_read_cells()` – Executes the cell-voltage acquisition sequence.
- `bms_print_cells()` – Displays cell-voltage data.
- `bms_get_cell_voltage()` – Provides access to an individual cell-voltage value.

The SPI transaction keeps chip select asserted throughout the command and response transfer, following the communication sequence documented in the source.

### 4.3 CAN Receiver – `can_receiver.c`

The CAN receiver collects temperature telemetry into a local 13-channel array.

The received frames are decoded according to the following mapping:

| CAN ID | Temperature Channels |
|---|---|
| 0x200 | CH0–CH3 |
| 0x201 | CH4–CH7 |
| 0x202 | CH8–CH11 |
| 0x203 | CH12 |

Each temperature value is reconstructed from two bytes in big-endian order.

The receiver maintains a validity flag for every channel. The function `can_all_temps_ready()` checks whether all 13 temperature values have been received.

The function `can_temp_to_celsius()` converts the received fixed-point value into Celsius using a 0.1 scaling factor.

## 5. Data Collection and Display

The master controller combines the received temperature array and the cell-voltage array before displaying the results.

The serial output is organized into:

- Cell-voltage measurements.
- Temperature measurements.
- Total pack-voltage summary.
- Average temperature.
- Minimum temperature.
- Maximum temperature.
- Collection-cycle count.

The temperature display currently handles 13 received channels, while the overall battery temperature-monitoring architecture is designed for 75 sensing points.

## 6. Configuration Options

The `bms_config.h` file contains the main BMS driver configuration.

| Macro | Purpose |
|---|---|
| `NUM_DEV` | Number of ADBMS devices in the chain |
| `BMS_ENABLE_PEC_CHECK` | Enables or disables data PEC10 checking |
| `BMS_ENABLE_SPI_DEBUG` | Enables or disables raw SPI TX/RX logging |
| `MEASUREMENTS_FREQ_MS` | Measurement frequency configuration retained from the original example |

The current minimal BMS driver directly uses `NUM_DEV`, `BMS_ENABLE_PEC_CHECK`, and `BMS_ENABLE_SPI_DEBUG`. Some legacy measurement configuration options are retained for documentation but are not connected to the current application loop.

## 7. Development Environment

The project is configured for the Analog Devices MAX32690 platform using the Maxim Microcontroller SDK (MSDK) and Analog Devices CodeFusion Studio.

| Component | Configuration |
|---|---|
| Development platform | Analog Devices CodeFusion Studio |
| Firmware platform | MSDK |
| Target MCU | MAX32690 |
| Board | AD-APARD32690-SL |
| CPU core | ARM Cortex-M4 |
| Build system | GNU Make |
| Debug interface | SWD |
| Console | UART0 |

The project includes generated CodeFusion Studio configuration files and MSDK build configuration.

## 8. Repository Structure

```text
apard_sample_1/
│
├── .cfs/
│   ├── max32690-wlp.cfsconfig
│   └── gdb_toolbox/
│
├── m4/
│   ├── src/
│   │   ├── main.c
│   │   ├── bms_main.c
│   │   ├── bms_config.h
│   │   ├── can_receiver.c
│   │   └── can_receiver.h
│   │
│   ├── lib/
│   │   ├── no-OS/
│   │   └── no-OS-BMS/
│   │
│   ├── .vscode/
│   ├── Makefile
│   ├── project.mk
│   ├── config.mk
│   ├── memory.ld
│   └── README.md
│
└── apard_sample_1.code-workspace
```

## 9. Current Development Status

The uploaded firmware contains the following implementation elements:

- MAX32690 application configured for the AD-APARD32690-SL.
- SPI1 initialization and manual GPIO chip-select handling.
- ADBMS6830 command PEC15 calculation.
- isoSPI wake-up and ADC conversion command sequence.
- Cell-voltage register read command sequence.
- Optional SPI debug logging and PEC10 verification.
- CAN1 initialization at 500 kbps.
- Reception and decoding of 13 temperature channels.
- Temperature collection timeout and validity tracking.
- Combined serial-console display structure.

The project is a master-controller firmware implementation intended to integrate cell-voltage monitoring with distributed temperature acquisition.

## 10. Integration and Validation Notes

The following details should be checked before treating the complete 75-channel system as integrated:

1. **CAN message compatibility:** The APARD receiver currently expects CAN IDs `0x200`–`0x203` and unsigned temperature values scaled by 0.1 °C. The distributed-board firmware must use the same identifiers, byte order, signedness, and scaling.
2. **Multiple temperature boards:** The current receiver collects 13 channels using four fixed CAN IDs. Extending it to all five boards and 75 channels requires a board-identification and message-mapping scheme.
3. **ADBMS cell-data decoding:** The uploaded `bms_main.c` contains a placeholder comment where the remaining response-decoding logic would be expected. Verify the complete decoding implementation before relying on the cell-voltage output.
4. **Cell 5 display value:** The current `main.c` assigns 4.16 V directly to Cell 5. This is a hardcoded demonstration value, not a measurement from the ADBMS6830.
5. **PEC validation:** `BMS_ENABLE_PEC_CHECK` is disabled by default in the supplied configuration. Enable and validate it when performing data-integrity testing.
6. **Full system validation:** Multi-board CAN reception, all 75 temperature channels, and complete battery-pack integration require system-level testing.

---

## Technologies Used

- **BMS Master:** AD-APARD32690-SL
- **Microcontroller:** MAX32690
- **Cell Monitor:** ADBMS6830
- **isoSPI Interface:** ADBMS6822
- **Communication:** SPI1, CAN1, UART0
- **CAN Transceiver Network:** 500 kbps
- **Firmware:** Embedded C
- **SDK:** Analog Devices MSDK
- **IDE:** CodeFusion Studio
- **Debugging:** SWD, UART, SPI diagnostics

---

*Centralized battery monitoring through an APARD32690 master controller, combining ADBMS6830 cell-voltage acquisition with distributed CAN-based temperature monitoring.*
