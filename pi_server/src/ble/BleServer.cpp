#include "BleServer.hpp"
#include "../Protocol.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <stdexcept>
#include <mutex>

// ---- D-Bus object paths ------------------------------------
static const sdbus::ObjectPath APP_PATH       {"/com/rover/app"};
static const sdbus::ObjectPath SERVICE_PATH   {"/com/rover/app/service0"};
static const sdbus::ObjectPath CTRL_CHAR_PATH {"/com/rover/app/service0/char0"};
static const sdbus::ObjectPath STAT_CHAR_PATH {"/com/rover/app/service0/char1"};
static const sdbus::ObjectPath OTA_CHAR_PATH  {"/com/rover/app/service0/char2"};
static const sdbus::ObjectPath ADV_PATH       {"/com/rover/advertisement"};

static const sdbus::ServiceName BLUEZ_SERVICE {"org.bluez"};
static const sdbus::InterfaceName GATT_MGR_IFACE {"org.bluez.GattManager1"};
static const sdbus::InterfaceName LE_ADV_MGR     {"org.bluez.LEAdvertisingManager1"};
static const sdbus::InterfaceName GATT_SVC       {"org.bluez.GattService1"};
static const sdbus::InterfaceName GATT_CHAR      {"org.bluez.GattCharacteristic1"};
static const sdbus::InterfaceName LE_ADV_IFACE   {"org.bluez.LEAdvertisement1"};
static const sdbus::InterfaceName OBJ_MGR_IFACE  {"org.freedesktop.DBus.ObjectManager"};
static const sdbus::InterfaceName PROPS_IFACE    {"org.freedesktop.DBus.Properties"};

using namespace sdbus;

BleServer::BleServer(const std::string& bleName, const std::string& hciDevice)
    : bleName_(bleName), hciDevice_(hciDevice) {}

BleServer::~BleServer() { stop(); }

