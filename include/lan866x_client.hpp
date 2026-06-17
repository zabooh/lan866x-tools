/**
 * \mainpage
 *
 * # LAN866x SOME/IP Client Library
 *
 * Welcome to the the LAN866x SOME/IP Client Library documentation.
 *
 * ## Remarks
 * - Full contained SOME/IP based remote control library to interface with Microchip Endpoint Solutions.
 * - Library is full thread safe. So calling API from different threads is allowed.
 * - It is allowed to call API from this library inside the callback methods it offers.
 * - This documentation refers to the C++ variant.
 *
 * ## License
 * Copyright (C) 2025, Microchip Technology Inc., and its subsidiaries. All rights reserved.
 */

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
#include <vector>
#include "lan866x_common.h"

using namespace std;

/// @brief Namespace owned by Microchip Inc.
namespace microchip {
/// @brief Namespace holding Remote Control Protocol API.
namespace rcp {

// *****************************************************************************
// *****************************************************************************
// Section: Callback API
// *****************************************************************************
// *****************************************************************************

class LAN866XClient;

/**
 * @class LAN866XFactoryListener
 * @brief Interface for receiving notifications from the LAN866X factory.
 *
 * This class defines a listener interface for receiving notifications
 * when new clients become available.
 */
class LAN866XFactoryListener
{
  public:
    /**
     * @brief Called when a new client becomes available.
     * @param pNewClient Pointer to the newly available client.
     */
    virtual void OnNewClientAvailable(LAN866XClient *pNewClient) = 0;
};

/**
 * @class LAN866XClientListener
 * @brief Interface for receiving notifications from a LAN866X client.
 *
 * This class defines a listener interface for receiving various notifications
 * from a LAN866X client, such as availability changes, measurement completions,
 * GPIO events, and UART data reception.
 */
class LAN866XClientListener
{
  public:
    /**
     * @brief Called when the availability of the client changes.
     * @param pClient Pointer to the client whose availability changed.
     * @param available True if the client is available, false otherwise.
     */
    virtual void OnAvailable(LAN866XClient *pClient, bool available);

    /**
     * \brief Notification message indicating that a TD measurement cycle has completed.
     */
    virtual void OnTDMeasurementCompleted();
    /**
     * \brief Notification message containing events. GPIO events are only reported, if notification is not set to None. In case of data notifications, the event data is included in the notification
     * message and gets cleared upon a successful transmission, i.e. a following GetGpioEvents() will not contain the already reported event data. In case of trigger events the client must retrieve
     * event data by calling the method GetGpioEvents(). If timestamp capturing is disabled, the timestamp status is reported as 'invalid' and the timestamp value must be ignored.
     */
    virtual void OnGpioEvents(const OnGpioEventsNotification_t *data);
    /**
     * \brief Notification message sent to the client if data notification is enabled. In order to receive the notification, the client must be subscribed to the defined event group.'ReadId' is
     * consecutively incremented with each notification message or by any data read request done by 'ReadUart'. It could be used by the client to detect repeated or missing data.
     */
    virtual void OnUartReceive(const OnUartReceiveNotification_t *data);
};

// *****************************************************************************
// *****************************************************************************
// Section: Public API
// *****************************************************************************
// *****************************************************************************

/**
 * @class LAN866XClientFactory
 * @brief Factory class for managing LAN866X clients.
 *
 * This class provides static methods for retrieving version information,
 * adding/removing factory listeners, and obtaining the broadcast device.
 */
class LAN866XClientFactory
{
  public:
    /**
     * @brief Retrieves the version of the LAN866X system.
     * @param[out] major Major version number.
     * @param[out] minor Minor version number.
     * @param[out] bugfix Bugfix version number.
     */
    static void GetVersion(uint8_t &major, uint8_t &minor, uint8_t &bugfix);

