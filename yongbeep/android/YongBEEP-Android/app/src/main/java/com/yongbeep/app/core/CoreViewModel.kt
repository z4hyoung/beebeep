package com.yongbeep.app.core

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.json.JSONObject

/** 核心事件（解析自 C 侧 JSON；未知类型静默忽略——容错红线）。 */
sealed class CoreEvent {
    data object Started : CoreEvent()
    data object Stopped : CoreEvent()
    data class MessageReceived(
        val chatId: Long,
        val fromUserId: Long,
        val fromUserName: String,
        val text: String,
        val markdown: Boolean,
    ) : CoreEvent()
    data class MessageSent(val chatId: Long, val text: String) : CoreEvent()
    data class UserOnline(val userId: Long, val name: String, val status: String) : CoreEvent()
    data class UserOffline(val userId: Long, val name: String) : CoreEvent()
    data class SystemMessage(val text: String) : CoreEvent()
}

data class UiState(
    val coreVersion: String = "(加载中)",
    val coreRunning: Boolean = false,
    /** 最近的核心事件（单行摘要，设置页冒烟证据）。 */
    val events: List<String> = emptyList(),
    val lastMessage: CoreEvent.MessageReceived? = null,
)

class CoreViewModel : ViewModel() {

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    init {
        // 触发 CoreBridge 类初始化（加载 5 个 .so + nativeVersion 自检）。
        _uiState.update { state ->
            state.copy(
                coreVersion = runCatching { CoreBridge.version }
                    .getOrElse { "加载失败: ${it.message}" },
            )
        }
        viewModelScope.launch {
            CoreBridge.events.collect { json -> onRawEvent(json) }
        }
    }

    fun startCore() {
        CoreBridge.start(NICKNAME)
    }

    fun stopCore() {
        CoreBridge.stop()
    }

    /** A1.4：手动直连对端（IP:端口），与自动发现同一条连接路径。 */
    fun connectPeer(ip: String, port: Int) {
        CoreBridge.connectPeer(ip, port)
    }

    /** A1.4：发送一条文本消息（markdown 关闭）。 */
    fun sendChat(text: String) {
        CoreBridge.send(text, false)
    }

    private fun onRawEvent(json: String) {
        // 容错：坏 JSON / 未知 type 一律忽略，不抛异常。
        val obj = runCatching { JSONObject(json) }.getOrNull() ?: return
        val type = obj.optString("type")
        if (type.isEmpty()) return

        val event: CoreEvent? = when (type) {
            "started" -> CoreEvent.Started
            "stopped" -> CoreEvent.Stopped
            "message_received" -> CoreEvent.MessageReceived(
                chatId = obj.optLong("chat_id"),
                fromUserId = obj.optLong("from_user_id"),
                fromUserName = obj.optString("from_user_name"),
                text = obj.optString("text"),
                markdown = obj.optBoolean("markdown"),
            )
            "message_sent" -> CoreEvent.MessageSent(
                chatId = obj.optLong("chat_id"),
                text = obj.optString("text"),
            )
            "user_online", "local_user_changed" -> CoreEvent.UserOnline(
                userId = obj.optLong("user_id"),
                name = obj.optString("name"),
                status = obj.optString("status"),
            )
            "user_offline" -> CoreEvent.UserOffline(
                userId = obj.optLong("user_id"),
                name = obj.optString("name"),
            )
            "system_message" -> CoreEvent.SystemMessage(obj.optString("text"))
            else -> null // bridge_ready / connect_attempt / error / 未知类型：不进事件流
        }

        _uiState.update { state ->
            state.copy(
                coreRunning = when (type) {
                    "started" -> true
                    "stopped" -> false
                    else -> state.coreRunning
                },
                lastMessage = (event as? CoreEvent.MessageReceived) ?: state.lastMessage,
                events = (listOf(summarize(type, obj)) + state.events).take(MAX_EVENT_LINES),
            )
        }
    }

    private fun summarize(type: String, obj: JSONObject): String = when (type) {
        "started" -> "核心已启动 · ${obj.optString("nickname")}"
        "stopped" -> "核心已停止"
        "message_received" -> "收到消息 ${obj.optString("from_user_name")}: ${obj.optString("text")}"
        "message_sent" -> "发出消息 → ${obj.optString("text")}"
        "user_online" -> "用户上线 ${obj.optString("name")}"
        "user_offline" -> "用户离线 ${obj.optString("name")}"
        "local_user_changed" -> "本机用户 ${obj.optString("name")} (${obj.optString("status")})"
        "system_message" -> "系统: ${obj.optString("text")}"
        else -> type
    }

    companion object {
        const val NICKNAME = "yonghu-android"
        private const val MAX_EVENT_LINES = 30
    }
}
