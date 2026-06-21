// DOM-IGNORE-BEGIN
/*
Copyright (C) 2025, Microchip Technology Inc., and its subsidiaries. All rights reserved.

The software and documentation is provided by microchip and its contributors
"as is" and any express, implied or statutory warranties, including, but not
limited to, the implied warranties of merchantability, fitness for a particular
purpose and non-infringement of third party intellectual property rights are
disclaimed to the fullest extent permitted by law. In no event shall microchip
or its contributors be liable for any direct, indirect, incidental, special,
exemplary, or consequential damages (including, but not limited to, procurement
of substitute goods or services; loss of use, data, or profits; or business
interruption) however caused and on any theory of liability, whether in contract,
strict liability, or tort (including negligence or otherwise) arising in any way
out of the use of the software and documentation, even if advised of the
possibility of such damage.

Except as expressly permitted hereunder and subject to the applicable license terms
for any third-party software incorporated in the software and any applicable open
source software license terms, no license or other rights, whether express or
implied, are granted under any patent or other intellectual property rights of
Microchip or any third party.
*/
// DOM-IGNORE-END

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
namespace microchip {
namespace rcp {
#endif

/**
 * @brief Standard return type of this component.
 */
typedef enum {
    RT_OK = 0x00,                      /** No error occurred */
    RT_NOT_OK = 0x01,                  /** An unspecified error occurred */
    RT_UNKNOWN_SERVICE = 0x02,         /** The requested Service ID is unknown */
    RT_UNKNOWN_METHOD = 0x03,          /** The requested Method ID is unknown. Service ID is known.*/
    RT_NOT_READY = 0x04,               /** Service ID and Method ID are known. Application not running. */
    RT_NOT_REACHABLE = 0x05,           /** System running the service is not reachable (internal error code only) */
    RT_TIMEOUT = 0x06,                 /** A timeout occurred (internal error code only) */
    RT_WRONG_PROTOCOL_VERSION = 0x07,  /** Version of SOME/IP not supported */
    RT_WRONG_INTERFACE_VERSION = 0x08, /** Interface version mismatch */
    RT_MALFORMED_MESSAGE = 0x09,       /** Deserialization error, so that payload cannot be deserialized */
    RT_WRONG_MESSAGE_TYPE = 0x0A,      /** An unexpected message type was received */
    RT_E2E_REPEATED = 0x0B,            /** Repeated E2E calculation error */
    RT_E2E_WRONG_SEQUENCE = 0x0C,      /** Wrong E2E sequence error */
    RT_E2E = 0x0D,                     /** Not further specified E2E error */
    RT_E2E_NOT_AVAILABLE = 0x0E,       /** E2E not available */
    RT_E2E_NO_NEW_DATA = 0x0F,         /** No new data for E2E calculation present */
    RT_INTERNAL_ERROR = 0x1000,        /** Internal state machine error */
    RT_SEND_ERROR = 0x1001,            /** Currently not able to send out data */
    RT_PARAMETER_NOT_VALID = 0x1002,   /** At least one of the given parameter is not valid */
    RT_DEVICE_NOT_AVAILABLE = 0x1003   /** The device is not present at the given time */
} ReturnCode_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::Reboot "Reboot" function.
 */
typedef struct
{
    uint16_t ImageLength; ///< Amount of Bytes stored in Image variable
    uint8_t Image[32];    ///< Name of the image which should be booted.
} RebootVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::Lock "Lock" function.
 */
typedef struct
{
    uint8_t Mode; ///< 1 = Security Mode 1; 2 = Security Mode 2; 3 = Security Mode 3;
} LockVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::StartUpdate "StartUpdate" function.
 */
typedef struct
{
    uint16_t ImageNameLength;  ///< Amount of Bytes stored in ImageName variable
    uint8_t ImageName[32];     ///< Name of the image which should be updated.
    uint16_t InitVectorLength; ///< Amount of Bytes stored in InitVector variable
    uint8_t InitVector[32];    ///< Image init vector.
} StartUpdateVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::WriteImage "WriteImage" function.
 */
typedef struct
{
    uint16_t ImageNameLength; ///< Amount of Bytes stored in ImageName variable
    uint8_t ImageName[32];    ///< Name of the image which should be updated.
    uint32_t WriteId;         ///< Consecutive counter of write commands.(Same write id in sequence is ignored, e.g. in case response is lost)
    uint16_t WriteDataLength; ///< Amount of Bytes stored in WriteData variable
    uint8_t WriteData[1200];  ///< Image data to write (multiples of 16 bytes).
} WriteImageVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::WriteImage "WriteImage" function.
 */
typedef struct
{
    uint32_t WriteId; ///< Consecutive counter of write commands is added to response message so programming device can detect a loss of packets.
} WriteImageReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::FinishUpdate "FinishUpdate" function.
 */
typedef struct
{
    uint16_t ImageNameLength; ///< Amount of Bytes stored in ImageName variable
    uint8_t ImageName[32];    ///< Name of the image which should be updated.
    uint16_t SignatureLength; ///< Amount of Bytes stored in Signature variable
    uint8_t Signature[512];   ///< Image signature that is used to validate the previously written image.
} FinishUpdateVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::StartPMATestMode "StartPMATestMode" function.
 */
typedef struct
{
    uint8_t TestMode;  ///< 1 = Output mode timing jitter; 2 - Output droop; 3 - PSD mask; 4 - High impedance mode;
    uint32_t Duration; ///< Test duration in ms. If set to 0 ms, the test mode is running until a device reset is performed.
} StartPMATestModeVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::StartTDMeasurement "StartTDMeasurement" function.
 */
typedef struct
{
    uint8_t Role;     ///< 0 = Initiator; 1 = Reference; 2 = Measurement;
    uint8_t Duration; ///< Measurement duration in ms. Initiator: Defines the duration of the overall measurement. Must be larger than the maximum duration configured in a reference/measured node.
                      ///< Reference/Measurement: Defines the maximum duration of the measurement in the range of 1 to 16ms.
} StartTDMeasurementVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::GetTDMeasurementResult "GetTDMeasurementResult" function.
 */
typedef struct
{
    uint8_t Role; ///< 0 = Initiator; 1 = Reference; 2 = Measurement;
} GetTDMeasurementResultVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::GetTDMeasurementResult "GetTDMeasurementResult" function.
 */
typedef struct
{
    uint8_t Role;                         ///< 0 = Initiator; 1 = Reference; 2 = Measurement;
    uint8_t Duration;                     ///< Measurement duration in ms.
    uint32_t InternalDelay;               ///< Number of pulses measured for the local node.
    uint32_t InternalDelayOnMeasuredNode; ///< Number of pulses measured on the measured node.
    uint32_t NetworkDelay;                ///< Number of pulses measured for the network.
} GetTDMeasurementResultReply_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::GetNetworkStatus "GetNetworkStatus" function.
 */
typedef struct
{
    uint32_t EndpointIpV4Address;   ///< IPv4 address assigned to the endpoint.
    uint64_t EndpointIpV6AddressHi; ///< IPv6 address (upper 8 bytes) assigned to the endpoint.
    uint64_t EndpointIpV6AddressLo; ///< IPv6 address (lower 8 bytes) assigned to the endpoint.
    uint64_t EndpointAddress;       ///< MAC address assigned to the endpoint.
    uint8_t EndpointStatus;         ///< 1 = Link-up; 2 = Link-down;
    uint64_t OaspiAddress;          ///< MAC address assigned to the OASPI bridge.
    uint8_t OaspiStatus;            ///< 0 = Disabled; 1 = Link-up; 2 = Link-down;
    uint8_t ArbitrationMode;        ///< 0 = CSMA/CD; 1 = PLCA; 2 = PLCA no fallback;
    uint8_t PLCANodeId;             ///< PLCA node identifier
} GetNetworkStatusReply_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::GetCurrentWallclock "GetCurrentWallclock" function.
 */
typedef struct
{
    uint8_t State;      ///< 0 = Unsynced; 1 = SyncedUncertain; 2 = SyncedCertain;
    uint64_t Wallclock; ///< Current gPTP wallclock
} GetCurrentWallclockReply_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::GetStatus "GetStatus" function.
 */
typedef struct
{
    uint16_t ActiveApplicationLength;        ///< Amount of Bytes stored in ActiveApplication variable
    uint8_t ActiveApplication[64];           ///< Name of the currently running application.
    uint16_t ChipIdentifierLength;           ///< Amount of Bytes stored in ChipIdentifier variable
    uint8_t ChipIdentifier[64];              ///< Chip identifier, e.g. LAN8661A.
    uint16_t RootApplicationVersionLength;   ///< Amount of Bytes stored in RootApplicationVersion variable
    uint8_t RootApplicationVersion[64];      ///< Version of the root application.
    uint16_t BootApplicationVersionLength;   ///< Amount of Bytes stored in BootApplicationVersion variable
    uint8_t BootApplicationVersion[64];      ///< Version of the bootloader application.
    uint16_t BootConfigurationVersionLength; ///< Amount of Bytes stored in BootConfigurationVersion variable
    uint8_t BootConfigurationVersion[64];    ///< Version of the bootloader configuration, which is the configuration name given in the COMO configuration XML file.
    uint16_t MainApplicationVersionLength;   ///< Amount of Bytes stored in MainApplicationVersion variable
    uint8_t MainApplicationVersion[64];      ///< Version of the main application.
    uint16_t MainConfigurationVersionLength; ///< Amount of Bytes stored in MainConfigurationVersion variable
    uint8_t MainConfigurationVersion[64];    ///< Version of the main configuration, which is the configuration name given in the COMO configuration XML file.
    uint64_t StartupInformation; ///< Bit 0 - Power-On Reset; Bit 1 - Under-voltage VDDC Reset; Bit 2 - Under-voltage VDDA Reset; Bit 3 - BG Error Reset; Bit 4 - External Reset; Bit 5 - Watchdog
                                 ///< Reset; Bit 6 - Over-temperature Reset; Bit 7 - Software Reset; Bit 8 - Lock-up Reset; Bit 9,10 - Security Mode: 0, 1, 2 or 3; Bit 11..63 - Reserved;
    uint64_t UpTime;             ///< Uptime in ns after reset.
    uint32_t ComoVersion;        ///< Como data model version.
    uint32_t ServiceVersion;     ///< Major and minor version of this service in following format: 0x<%02X:major><%02X:minor>00
    uint16_t KeysVersionLength;  ///< Amount of Bytes stored in KeysVersion variable
    uint8_t KeysVersion[64];     ///<  Version of the security keys.
} GetStatusReply_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::ReadDiagnosisData "ReadDiagnosisData" function.
 */
typedef struct
{
    uint16_t Channel0Length; ///< Amount of Bytes stored in Channel0 variable
    uint8_t Channel0[16];    ///< Diagnosis data from channel 0.
    uint16_t Channel1Length; ///< Amount of Bytes stored in Channel1 variable
    uint8_t Channel1[16];    ///< Diagnosis data from channel 1.
    uint16_t Channel2Length; ///< Amount of Bytes stored in Channel2 variable
    uint8_t Channel2[16];    ///< Diagnosis data from channel 2.
    uint16_t Channel3Length; ///< Amount of Bytes stored in Channel3 variable
    uint8_t Channel3[16];    ///< Diagnosis data from channel 3.
} ReadDiagnosisDataReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::ConfigDigitalPin "ConfigDigitalPin" function.
 */
typedef struct
{
    uint8_t PinId;         ///< Id of digital pin PA00 to PA15
    uint8_t Mode;          ///< 0 = No resistor (default); 1 = Pull-up resistor; 2 = Pull-down resistor;
    uint8_t DriveStrength; ///< 0 = 1mA; 1 = 2mA (default); 2 = 3mA; 3 = 4mA;
} ConfigDigitalPinVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::ReleaseDigitalPins "ReleaseDigitalPins" function.
 */
typedef struct
{
    uint16_t PinIdListLength; ///< Amount of Bytes stored in PinIdList variable
    uint8_t PinIdList[16];    ///< List of pin ids, PA00 to PA15, that shall be unlocked and reset to default.
} ReleaseDigitalPinsVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::OpenI2C "OpenI2C" function.
 */
typedef struct
{
    uint8_t PinIdSda;   ///< Id of digital pin PA00 to PA15
    uint8_t PinIdScl;   ///< Id of digital pin PA00 to PA15
    uint8_t ClockSpeed; ///< 0 = Standard (100 kHz); 1 = Fast (400 kHz); 2 = Fast plus (1 MHz);
} OpenI2CVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::OpenI2C "OpenI2C" function.
 */
typedef struct
{
    uint16_t HandleI2C; ///< Handle of the opened I2C interface.
} OpenI2CReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::CloseI2C "CloseI2C" function.
 */
typedef struct
{
    uint16_t HandleI2C; ///< Handle of the I2C interface.
} CloseI2CVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::ClearI2CBus "ClearI2CBus" function.
 */
typedef struct
{
    uint16_t HandleI2C; ///< Handle of the I2C interface.
} ClearI2CBusVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::WriteI2C "WriteI2C" function.
 */
typedef struct
{
    uint16_t HandleI2C;       ///< Handle of the I2C interface.
    uint16_t DeviceAddress;   ///< Address shifted right by 1 (no R/W Bit).
    uint32_t WriteId;         ///< Consecutive counter of write commands. WriteId shall start with 0 after openI2C().
    uint16_t WriteDataLength; ///< Amount of Bytes stored in WriteData variable
    uint8_t WriteData[1400];  ///< Data that will be written to I2C.
} WriteI2CVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::ReadI2C "ReadI2C" function.
 */
typedef struct
{
    uint16_t HandleI2C;      ///< Handle of the I2C interface.
    uint16_t DeviceAddress;  ///< Address shifted right by 1 (no R/W Bit).
    uint16_t ReadDataLength; ///< Length of the data to read in bytes.
} ReadI2CVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::WriteAndReadI2C "WriteAndReadI2C" function.
 */
typedef struct
{
    uint16_t HandleI2C;       ///< Handle of the I2C interface.
    uint16_t DeviceAddress;   ///< Address shifted right by 1 (no R/W Bit).
    uint16_t ReadDataLength;  ///< Length of the data to read in bytes.
    uint32_t WriteId;         ///< Consecutive counter of write commands. WriteId shall start with 0 after openI2C().
    uint16_t WriteDataLength; ///< Amount of Bytes stored in WriteData variable
    uint8_t WriteData[1400];
} WriteAndReadI2CVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::ReadI2C "ReadI2C" function.
 */
typedef struct
{
    uint32_t ReadId;         ///< Consecutive counter of reported read requests.
    uint16_t ReadDataLength; ///< Amount of Bytes stored in ReadData variable
    uint8_t ReadData[1400];  ///< Data that was read from I2C.
} ReadI2CReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::OpenGpio "OpenGpio" function.
 */
typedef struct
{
    uint8_t PinIdGpio; ///< Id of digital pin PA00 to PA15
    uint8_t Direction; ///< 0 = Input; 1 = Output low; 2 = Output high; 3 = Open drain;
} OpenGpioVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::OpenDebouncedGpio "OpenDebouncedGpio" function.
 */
typedef struct
{
    uint8_t PinIdGpio;    ///< Id of digital pin PA00 to PA15
    uint8_t DebounceTime; ///< 0 = 200ns; 1 = 400ns; 2 = 800ns; 3 = 1600ns; 4 = 3200ns; 5 = 6400ns; 6 = 12800ns; 7 = 25600ns; 8 = 187us; 9 = 375us; 10 = 750us; 11 = 1500us; 12 = 3ms; 13 = 6ms; 14 =
                          ///< 12ms; 15 = 24ms;
} OpenDebouncedGpioVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::OpenGpio "OpenGpio" function.
 */
typedef struct
{
    uint16_t HandleGpio; ///< Handle of the opened GPIO pin.
} OpenGpioReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::CloseGpio "CloseGpio" function.
 */
typedef struct
{
    uint16_t HandleGpio; ///< Handle of the opened GPIO pin.
} CloseGpioVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::EnableGpioPulseEvent "EnableGpioPulseEvent" function.
 */
typedef struct
{
    uint16_t HandleGpio;       ///< Handle of the opened GPIO pin.
    uint8_t Notification;      ///< 0 = None; 1 = Trigger; 2 = Data;
    uint8_t StartEdge;         ///< 0 = Falling Edge; 1 = Rising Edge;
    uint8_t StartTimeRelative; ///< 0 = Absolute Time; 1 = Relative Time;
    uint64_t StartTime;        ///< Start time in ns
    uint32_t PulseTime;        ///< Pulse time in ns (max. 2^30-1).  Pulse (high) time of the event. Must not be 0.
    uint32_t IdleTime; ///< dle time in ns (max. 2^30-1).  Idle (low) time of the event. If set to 0, a single (one-shot) pulse is generated. Otherwise a periodic event is generated.  V1.4 and above:
                       ///< Count is used to limit the number of periodic events.
} EnableGpioPulseEventVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::EnableGpioCaptureEvent "EnableGpioCaptureEvent" function.
 */
typedef struct
{
    uint16_t HandleGpio;      ///< Handle of the opened GPIO pin.
    uint8_t NotificationType; ///< 0 = None; 1 = Trigger; 2 = Data;
    uint8_t Trigger;          ///< 0 = Falling Edge; 1 = Rising Edge; 2 = Both Edges; 3 = Low Level; 4 = High Level; 5 = Periodic Falling Edge; 6 = Periodic Rising Edge; 7 = Periodic Both Edges;
    uint8_t Timestamped;      ///< 0 = False; 1 = True;
} EnableGpioCaptureEventVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::DisableGpioEvent "DisableGpioEvent" function.
 */
typedef struct
{
    uint16_t HandleGpio; ///< Handle of the opened GPIO pin.
} DisableGpioEventVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::SetGpio "SetGpio" function.
 */
typedef struct
{
    uint16_t GpioValuesLength; ///< Amount of Bytes stored in GpioValues variable
    uint8_t GpioValues[48];    ///< List of value tuples, whereas the structure of tuple is defined as: { uint16 Handle of the GPIO. uint8 Value of the GPIO. }
} SetGpioVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::GetGpio "GetGpio" function.
 */
typedef struct
{
    uint16_t GpioValuesLength; ///< Amount of Bytes stored in GpioValues variable
    uint8_t GpioValues[48];    ///< List of value tuples, whereas the structure of tuple is defined as: { uint16 Handle of the GPIO. uint8 Value of the GPIO. }
} GetGpioReply_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::GetGpioEvents "GetGpioEvents" function.
 */
typedef struct
{
    uint16_t EventsLength; ///< Amount of Bytes stored in Events variable
    uint8_t Events[208]; ///< List of events, whereas each event is structured as defined below: { uint16 Handle of the GPIO. uint8 Current GPIO State. uint8 Overflow; uint8 Timestamp Status [unsynced
                         ///< = 0, uncertain = 1, certain = 2, invalid = 3] uint64 Timestamp }
} GetGpioEventsReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClientListener::OnGpioEvents "OnGpioEvents" callback.
 */
typedef struct
{
    uint16_t EventsLength; ///< Amount of Bytes stored in Events variable
    uint8_t Events[208]; ///< List of events, whereas each event is structured as defined below: { uint16 Handle of the GPIO. uint8 Current GPIO State. uint8 Overflow; uint8 Timestamp Status [unsynced
                         ///< = 0, uncertain = 1, certain = 2, invalid = 3] uint64 Timestamp }
} OnGpioEventsNotification_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::OpenUart "OpenUart" function.
 */
typedef struct
{
    uint8_t PinIdTx;       ///< Id of digital pin PA00 to PA15, value 0xFF for an unused pin
    uint8_t PinIdRx;       ///< Id of digital pin PA00 to PA15, value 0xFF for an unused pin
    uint8_t PinIdRts;      ///< Id of digital pin PA00 to PA15, value 0xFF for an unused pin
    uint8_t PinIdCts;      ///< Id of digital pin PA00 to PA15, value 0xFF for an unused pin
    uint8_t Notification;  ///< 0 = None; 1 = Data;
    uint32_t BaudRate;     ///< UART baud rate in Hz. Supported baud rates are:  4800 Bd; 9600 Bd; 19200 Bd; 38400 Bd; 57600 Bd; 115200 Bd; 230400 Bd; 460800 Bd; 921600 Bd; 1 MBd; 2 MBd; 3 MBd;
    uint8_t Parity;        ///< 0 = Even parity; 1 = Odd parity; 2 = No parity;
    uint8_t StopBits;      ///< 0 = One stop bit; 1 = Two stop bits;
    uint8_t BitOrder;      ///< Defined the bit-order of the data received from or transmitted to UART: 0 = little-endian; 1 = big-endian;
    uint16_t RxBufferSize; ///< Buffer size for UART receive buffer in bytes.
    uint16_t RxThreshold;  ///< Threshold for rx queue size when notification is sent in bytes.
    uint16_t RxTimeout;    ///< Timeout when notification is sent in ms.
} OpenUartVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::OpenUart "OpenUart" function.
 */
typedef struct
{
    uint16_t HandleUart; ///< Handle of the opened UART interface.
} OpenUartReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::CloseUart "CloseUart" function.
 */
typedef struct
{
    uint16_t HandleUart; ///< Handle of the opened UART interface.
} CloseUartVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::WriteUart "WriteUart" function.
 */
typedef struct
{
    uint16_t HandleUart;      ///< Handle of the opened UART interface.
    uint32_t WriteId;         ///< Consecutive counter of write commands. WriteId shall start with 0 after openUart().
    uint16_t WriteDataLength; ///< Amount of Bytes stored in WriteData variable
    uint8_t WriteData[1400];  ///< Data to write.
} WriteUartVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::WriteUartFireAndForget "WriteUartFireAndForget" function.
 */
typedef struct
{
    uint16_t HandleUart;      ///< Handle of the opened UART interface.
    uint32_t WriteId;         ///< Consecutive counter of write commands. WriteId shall start with 0 after openUart().
    uint16_t WriteDataLength; ///< Amount of Bytes stored in WriteData variable
    uint8_t WriteData[1400];  ///< Data to write.
} WriteUartFireAndForgetVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::ReadUart "ReadUart" function.
 */
typedef struct
{
    uint16_t HandleUart; ///< Handle of the opened UART interface.
} ReadUartVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::ReadUart "ReadUart" function.
 */
typedef struct
{
    uint32_t ReadId;         ///< Consecutive counter of reported read requests or notifications.
    uint16_t ReadDataLength; ///< Amount of Bytes stored in ReadData variable
    uint8_t ReadData[1400];  ///< Data that was read from UART.
} ReadUartReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClientListener::OnUartReceive "OnUartReceive" callback.
 */
typedef struct
{
    uint16_t HandleUart;     ///< Handle of the opened UART interface.
    uint32_t ReadId;         ///< Consecutive counter of reported read requests or notifications.
    uint16_t ReadDataLength; ///< Amount of Bytes stored in ReadData variable
    uint8_t ReadData[1400];  ///< Data that was received from UART.
} OnUartReceiveNotification_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::OpenSpi "OpenSpi" function.
 */
typedef struct
{
    uint8_t PinIdMiso;   ///< Id of digital pin PA00 to PA15, value 0xFF for an unused pin
    uint8_t PinIdSck;    ///< Id of digital pin PA00 to PA15, value 0xFF for an unused pin
    uint8_t PinIdCs;     ///< Id of digital pin PA00 to PA15, value 0xFF for an unused pin
    uint8_t PinIdMosi;   ///< Id of digital pin PA00 to PA15, value 0xFF for an unused pin
    uint8_t Mode;        ///< 0 = Mode 0 (CPOL=0, CPHA=0); 1 = Mode 1 (CPOL=0, CPHA=1); 2 = Mode 2 (CPOL=1, CPHA=0); 3 = Mode 3(CPOL=1, CPHA=1);
    uint32_t ClockSpeed; ///< SPI clock speed in Hz. Supported clock speeds are: 1 MHz; 1.25 MHz; 1.786 MHz; 1.923 MHz; 2.083 MHz; 3.125 MHz; 3.571 MHz; 4.166 MHz; 5 MHz; 6.25 MHz; 8.33 MHz; 12.5 MHz;
                         ///< 25 MHz;
} OpenSpiVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::OpenSpi "OpenSpi" function.
 */
typedef struct
{
    uint16_t HandleSpi; ///< Handle of the opened SPI interface.
} OpenSpiReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::CloseSpi "CloseSpi" function.
 */
typedef struct
{
    uint16_t HandleSpi; ///< Handle of the opened SPI interface.
} CloseSpiVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::WriteAndReadSpi "WriteAndReadSpi" function.
 */
typedef struct
{
    uint16_t HandleSpi;       ///< Handle of the opened SPI interface.
    uint16_t ReadDataLength;  ///< Length of the data to read in bytes.
    uint32_t WriteId;         ///< Consecutive counter of write commands. WriteId shall start with 0 after openSpi().
    uint16_t WriteDataLength; ///< Amount of Bytes stored in WriteData variable
    uint8_t WriteData[1400];  ///< Data that will be written to SPI interface.
} WriteAndReadSpiVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::WriteAndReadSpi "WriteAndReadSpi" function.
 */
typedef struct
{
    uint32_t ReadId;         ///< Consecutive counter of read commands. ReadId shall start with 0 after openSpi().
    uint16_t ReadDataLength; ///< Amount of Bytes stored in ReadData variable
    uint8_t ReadData[1400];  ///< Data that was read from SPI.
} WriteAndReadSpiReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::WriteSpiFireAndForget "WriteSpiFireAndForget" function.
 */
typedef struct
{
    uint16_t HandleSpi;       ///< Handle of the opened SPI interface.
    uint32_t WriteId;         ///< Consecutive counter of write commands. WriteId shall start with 0 after openSpi().
    uint16_t WriteDataLength; ///< Amount of Bytes stored in WriteData variable
    uint8_t WriteData[1400];  ///< Data that will be written to SPI interface.
} WriteSpiFireAndForgetVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::OpenAdc "OpenAdc" function.
 */
typedef struct
{
    uint8_t PinId; ///< Id of analog pin (always 0).
} OpenAdcVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::OpenAdc "OpenAdc" function.
 */
typedef struct
{
    uint16_t HandleAdc; ///< Handle of the opened ADC interface.
} OpenAdcReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::CloseAdc "CloseAdc" function.
 */
typedef struct
{
    uint16_t HandleAdc; ///< Handle of the opened ADC interface.
} CloseAdcVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::ReadAdc "ReadAdc" function.
 */
typedef struct
{
    uint16_t HandleAdc;       ///< Handle of the opened ADC interface.
    uint8_t ChannelSelect;    ///< 0 = Analog input; 1 = Internal temperature;
    uint8_t VoltageReference; ///< 0 = 3v3 (VDDA33); 1 = 1v1;
} ReadAdcVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::ReadAdc "ReadAdc" function.
 */
typedef struct
{
    uint8_t Instance;  ///< Always 0.
    uint16_t ReadData; ///< Data that was read from ADC or temperature sensor.
} ReadAdcReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::OpenPwm "OpenPwm" function.
 */
typedef struct
{
    uint8_t PinId;         ///< Id of digital pin PA00 to PA15
    uint32_t IntervalTime; ///< PWM interval time in ns.  If not a multiple of 20ns, the interval time is rounded down.
    uint32_t DutyCycle;    ///< Initial duty cycle in percent. 0 = 0% .. 2^31 = 100%.  If the calculated duty cycle is not a multiple of 20ns, the duty cycle is rounded down to multiple of 20ns.
} OpenPwmVar_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::OpenPwm "OpenPwm" function.
 */
typedef struct
{
    uint16_t HandlePwm; ///< Handle of the opened PWM interface.
} OpenPwmReply_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::ClosePwm "ClosePwm" function.
 */
typedef struct
{
    uint16_t HandlePwm; ///< Handle of the opened PWM interface.
} ClosePwmVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::WritePwm "WritePwm" function.
 */
typedef struct
{
    uint16_t HandlePwm; ///< Handle of the opened PWM interface.
    uint32_t WriteId;   ///< Consecutive counter of write commands. WriteId shall start with 0 after openPwm().
    uint32_t WriteData; ///< Duty cycle in percent. 0 = 0% .. 2^31 = 100%
} WritePwmVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::WriteAndReadSpiExtended "WriteAndReadSpiExtended" function.
 */
typedef struct WriteAndReadSpiElementVar
{
    uint16_t ReadDataLength;                ///< Length of the data to read in bytes.
    uint16_t WriteDataLength;               ///< Amount of Bytes stored in WriteData variable
    uint8_t WriteData[64];                  ///< Data that will be written to SPI interface.
    struct WriteAndReadSpiElementVar *next; ///< Pointer to the next element. Last element is NULL terminated. Forming a linked list.
} WriteAndReadSpiElementVar_t;

/**
 * @brief This structure is used as an input to the \ref microchip::rcp::LAN866XClient::WriteAndReadSpiExtended "WriteAndReadSpiExtended" function.
 */
typedef struct
{
    uint16_t HandleSpi;                        ///< Handle of the opened SPI interface.
    uint32_t WriteId;                          ///< Consecutive counter of write commands. WriteId shall start with 0 after openSpi().
    WriteAndReadSpiElementVar_t *firstElement; ///< First SPI element.
} WriteAndReadSpiExtendedVar_t;

/**
 * @brief This structure is used as an output to the \ref microchip::rcp::LAN866XClient::WriteAndReadSpiExtended "WriteAndReadSpiExtended" function.
 */
typedef struct WriteAndReadSpiElementReply
{
    uint16_t ReadDataLength;                  ///< Length of the data to read in bytes.
    uint8_t ReadData[64];                     ///< Data that will be written to SPI interface.
    struct WriteAndReadSpiElementReply *next; ///< Pointer to the next element. Last element is NULL terminated. Forming a linked list.
} WriteAndReadSpiElementReply_t;

/**
 * @brief This structure is used as an output from the \ref microchip::rcp::LAN866XClient::WriteAndReadSpiExtended "WriteAndReadSpiExtended" function.
 */
typedef struct
{
    uint32_t ReadId;                             ///< Consecutive counter of read commands. ReadId shall start with 0 after openSpi().
    WriteAndReadSpiElementReply_t *firstElement; ///< First element;
} WriteAndReadSpiExtenedReply_t;

#ifdef __cplusplus
} // namespace rcp
} // namespace microchip
#endif