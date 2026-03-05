package com.rover.app.network

import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import okhttp3.*
import java.util.concurrent.TimeUnit

private const val TAG = "WebSocketManager"

class WebSocketManager {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private val client = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.MINUTES)    // keep-alive
        .pingInterval(20, TimeUnit.SECONDS)
        .build()

    private var socket: WebSocket? = null
    private var currentIp: String  = ""

    // Observable state
    private val _connectionState = MutableStateFlow(Pair(false, ""))
    val connectionState = _connectionState.asStateFlow()

    private val _incoming = MutableSharedFlow<String>(extraBufferCapacity = 128)
    val incoming = _incoming.asSharedFlow()

    // ---- Connect -------------------------------------------
    fun connect(ip: String, port: Int = 9000) {
        disconnect()
        currentIp = ip
        val req = Request.Builder().url("ws://$ip:$port").build()
        socket = client.newWebSocket(req, object : WebSocketListener() {
            override fun onOpen(ws: WebSocket, response: Response) {
                Log.d(TAG, "WebSocket open: $ip")
                _connectionState.tryEmit(Pair(true, ip))
            }

            override fun onMessage(ws: WebSocket, text: String) {
                _incoming.tryEmit(text)
            }

            override fun onFailure(ws: WebSocket, t: Throwable, response: Response?) {
                Log.e(TAG, "WS failure: ${t.message}")
                _connectionState.tryEmit(Pair(false, ""))
            }

            override fun onClosed(ws: WebSocket, code: Int, reason: String) {
                Log.d(TAG, "WS closed: $reason")
                _connectionState.tryEmit(Pair(false, ""))
            }
        })
    }

    // ---- Disconnect ----------------------------------------
    fun disconnect() {
        socket?.close(1000, "User disconnect")
        socket = null
        _connectionState.tryEmit(Pair(false, ""))
    }

    // ---- Send JSON command ----------------------------------
    fun send(json: String) {
        if (socket?.send(json) == false) {
            Log.w(TAG, "WS send failed — not connected?")
        }
    }

    // ---- Utility: MJPEG stream URL for a camera index ------
    fun mjpegUrl(cam: Int): String =
        "http://$currentIp:${if (cam == 0) 8081 else 8082}/"
}
