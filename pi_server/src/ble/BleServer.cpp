#include "BleServer.hpp"
#include "../Protocol.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <stdexcept>
#include <mutex>

// ---- D-Bus object paths ------------------------------------
static const char* APP_PATH       = "/com/rover/app";
static const char* SERVICE_PATH   = "/com/rover/app/service0";
static const char* CTRL_CHAR_PATH = "/com/rover/app/service0/char0";
static const char* STAT_CHAR_PATH = "/com/rover/app/service0/char1";
static const char* OTA_CHAR_PATH  = "/com/rover/app/service0/char2";
static const char* ADV_PATH       = "/com/rover/advertisement";

static const char* BLUEZ_SERVICE  = "org.bluez";
static const char* GATT_MGR_IFACE = "org.bluez.GattManager1";
static const char* LE_ADV_MGR     = "org.bluez.LEAdvertisingManager1";
static const char* GATT_SVC       = "org.bluez.GattService1";
static const char* GATT_CHAR      = "org.bluez.GattCharacteristic1";
static const char* LE_ADV_IFACE   = "org.bluez.LEAdvertisement1";
static const char* OBJ_MGR_IFACE  = "org.freedesktop.DBus.ObjectManager";
static const char* PROPS_IFACE    = "org.freedesktop.DBus.Properties";

using namespace sdbus;

BleServer::BleServer(const std::string& bleName, const std::string& hciDevice)
    : bleName_(bleName), hciDevice_(hciDevice) {}

BleServer::~BleServer() { stop(); }

// ---- Helper: create a D-Bus object path for hci adapter ---
static std::string adapterPath(const std::string& hci) {
    return "/org/bluez/" + hci;
}

// ---- Build managed-objects map (required by ObjectManager) -
static std::map<ObjectPath, std::map<std::string, std::map<std::string, Variant>>>
buildManagedObjects(
    const std::string& serviceUUID,
    const std::string& ctrlUUID,
    const std::string& statUUID,
    const std::string& otaUUID)
{
    using PropMap   = std::map<std::string, Variant>;
    using IfaceMap  = std::map<std::string, PropMap>;
    using ObjMap    = std::map<ObjectPath, IfaceMap>;

    ObjMap result;

    // --- Service ---
    {
        PropMap p;
        p["UUID"]    = Variant(serviceUUID);
        p["Primary"] = Variant(true);
        result[SERVICE_PATH][GATT_SVC] = p;
    }
    // --- Control characteristic (Write, WriteWithoutResponse) ---
    {
        PropMap p;
        p["UUID"]    = Variant(ctrlUUID);
        p["Service"] = Variant(ObjectPath(SERVICE_PATH));
        p["Flags"]   = Variant(std::vector<std::string>{"write", "write-without-response"});
        p["Value"]   = Variant(std::vector<uint8_t>{});
        result[CTRL_CHAR_PATH][GATT_CHAR] = p;
    }
    // --- Status characteristic (Read, Notify) ---
    {
        PropMap p;
        p["UUID"]    = Variant(statUUID);
        p["Service"] = Variant(ObjectPath(SERVICE_PATH));
        p["Flags"]   = Variant(std::vector<std::string>{"read", "notify"});
        p["Value"]   = Variant(std::vector<uint8_t>{});
        result[STAT_CHAR_PATH][GATT_CHAR] = p;
    }
    // --- OTA characteristic (Write) ---
    {
        PropMap p;
        p["UUID"]    = Variant(otaUUID);
        p["Service"] = Variant(ObjectPath(SERVICE_PATH));
        p["Flags"]   = Variant(std::vector<std::string>{"write", "write-without-response"});
        p["Value"]   = Variant(std::vector<uint8_t>{});
        result[OTA_CHAR_PATH][GATT_CHAR] = p;
    }
    return result;
}

