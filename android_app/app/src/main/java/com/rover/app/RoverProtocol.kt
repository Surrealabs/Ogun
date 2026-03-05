package com.rover.app

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject

// ============================================================
//  Shared rover protocol constants & message builders
// ============================================================
object RoverProtocol {
    // BLE service / characteristic UUIDs (must match Pi proto)
    const val SERVICE_UUID = "0000ABCD-0000-1000-8000-00805F9B34FB"
    const val CTRL_UUID    = "0000ABD0-0000-1000-8000-00805F9B34FB"
    const val STATUS_UUID  = "0000ABD1-0000-1000-8000-00805F9B34FB"
    const val OTA_UUID     = "0000ABD2-0000-1000-8000-00805F9B34FB"

    // Camera stream URLs (filled in by UI based on rover IP)
    fun mjpegUrl(ip: String, cam: Int): String =
        "http://$ip:${if (cam == 0) 8081 else 8082}/"

    // WebSocket URL
    fun wsUrl(ip: String): String = "ws://$ip:9000"

    // ---- Outbound messages -----------------------------------
    fun drive(x: Float, y: Float, rot: Float): String =
        """{"type":"drive","x":$x,"y":$y,"rot":$rot}"""

    fun gpio(pin: String, state: Boolean): String =
        """{"type":"gpio","pin":"$pin","state":$state}"""

    fun estop(): String = """{"type":"estop"}"""

    fun statusReq(): String = """{"type":"status"}"""

    fun otaBegin(totalChunks: Int): String =
        """{"type":"ota_begin","total":$totalChunks}"""

    fun otaChunk(index: Int, base64Data: String): String =
        """{"type":"ota","chunk":$index,"data":"$base64Data"}"""

    fun audio(file: String): String =
        """{"type":"audio","file":"$file"}"""
}

// ---- Telemetry data class ----------------------------------
data class Telemetry(
    val encL   : Long  = 0L,
    val encR   : Long  = 0L,
    val voltage: Float = 0f,
    val current: Float = 0f,
    val temp   : Float = 0f,
    val horn   : Boolean = false,
    val ledFwd : Boolean = false,
) {
    companion object {
        fun fromJson(json: String): Telemetry? = runCatching {
            val o = JSONObject(json)
            if (o.optString("type") != "telemetry") return null
            Telemetry(
                encL    = o.optLong("enc_l"),
                encR    = o.optLong("enc_r"),
                voltage = o.optDouble("volt", 0.0).toFloat(),
                current = o.optDouble("curr", 0.0).toFloat(),
                temp    = o.optDouble("temp", 0.0).toFloat(),
                horn    = o.optBoolean("horn"),
                ledFwd  = o.optBoolean("led_fwd"),
            )
        }.getOrNull()
    }
}

// ---- OTA progress data class --------------------------------
data class OtaProgress(val percent: Int, val message: String) {
    companion object {
        fun fromJson(json: String): OtaProgress? = runCatching {
            val o = JSONObject(json)
            if (o.optString("type") != "ota_prog") return null
            OtaProgress(o.optInt("pct"), o.optString("msg", ""))
        }.getOrNull()
    }
}

// ---- Connection state --------------------------------------
enum class Transport { NONE, BLE, WIFI }

data class ConnectionState(
    val transport: Transport = Transport.NONE,
    val connected: Boolean   = false,
    val deviceName: String   = "",
    val ip: String           = "",
)