    /**
     * @brief Retrieves a vector containing pointers to all managed LAN866X clients, which are currently available.
     *
     * This method returns a vector of pointers to all LAN866XClient instances currently
     * managed by the factory. The returned vector does not transfer ownership of the
     * client objects; the caller must not delete the pointers.
     *
     * @return std::vector<LAN866XClient*> A vector of pointers to all managed clients.
     */
    static std::vector<LAN866XClient *> GetAllClients();

    /**
     * @brief Adds a listener to receive notifications from the factory.
     * @param listener Pointer to the listener to be added.
     * @param subscribeToEvent True, if this client shall subscribe to LAN866x events.
     */
    static void AddListener(LAN866XFactoryListener *listener, bool subscribeToEvent = true);

    /**
     * @brief Removes a listener from the factory.
     * @param listener Pointer to the listener to be removed.
     */
    static void RemoveListener(LAN866XFactoryListener *listener);

    /**
     * @brief Retrieves the broadcast device associated with the factory.
     * @return Pointer to the broadcast device.
     */
    static LAN866XClient *GetBroadcastDevice();

    /**
     * @brief Retrieves the next instance of LAN866X client. This method will cycle through all available instances, after every call.
     * @return Pointer to the LAN866X client.
     */
    static LAN866XClient *GetAnyInstance(bool &resetDetected, void *&contextForThatInstance);
};

/**
 * @class LAN866XClient
 * @brief Represents a client in the LAN866X system.
 *
 * This class provides an interface for interacting with a LAN866X client.
 * It includes methods for checking node availability, adding/removing listeners,
 * retrieving instance IDs, and obtaining IP address and port information.
 */
class LAN866XClient
{
  public:
    /**
     * @brief Checks if the node is available.
     * @return True if the node is available, false otherwise.
     */
    virtual bool IsNodeAvailable() = 0;

    /**
     * @brief Checks if there was a reset detected since the last call of this method.
     * @return True if there was a reset detected. false otherwise.
     */
    virtual bool WasResetDetected() = 0;

    /**
     * @brief Gets the context for the user assigned to this instance.
     * @param context A pointer where use can safe its context to, is written to the given pointer.
     */
    virtual void GetUserContext(void *&context) = 0;

    /**
     * @brief Sets the given pointer as new context for this instance.
     * @param context A pointer which all act as new context for this instance.
     */
    virtual void SetUserContext(void *context) = 0;

    /**
     * @brief Adds a listener to receive notifications from the client.
     * @param listener Pointer to the listener to be added.
     */
    virtual void AddListener(LAN866XClientListener *listener) = 0;

    /**
     * @brief Removes a listener from the client.
     * @param listener Pointer to the listener to be removed.
     */
    virtual void RemoveListener(LAN866XClientListener *listener) = 0;

    /**
     * @brief Retrieves the SOME/IP instance ID associated with the client.
     * @return The SOME/IP instance ID.
     */
    virtual uint16_t GetSomeIpInstaneId() = 0;

    /**
     * @brief Retrieves the IP address and port of the client.
     * @param[out] ipAddr Pointer to the IP address array.
     * @param[out] ipAddrLength Length of the IP address array.
     * @param[out] port Port number associated with the client.
     */
    virtual void GetIpAddressAndPort(uint8_t **ipAddr, uint8_t *ipAddrLength, uint16_t *port) = 0;

    /**
     * \brief Reboots the device either into the bootloader application or main application. Booting into the bootloader is used to start an update process. In case the image name does not match any
     * of the listed names, SOMEIP_E_NOT_REACHABLE is returned. A reboot is initiated after the response was sent. \param[in] dataIn Pointer to RebootVar_t structure holding all input parameters.
     * nullptr is not allowed.
     */
    virtual ReturnCode_t Reboot(const RebootVar_t *dataIn) = 0;

    /**
     * \brief Triggers a function used to identify the endpoint. Toggles the IDENTIFY pin, if configured.
     */
    virtual ReturnCode_t Identify() = 0;

    /**
     * \brief Locks the device to any of the given security modes.
     * \param[in] dataIn Pointer to LockVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t Lock(const LockVar_t *dataIn) = 0;

    /**
     * \brief Shutdown the device and turns of power when connected to a OA-3pin transceiver.
     */
    virtual ReturnCode_t Shutdown() = 0;

