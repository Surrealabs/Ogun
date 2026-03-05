package com.rover.app

import android.app.Application
import android.util.Base64
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.rover.app.ble.BleManager
import com.rover.app.network.WebSocketManager
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch

class RoverViewModel(app: Application) : AndroidViewModel(app) {

    // ---- Sub-managers --------------------------------------
    val ble  = BleManager(app)
    val wifi = WebSocketManager()

    // ---- Observable state ----------------------------------
    private val _connection = MutableStateFlow(ConnectionState())
    val connection: StateFlow<ConnectionState> = _connection.asStateFlow()

    private val _telemetry = MutableStateFlow(Telemetry())
    val telemetry: StateFlow<Telemetry> = _telemetry.asStateFlow()

    private val _otaProgress = MutableStateFlow<OtaProgress?>(null)
    val otaProgress: StateFlow<OtaProgress?> = _otaProgress.asStateFlow()

    private val _log = MutableStateFlow<List<String>>(emptyList())
    val log: StateFlow<List<String>> = _log.asStateFlow()

    // ---- Active transport ----------------------------------
    private var activeTransport: Transport = Transport.NONE

    init {
        // Merge incoming messages from both transports
        viewModelScope.launch {
            merge(ble.incoming, wifi.incoming).collect { json ->
                handleIncoming(json)
            }
        }
        // Track BLE connection state
        viewModelScope.launch {
            ble.connectionState.collect { (connected, name) ->
                if (connected) {
                    activeTransport = Transport.BLE
                    _connection.value = ConnectionState(
                        transport = Transport.BLE,
                        connected = true,
                        deviceName = name,
                    )
                    addLog("BLE connected: $name")
                } else if (activeTransport == Transport.BLE) {
                    activeTransport = Transport.NONE
                    _connection.value = ConnectionState()
                    addLog("BLE disconnected")
                }
            }
        }
        // Track WiFi connection state
        viewModelScope.launch {
            wifi.connectionState.collect { (connected, ip) ->
                if (connected) {
                    activeTransport = Transport.WIFI
                    _connection.value = ConnectionState(
                        transport = Transport.WIFI,
                        connected = true,
                        ip = ip,
                    )
                    addLog("WiFi connected: $ip")
                } else if (activeTransport == Transport.WIFI) {
                    activeTransport = Transport.NONE
                    _connection.value = ConnectionState()
                    addLog("WiFi disconnected")
                }
            }
        }
    }

    // ---- Send helpers (uses whichever transport is active) -
    fun sendJson(json: String) {
        when (activeTransport) {
            Transport.BLE  -> ble.send(json)
            Transport.WIFI -> wifi.send(json)
            else -> addLog("Not connected — command dropped")
        }
    }

    fun sendDrive(x: Float, y: Float, rot: Float) =
        sendJson(RoverProtocol.drive(x, y, rot))

    fun sendEstop() = sendJson(RoverProtocol.estop())

    fun sendGpio(pin: String, state: Boolean) =
        sendJson(RoverProtocol.gpio(pin, state))

    fun sendHorn() = sendGpio("horn", true)

    fun sendAudio(file: String) = sendJson(RoverProtocol.audio(file))

    // ---- OTA firmware upload --------------------------------
    fun uploadFirmware(bytes: ByteArray) {
        viewModelScope.launch {
            val chunkSize = 512
            val total = (bytes.size + chunkSize - 1) / chunkSize
            sendJson(RoverProtocol.otaBegin(total))
            for (i in 0 until total) {
                val from = i * chunkSize
                val to   = minOf(from + chunkSize, bytes.size)
                val chunk = bytes.copyOfRange(from, to)
                val b64 = Base64.encodeToString(chunk, Base64.NO_WRAP)
                sendJson(RoverProtocol.otaChunk(i, b64))
                // Small pacing delay so the Pi doesn't get flooded
                kotlinx.coroutines.delay(20)
            }
            addLog("OTA: all $total chunks sent")
        }
    }

    // ---- Incoming message handler ---------------------------
    private fun handleIncoming(json: String) {
        Telemetry.fromJson(json)?.let { _telemetry.value = it; return }
        OtaProgress.fromJson(json)?.let { _otaProgress.value = it; addLog("OTA: ${it.percent}% ${it.message}"); return }
    }

    private fun addLog(msg: String) {
        _log.value = (_log.value + msg).takeLast(100)
    }

    override fun onCleared() {
        super.onCleared()
        ble.disconnect()
        wifi.disconnect()
    }
}