// ---- Helper: create a D-Bus object path for hci adapter ---
static ObjectPath adapterPath(const std::string& hci) {
    return ObjectPath("/org/bluez/" + hci);
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
        p["Service"] = Variant(SERVICE_PATH);
        p["Flags"]   = Variant(std::vector<std::string>{"write", "write-without-response"});
        p["Value"]   = Variant(std::vector<uint8_t>{});
        result[CTRL_CHAR_PATH][GATT_CHAR] = p;
    }
    // --- Status characteristic (Read, Notify) ---
    {
        PropMap p;
        p["UUID"]    = Variant(statUUID);
        p["Service"] = Variant(SERVICE_PATH);
        p["Flags"]   = Variant(std::vector<std::string>{"read", "notify"});
        p["Value"]   = Variant(std::vector<uint8_t>{});
        result[STAT_CHAR_PATH][GATT_CHAR] = p;
    }
    // --- OTA characteristic (Write) ---
    {
        PropMap p;
        p["UUID"]    = Variant(otaUUID);
        p["Service"] = Variant(SERVICE_PATH);
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
            .implementedAs([this]() {
                return buildManagedObjects(
                    BleUUID::SERVICE, BleUUID::CONTROL,
                    BleUUID::STATUS_CH, BleUUID::OTA_CH);
            })
    ).forInterface(OBJ_MGR_IFACE);

    // ---- Create GattService1 object ------------------------
    serviceObj_ = createObject(*conn_, SERVICE_PATH);
    serviceObj_->addVTable(
        registerProperty("UUID")
            .withGetter([](){ return std::string(BleUUID::SERVICE); }),
        registerProperty("Primary")
            .withGetter([](){ return true; })
    ).forInterface(GATT_SVC);

    // ---- Control characteristic ----------------------------
    controlCharObj_ = createObject(*conn_, CTRL_CHAR_PATH);
    controlCharObj_->addVTable(
        registerProperty("UUID")
            .withGetter([](){ return std::string(BleUUID::CONTROL); }),
        registerProperty("Service")
            .withGetter([](){ return SERVICE_PATH; }),
        registerProperty("Flags")
            .withGetter([](){
                return std::vector<std::string>{"write","write-without-response"};
            }),
        registerMethod("WriteValue")
            .implementedAs([this](std::vector<uint8_t> value,
                                  std::map<std::string, Variant> /*opts*/) {
                std::string json(value.begin(), value.end());
                if (commandCb_) commandCb_(json);
            })
    ).forInterface(GATT_CHAR);

    // ---- Status characteristic (notify) --------------------
    statusCharObj_ = createObject(*conn_, STAT_CHAR_PATH);
    statusCharObj_->addVTable(
        registerProperty("UUID")
            .withGetter([](){ return std::string(BleUUID::STATUS_CH); }),
        registerProperty("Service")
            .withGetter([](){ return SERVICE_PATH; }),
        registerProperty("Flags")
            .withGetter([](){
                return std::vector<std::string>{"read","notify"};
            }),
        registerProperty("Value")
            .withGetter([this]() -> std::vector<uint8_t> {
                return latestStatus_;
            }),
        registerProperty("Notifying")
            .withGetter([this](){ return notifyEnabled_; }),
        registerMethod("ReadValue")
            .implementedAs([this](std::map<std::string, Variant> /*opts*/)
                           -> std::vector<uint8_t> {
                return latestStatus_;
            }),
        registerMethod("StartNotify")
            .implementedAs([this]() {
                notifyEnabled_ = true;
                std::cout << "[ble] notify enabled on STATUS\n";
            }),
        registerMethod("StopNotify")
            .implementedAs([this]() {
                notifyEnabled_ = false;
                std::cout << "[ble] notify disabled\n";
            })
    ).forInterface(GATT_CHAR);

    // ---- OTA characteristic --------------------------------
    otaCharObj_ = createObject(*conn_, OTA_CHAR_PATH);
    otaCharObj_->addVTable(
        registerProperty("UUID")
            .withGetter([](){ return std::string(BleUUID::OTA_CH); }),
        registerProperty("Service")
            .withGetter([](){ return SERVICE_PATH; }),
        registerProperty("Flags")
            .withGetter([](){
                return std::vector<std::string>{"write","write-without-response"};
            }),
        registerMethod("WriteValue")
            .implementedAs([this](std::vector<uint8_t> value,
                                  std::map<std::string, Variant> /*opts*/) {
                if (otaChunkCb_) otaChunkCb_(value);
            })
    ).forInterface(GATT_CHAR);

    return true;
}

void BleServer::registerApp() {
    auto adapterObjPath = adapterPath(hciDevice_);
    auto proxy = createProxy(*conn_, BLUEZ_SERVICE, adapterObjPath);
    proxy->callMethod("RegisterApplication")
         .onInterface(GATT_MGR_IFACE)
         .withArguments(APP_PATH,
                        std::map<std::string, Variant>{});
    std::cout << "[ble] GATT application registered\n";
}

void BleServer::startAdvertising() {
    // Create LE advertisement object
    auto advObj = createObject(*conn_, ADV_PATH);
    advObj->addVTable(
        registerProperty("Type")
            .withGetter([](){ return std::string("peripheral"); }),
        registerProperty("LocalName")
            .withGetter([this](){ return bleName_; }),
        registerProperty("ServiceUUIDs")
            .withGetter([](){
                return std::vector<std::string>{BleUUID::SERVICE};
            }),
        registerMethod("Release")
            .implementedAs([](){})
    ).forInterface(LE_ADV_IFACE);

    auto adapterObjPath = adapterPath(hciDevice_);
    auto proxy = createProxy(*conn_, BLUEZ_SERVICE, adapterObjPath);
    proxy->callMethod("RegisterAdvertisement")
         .onInterface(LE_ADV_MGR)
         .withArguments(ADV_PATH,
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
            while (running_) conn_->processPendingEvent();
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
        statusCharObj_->emitPropertiesChangedSignal(
            GATT_CHAR, {"Value"});
    } catch (...) {}
}