    /**
     * \brief Returns device information, such as: Chip Revision; Configuration Versions; Reset Status Information; Current Wallclock Time;
     * \param[out] dataOut Pointer to GetStatusReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t GetStatus(GetStatusReply_t *dataOut) = 0;

    /**
     * \brief Read the current gPTP wallclock.
     * \param[out] dataOut Pointer to GetCurrentWallclockReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t GetCurrentWallclock(GetCurrentWallclockReply_t *dataOut) = 0;

    /**
     * \brief Read diagnosis data.
     * \param[out] dataOut Pointer to ReadDiagnosisDataReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t ReadDiagnosisData(ReadDiagnosisDataReply_t *dataOut) = 0;

    /**
     * \brief Starts an configuration update process.
     * \param[in] dataIn Pointer to StartUpdateVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t StartUpdate(const StartUpdateVar_t *dataIn) = 0;

    /**
     * \brief Write configuration data.
     * \param[in] dataIn Pointer to WriteImageVar_t structure holding all input parameters. nullptr is not allowed.
     * \param[out] dataOut Pointer to WriteImageReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t WriteImage(const WriteImageVar_t *dataIn, WriteImageReply_t *dataOut) = 0;

    /**
     * \brief Finalizes an configuration update process.
     * \param[in] dataIn Pointer to FinishUpdateVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t FinishUpdate(const FinishUpdateVar_t *dataIn) = 0;

    /**
     * \brief Returns information about the 10Base-T1S network and the OASPI bridge, such as: Interface Status; MAC addresses;
     * \param[out] dataOut Pointer to GetNetworkStatusReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t GetNetworkStatus(GetNetworkStatusReply_t *dataOut) = 0;

    /**
     * \brief Starts a topology discovery (TD) measurement cycle. There are three roles defined:  Initiator: PLCA coordinator which initiates the measurement and reports the end of the
     * measurementReference: Reference nodeMeasurement: Measured nodeThe reference node and measured node must be started before the initiator gets started. Once the initiator is started, the
     * measurement will be executed, which includes disabling the network (no PLCA beacons). Upon the provided duration, the initiator reenables the network and reports the completion via a
     * OnTDMeasurementCompleted notification message. \param[in] dataIn Pointer to StartTDMeasurementVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t StartTDMeasurement(const StartTDMeasurementVar_t *dataIn) = 0;

    /**
     * \brief Returns the result of the TD measurement cycle. If no measurement was started, SOMEIP_E_NOT_REACHABLE is returned. When called for an initiator, both delay counters are 0, since it does
     * not provide any measurement values. A measurement node reports only InternalDelay and NetworkDelay, InternalDelayOnMeasuredNode is 0 in that case. \param[in] dataIn Pointer to
     * GetTDMeasurementResultVar_t structure holding all input parameters. nullptr is not allowed. \param[out] dataOut Pointer to GetTDMeasurementResultReply_t structure where all output parameters
     * will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t GetTDMeasurementResult(const GetTDMeasurementResultVar_t *dataIn, GetTDMeasurementResultReply_t *dataOut) = 0;

    /**
     * \brief Starts a PMA test mode, as specified by Clause 147.5.2 of the IEEE Std 802.3-2022.
     * \param[in] dataIn Pointer to StartPMATestModeVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t StartPMATestMode(const StartPMATestModeVar_t *dataIn) = 0;

    /**
     * \brief Wakes all sleeping devices on the local network.
     */
    virtual ReturnCode_t WakeupNetwork() = 0;

