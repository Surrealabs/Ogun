#pragma once
// ============================================================
//  BleUart — Nordic UART Service (NUS) for nRF52840
//
//  Provides a BLE serial-like interface compatible with the
//  rover JSON protocol.  Line-buffered: fires callback on '\n'.
//
//  Uses Adafruit nRF52 BLEUart built-in service.
// ============================================================
#include <bluefruit.h>

class BleUart {
public:
    using LineCb = void(*)(const char* line);

    void begin(const char* deviceName, LineCb onLine) {
        onLine_ = onLine;

        Bluefruit.begin();
        Bluefruit.setTxPower(4);
        Bluefruit.setName(deviceName);

        // Nordic UART Service
        bleuart_.begin();

        // Peripheral callbacks
        Bluefruit.Periph.setConnectCallback(connectCb);
        Bluefruit.Periph.setDisconnectCallback(disconnectCb);

        // Advertising
        Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
        Bluefruit.Advertising.addTxPower();
        Bluefruit.Advertising.addService(bleuart_);
        Bluefruit.ScanResponse.addName();
        Bluefruit.Advertising.restartOnDisconnect(true);
        Bluefruit.Advertising.setInterval(32, 244);  // fast then slow (units of 0.625 ms)
        Bluefruit.Advertising.setFastTimeout(30);     // 30 s fast advertising
        Bluefruit.Advertising.start(0);                // 0 = never stop
    }

    // Call from loop() — reads BLE data and fires line callback
    void poll() {
        while (bleuart_.available()) {
            char c = (char)bleuart_.read();
            if (c == '\n' || c == '\r') {
                if (rxIdx_ > 0) {
                    rxBuf_[rxIdx_] = '\0';
                    if (onLine_) onLine_(rxBuf_);
                    rxIdx_ = 0;
                }
            } else if (rxIdx_ < sizeof(rxBuf_) - 1) {
                rxBuf_[rxIdx_++] = c;
            } else {
                rxIdx_ = 0;  // overflow — discard
            }
        }
    }

    // Send a line over BLE UART (adds newline)
    void println(const char* msg) {
        if (Bluefruit.connected()) {
            bleuart_.print(msg);
            bleuart_.print('\n');
            bleuart_.flush();
        }
    }

    bool connected() const { return Bluefruit.connected(); }

private:
    static void connectCb(uint16_t connHandle) {
        (void)connHandle;
    }
    static void disconnectCb(uint16_t connHandle, uint8_t reason) {
        (void)connHandle;
        (void)reason;
    }

    BLEUart bleuart_;
    LineCb  onLine_ = nullptr;
    char    rxBuf_[256];
    uint8_t rxIdx_ = 0;
};