bool BleServer::setupBlueZ() {
    conn_ = createSystemBusConnection();

    // ---- Application root object (ObjectManager) -----------
    appObj_ = createObject(*conn_, APP_PATH);
    appObj_->addVTable(
        registerMethod("GetManagedObjects")
            .onInterface(OBJ_MGR_IFACE)
            .implementedAs([this]() {
                return buildManagedObjects(
                    BleUUID::SERVICE, BleUUID::CONTROL,
                    BleUUID::STATUS_CH, BleUUID::OTA_CH);
            })
    ).forInterface(OBJ_MGR_IFACE);
    appObj_->finishRegistration();

    // ---- Create GattService1 object ------------------------
    serviceObj_ = createObject(*conn_, SERVICE_PATH);
    serviceObj_->addVTable(
        registerProperty("UUID").onInterface(GATT_SVC)
            .withGetter([](){ return std::string(BleUUID::SERVICE); }),
        registerProperty("Primary").onInterface(GATT_SVC)
            .withGetter([](){ return true; })
    ).forInterface(GATT_SVC);
    serviceObj_->finishRegistration();

    // ---- Control characteristic ----------------------------
    controlCharObj_ = createObject(*conn_, CTRL_CHAR_PATH);
    controlCharObj_->addVTable(
        registerProperty("UUID").onInterface(GATT_CHAR)
            .withGetter([](){ return std::string(BleUUID::CONTROL); }),
        registerProperty("Service").onInterface(GATT_CHAR)
            .withGetter([](){ return ObjectPath(SERVICE_PATH); }),
        registerProperty("Flags").onInterface(GATT_CHAR)
            .withGetter([](){
                return std::vector<std::string>{"write","write-without-response"};
            }),
        registerMethod("WriteValue").onInterface(GATT_CHAR)
            .implementedAs([this](std::vector<uint8_t> value,
                                  std::map<std::string, Variant> /*opts*/) {
                std::string json(value.begin(), value.end());
                if (commandCb_) commandCb_(json);
            })
    ).forInterface(GATT_CHAR);
    controlCharObj_->finishRegistration();

    // ---- Status characteristic (notify) --------------------
    statusCharObj_ = createObject(*conn_, STAT_CHAR_PATH);
    statusCharObj_->addVTable(
        registerProperty("UUID").onInterface(GATT_CHAR)
            .withGetter([](){ return std::string(BleUUID::STATUS_CH); }),
        registerProperty("Service").onInterface(GATT_CHAR)
            .withGetter([](){ return ObjectPath(SERVICE_PATH); }),
        registerProperty("Flags").onInterface(GATT_CHAR)
            .withGetter([](){
                return std::vector<std::string>{"read","notify"};
            }),
        registerProperty("Value").onInterface(GATT_CHAR)
            .withGetter([this]() -> std::vector<uint8_t> {
                return latestStatus_;
            }),
        registerProperty("Notifying").onInterface(GATT_CHAR)
            .withGetter([this](){ return notifyEnabled_; }),
        registerMethod("ReadValue").onInterface(GATT_CHAR)
            .implementedAs([this](std::map<std::string, Variant> /*opts*/)
                           -> std::vector<uint8_t> {
                return latestStatus_;
            }),
        registerMethod("StartNotify").onInterface(GATT_CHAR)
            .implementedAs([this]() {
                notifyEnabled_ = true;
                std::cout << "[ble] notify enabled on STATUS\n";
            }),
        registerMethod("StopNotify").onInterface(GATT_CHAR)
            .implementedAs([this]() {
                notifyEnabled_ = false;
                std::cout << "[ble] notify disabled\n";
            })
    ).forInterface(GATT_CHAR);
    statusCharObj_->finishRegistration();

    // ---- OTA characteristic --------------------------------
    otaCharObj_ = createObject(*conn_, OTA_CHAR_PATH);
    otaCharObj_->addVTable(
        registerProperty("UUID").onInterface(GATT_CHAR)
            .withGetter([](){ return std::string(BleUUID::OTA_CH); }),
        registerProperty("Service").onInterface(GATT_CHAR)
            .withGetter([](){ return ObjectPath(SERVICE_PATH); }),
        registerProperty("Flags").onInterface(GATT_CHAR)
            .withGetter([](){
                return std::vector<std::string>{"write","write-without-response"};
            }),
        registerMethod("WriteValue").onInterface(GATT_CHAR)
            .implementedAs([this](std::vector<uint8_t> value,
                                  std::map<std::string, Variant> /*opts*/) {
                if (otaChunkCb_) otaChunkCb_(value);
            })
    ).forInterface(GATT_CHAR);
    otaCharObj_->finishRegistration();

    return true;
}

void BleServer::registerApp() {
    std::string adapterObjPath = adapterPath(hciDevice_);
    auto proxy = createProxy(*conn_, BLUEZ_SERVICE, adapterObjPath);
    proxy->callMethod("RegisterApplication")
         .onInterface(GATT_MGR_IFACE)
         .withArguments(ObjectPath(APP_PATH),
                        std::map<std::string, Variant>{});
    std::cout << "[ble] GATT application registered\n";
}

void BleServer::startAdvertising() {
    // Create LE advertisement object
    auto advObj = createObject(*conn_, ADV_PATH);
    advObj->addVTable(
        registerProperty("Type").onInterface(LE_ADV_IFACE)
            .withGetter([](){ return std::string("peripheral"); }),
        registerProperty("LocalName").onInterface(LE_ADV_IFACE)
            .withGetter([this](){ return bleName_; }),
        registerProperty("ServiceUUIDs").onInterface(LE_ADV_IFACE)
            .withGetter([](){
                return std::vector<std::string>{BleUUID::SERVICE};
            }),
        registerMethod("Release").onInterface(LE_ADV_IFACE)
            .implementedAs([](){})
    ).forInterface(LE_ADV_IFACE);
    advObj->finishRegistration();

    std::string adapterObjPath = adapterPath(hciDevice_);
    auto proxy = createProxy(*conn_, BLUEZ_SERVICE, adapterObjPath);
    proxy->callMethod("RegisterAdvertisement")
         .onInterface(LE_ADV_MGR)
         .withArguments(ObjectPath(ADV_PATH),
                        std::map<std::string, Variant>{});
    std::cout << "[ble] advertising as '" << bleName_ << "'\n";
}

bool BleServer::start() {
    try {
        setupBlueZ();
        registerApp();
        startAdvertising();
        running_ = true;
        loopThread_ = std::thread([this]() {
            while (running_) conn_->processPendingRequest();
        });
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[ble] start failed: " << ex.what() << "\n";
        return false;
    }
}

void BleServer::stop() {
    running_ = false;
    if (conn_) conn_->leaveEventLoop();
    if (loopThread_.joinable()) loopThread_.join();
}

void BleServer::notifyStatus(const std::string& json) {
    latestStatus_.assign(json.begin(), json.end());
    if (!notifyEnabled_ || !statusCharObj_) return;
    try {
        // Emit PropertiesChanged to trigger notification
        statusCharObj_->emitPropertiesChangedSignal(
            GATT_CHAR, {"Value"});
    } catch (...) {}
}