    /**
     * \brief Configures a digital IO (DIO) pin. After a DIO pin is configured, it can be used by a remote controlled interface like GPIO, I2C, UART, SPI. The pin can be reconfigured until used by a
     * remote controlled interface. Once the pin is in use by a remote interface it is locked. A locked pin can`t be configured and an error is returned. LAN866X provides 16 DIOs, whereas each DIO can
     * be multiplexed to various functionalities. \param[in] dataIn Pointer to ConfigDigitalPinVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t ConfigDigitalPin(const ConfigDigitalPinVar_t *dataIn) = 0;

    /**
     * \brief Unlocks a digital IO (DIO) pin if it is locked by a remote controlled interface like GPIO, I2C, UART, SPI. Furthermore the mode and drive strength are reset to default.
     * \param[in] dataIn Pointer to ReleaseDigitalPinsVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t ReleaseDigitalPins(const ReleaseDigitalPinsVar_t *dataIn) = 0;

    /**
     * \brief Opens and configures an I2C interface. An I2C interface can only be opened if it was not opened before. Both pins have to be assigned a configured DIO which refers to the same interface
     * instance and are not used by any other functionality, otherwise an error will be returned. \param[in] dataIn Pointer to OpenI2CVar_t structure holding all input parameters. nullptr is not
     * allowed. \param[out] dataOut Pointer to OpenI2CReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t OpenI2C(const OpenI2CVar_t *dataIn, OpenI2CReply_t *dataOut) = 0;

    /**
     * \brief Closes an I2C interface.
     * \param[in] dataIn Pointer to CloseI2CVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t CloseI2C(const CloseI2CVar_t *dataIn) = 0;

    /**
     * \brief Generates the I2C 'bus clear' sequence to resolve data line (SDA) stuck low conditions.
     * \param[in] dataIn Pointer to ClearI2CBusVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t ClearI2CBus(const ClearI2CBusVar_t *dataIn) = 0;

    /**
     * \brief Writes data to an I2C device. SOMEIP_E_NOT_REACHABLE is returned in case of any I2C error, like NACK on device address; NACK on data; Clock stretching timeout;
     * \param[in] dataIn Pointer to WriteI2CVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t WriteI2C(const WriteI2CVar_t *dataIn) = 0;

    /**
     * \brief Same as the 'write' method, but it doesn't send a response. also no error response is sent.
     * \param[in] dataIn Pointer to WriteI2CVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t WriteI2CFireAndForget(const WriteI2CVar_t *dataIn) = 0;

    /**
     * \brief Reads data from an I2C device. SOMEIP_E_NOT_REACHABLE is returned in case of any I2C error, like NACK on device address; Clock stretching timeout;
     * \param[in] dataIn Pointer to ReadI2CVar_t structure holding all input parameters. nullptr is not allowed.
     * \param[out] dataOut Pointer to ReadI2CReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t ReadI2C(const ReadI2CVar_t *dataIn, ReadI2CReply_t *dataOut) = 0;

    /**
     * \brief Writes data to an I2C device, performs a repeated start and reads data from the same device afterwards. SOMEIP_E_NOT_REACHABLE is returned in case of any I2C error, like NACK on device
     * address; NACK on data; Clock stretching timeout; \param[in] dataIn Pointer to WriteAndReadI2CVar_t structure holding all input parameters. nullptr is not allowed. \param[out] dataOut Pointer to
     * ReadI2CReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t WriteAndReadI2C(const WriteAndReadI2CVar_t *dataIn, ReadI2CReply_t *dataOut) = 0;

    /**
     * \brief Opens a digital IO pin as GPIO.
     * \param[in] dataIn Pointer to OpenGpioVar_t structure holding all input parameters. nullptr is not allowed.
     * \param[out] dataOut Pointer to OpenGpioReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t OpenGpio(const OpenGpioVar_t *dataIn, OpenGpioReply_t *dataOut) = 0;

    /**
     * \brief Opens a digital IO pin as GPIO. The IO pin is configured as input and gets debounced according to the given parameters. Debouncing is not supported in Rev.A0. In this case,
     * SOMEIP_E_NOT_REACHABLE is returned. \param[in] dataIn Pointer to OpenDebouncedGpioVar_t structure holding all input parameters. nullptr is not allowed. \param[out] dataOut Pointer to
     * OpenGpioReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t OpenDebouncedGpio(const OpenDebouncedGpioVar_t *dataIn, OpenGpioReply_t *dataOut) = 0;

    /**
     * \brief Closes a GPIO. This unlocks the use of the assigned DIO pin.
     * \param[in] dataIn Pointer to CloseGpioVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t CloseGpio(const CloseGpioVar_t *dataIn) = 0;

    /**
     * \brief Creates non-periodic or periodic pulses at a GPIO pin. If parameter 'IdleTime' is set to 0, a single (one-shot) pulse is generated.  Each event will raise the event bit of the
     * appropriate GPIO. Depending on parameter 'Notification Type' a notification message is sent. For a client to receive a notification, it must subscribe to the appropriate event group by the use
     * of SOME/IP-SD.There are three notification options for a client to handle an event: 'None': No notification message is issued if the event occurs. The client must poll the event status by the
     * use of 'GetGpioEvents'. 'Trigger': A notification message is sent to the client. The event data is included in the notification message and gets not cleared upon a successful transmission.. The
     * client must read the event data by using 'GetGpioEvents' to clear the event. 'Data': A notification message is sent to the client. The event data is included in the notification message and
     * gets cleared upon a successful transmission. \param[in] dataIn Pointer to EnableGpioPulseEventVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t EnableGpioPulseEvent(const EnableGpioPulseEventVar_t *dataIn) = 0;

    /**
     * \brief Captures an GPIO input event. If timestamping is enabled (parameter 'timestamped' set to true), a timestamp of the event is captured in addition.  Non-periodic events only occur once.
     * They must be disabled and re-enabled for any further event capture. There are three notification options for a client to handle an event: 'None': No notification message is issued if the event
     * occurs. The client must poll the event status by the use of 'GetGpioEvents'.'Trigger': A notification message is sent to the client. The event data is included in the notification message and
     * gets not cleared upon a successful transmission.. The client must read the event data by using 'GetGpioEvents' to clear the event.'Data': A notification message is sent to the client. The event
     * data is included in the notification message and gets cleared upon a successful transmission.Capture events are available only, if the GPIO pin is opened as input. When opened as output, an
     * error is returned. \param[in] dataIn Pointer to EnableGpioCaptureEventVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t EnableGpioCaptureEvent(const EnableGpioCaptureEventVar_t *dataIn) = 0;

    /**
     * \brief This method stops a previously enabled pulse event generation or GPIO event capture.
     * \param[in] dataIn Pointer to DisableGpioEventVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t DisableGpioEvent(const DisableGpioEventVar_t *dataIn) = 0;

    /**
     * \brief Sets or clears (high or low) a list of GPIO pins. If a GPIO pin is configured as input, it will be ignored. All GPIOs that should be set, are set at once. The same happens for GPIOs that
     * should be cleared. But the set and clear operation is not performed at the same time. \param[in] dataIn Pointer to SetGpioVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t SetGpio(const SetGpioVar_t *dataIn) = 0;

    /**
     * \brief Returns a list of the current state of all opened GPIOs.
     * \param[out] dataOut Pointer to GetGpioReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t GetGpio(GetGpioReply_t *dataOut) = 0;

    /**
     * \brief Combination of methods 'SetGpio' and 'GetGpio'.
     * \param[in] dataIn Pointer to SetGpioVar_t structure holding all input parameters. nullptr is not allowed.
     * \param[out] dataOut Pointer to GetGpioReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t SetAndGetGpio(const SetGpioVar_t *dataIn, GetGpioReply_t *dataOut) = 0;

    /**
     * \brief Same as 'SetGpio' method but without a response.
     * \param[in] dataIn Pointer to SetGpioVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t SetGpioFireAndForget(const SetGpioVar_t *dataIn) = 0;

    /**
     * \brief Returns a list of GPIO events. GPIO events using data notification are not reported by this method. Events and timestamp information are cleared upon a successful response
     * transmission.If timestamp capturing is disabled, the timestamp status is reported as 'invalid' and the timestamp value must be ignored. \param[out] dataOut Pointer to GetGpioEventsReply_t
     * structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t GetGpioEvents(GetGpioEventsReply_t *dataOut) = 0;

    /**
     * \brief Opens and configures a UART interface. A UART interface can only be opened if it was not opened before. Up to 4 pins have to be assigned a valid DIO pin which refers to the same
     * interface instance and are not used by any other functionality, otherwise an error will be returned.There are two notification options for a client to handle received UART data: 'None': No
     * notification message is issued if data received. The client must poll the event status by the use of 'ReadUart'. 'Data': A notification message is sent to the client. The received data is
     * included in the notification message. \param[in] dataIn Pointer to OpenUartVar_t structure holding all input parameters. nullptr is not allowed. \param[out] dataOut Pointer to OpenUartReply_t
     * structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t OpenUart(const OpenUartVar_t *dataIn, OpenUartReply_t *dataOut) = 0;

    /**
     * \brief Closes a UART interface.
     * \param[in] dataIn Pointer to CloseUartVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t CloseUart(const CloseUartVar_t *dataIn) = 0;

    /**
     * \brief Writes a sequence of bytes to the UART interface. 'WriteId' must be consecutively incremented with each call to this function addressing the same interface. It is used to detect repeated
     * data in case a client repeats a write due to an error. In this case the data is discarded. But it also detects missing data, which ends up in an error response. \param[in] dataIn Pointer to
     * WriteUartVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t WriteUart(const WriteUartVar_t *dataIn) = 0;

    /**
     * \brief Same as the 'write' method, but it doesn't send a response.
     * \param[in] dataIn Pointer to WriteUartFireAndForgetVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t WriteUartFireAndForget(const WriteUartFireAndForgetVar_t *dataIn) = 0;

    /**
     * \brief Reads received data.'ReadId' is consecutively incremented with each response to this request or by any data notification sent. It could be used by the client to detect repeated or
     * missing data. \param[in] dataIn Pointer to ReadUartVar_t structure holding all input parameters. nullptr is not allowed. \param[out] dataOut Pointer to ReadUartReply_t structure where all
     * output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t ReadUart(const ReadUartVar_t *dataIn, ReadUartReply_t *dataOut) = 0;

    /**
     * \brief Opens and configures an SPI interface. An SPI interface can only be opened if it was not opened before. Up to four pins have to be assigned a valid DIO which refers to the same interface
     * instance and are not used by any other functionality, otherwise an error will be returned. \param[in] dataIn Pointer to OpenSpiVar_t structure holding all input parameters. nullptr is not
     * allowed. \param[out] dataOut Pointer to OpenSpiReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t OpenSpi(const OpenSpiVar_t *dataIn, OpenSpiReply_t *dataOut) = 0;

    /**
     * \brief Closes a SPI interface.
     * \param[in] dataIn Pointer to CloseSpiVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t CloseSpi(const CloseSpiVar_t *dataIn) = 0;

    /**
     * \brief Performs a SPI transaction which writes and reads data. An SPI transaction reads and writes data simultaneously on its POCI and PICO pins. This method provides the option to define the
     * read length independent of the write length (length of the write data array). If the read length is less than the write length, the bytes read from the start of the transfer until the read
     * length is returned. The remaining incoming data is discarded. If the read length is larger than than the write length, zero bytes are transmitted after all outing data bytes are transmitted. In
     * half-duplex operation the transaction starts with writing the given data as a master (clock driven by the SPI interface). When all data bytes are transmitted, the interface is switched to slave
     * mode and incoming data is received. If the received data is less than the expected read length, an error is reported. 'WriteId' must be consecutively incremented with each call to this function
     * addressing the same interface. It is used to detect repeated data in case a client repeats a write due to an error. In this case the data is discarded. But it also detects missing data, which
     * ends up in an error response. \param[in] dataIn Pointer to WriteAndReadSpiVar_t structure holding all input parameters. nullptr is not allowed. \param[out] dataOut Pointer to
     * WriteAndReadSpiReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t WriteAndReadSpi(const WriteAndReadSpiVar_t *dataIn, WriteAndReadSpiReply_t *dataOut) = 0;

    /**
     * \brief Writes data to the SPI interface, but doesn`t return a response.
     * \param[in] dataIn Pointer to WriteSpiFireAndForgetVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t WriteSpiFireAndForget(const WriteSpiFireAndForgetVar_t *dataIn) = 0;

    /**
     * \brief Opens and configures an ADC interface. An ADC interface can only be opened if it was not opened before and the DIO pins are not used for other functionality.
     * \param[in] dataIn Pointer to OpenAdcVar_t structure holding all input parameters. nullptr is not allowed.
     * \param[out] dataOut Pointer to OpenAdcReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t OpenAdc(const OpenAdcVar_t *dataIn, OpenAdcReply_t *dataOut) = 0;

    /**
     * \brief Closes an ADC interface.
     * \param[in] dataIn Pointer to CloseAdcVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t CloseAdc(const CloseAdcVar_t *dataIn) = 0;

    /**
     * \brief Read data from ADC or internal temperature sensor.
     * \param[in] dataIn Pointer to ReadAdcVar_t structure holding all input parameters. nullptr is not allowed.
     * \param[out] dataOut Pointer to ReadAdcReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t ReadAdc(const ReadAdcVar_t *dataIn, ReadAdcReply_t *dataOut) = 0;

    /**
     * \brief Opens and configures a PWM interface. A PWM interface can only be opened if it was not opened before. Up to 4 pins have to be assigned a valid DIO pin which refers to the same interface
     * instance and are not used by any other functionality, otherwise an error will be returned. \param[in] dataIn Pointer to OpenPwmVar_t structure holding all input parameters. nullptr is not
     * allowed. \param[out] dataOut Pointer to OpenPwmReply_t structure where all output parameters will be stored on successful transaction. nullptr is not allowed.
     */
    virtual ReturnCode_t OpenPwm(const OpenPwmVar_t *dataIn, OpenPwmReply_t *dataOut) = 0;

    /**
     * \brief Closes a PWM interface.
     * \param[in] dataIn Pointer to ClosePwmVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t ClosePwm(const ClosePwmVar_t *dataIn) = 0;

    /**
     * \brief Writes a PWM cycle.
     * \param[in] dataIn Pointer to WritePwmVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t WritePwm(const WritePwmVar_t *dataIn) = 0;

    /**
     * \brief Same as the 'write' method, but it doesn't send a response.
     * \param[in] dataIn Pointer to WritePwmVar_t structure holding all input parameters. nullptr is not allowed.
     */
    virtual ReturnCode_t WritePwmFireAndForget(const WritePwmVar_t *dataIn) = 0;

    /**
     * \brief Performs multiple SPI transactions which each of them writes and reads data. Each SPI transaction reads and writes data simultaneously on its POCI and PICO pins.
     * This method provides the option to define the read length independent of the write length (length of the write data array) for each transaction. If the read length is less than the write
     * length, the bytes read from the start of the transfer until the read length is returned. The remaining incoming data is discarded. If the read length is larger than than the write length, zero
     * bytes are transmitted after all outing data bytes are transmitted. In half-duplex operation the transaction starts with writing the given data as a master (clock driven by the SPI interface).
     * When all data bytes are transmitted, the interface is switched to slave mode and incoming data is received. If the received data is less than the expected read length, an error is reported.
     * 'WriteId' must be consecutively incremented with each call to this function addressing the same interface. It is used to detect repeated data in case a client repeats a write due to an error.
     * In this case the data is discarded. But it also detects missing data, which ends up in an error response.
     */
    virtual ReturnCode_t WriteAndReadSpiExtended(const WriteAndReadSpiExtendedVar_t *dataIn, WriteAndReadSpiExtenedReply_t *dataOut) = 0;
};

} // namespace rcp
} // namespace microchip
