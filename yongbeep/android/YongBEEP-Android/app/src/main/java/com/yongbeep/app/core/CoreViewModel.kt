package com.yongbeep.app.core

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.json.JSONObject

/**
 * 核心事件（解析自 C 侧 JSON；未知类型静默忽略——容错红线）。
 * A2 事件全集见 ybcore_jni.cpp 头注释；此处只列 UI 需要的结构化事件，
 * 其余类型（bridge_ready/connect_attempt/error/...）进事件日志。
 */
sealed class CoreEvent {
    data object Started : CoreEvent()
    data object Stopped : CoreEvent()
    data class MessageReceived(
        val chatId: Long,
        val fromUserId: Long,
        val fromUserName: String,
        val text: String,
        val markdown: Boolean,
        val timestamp: Long,
    ) : CoreEvent()
    data class MessageSent(val chatId: Long, val text: String) : CoreEvent()
    data class MessageSystem(val chatId: Long, val text: String, val timestamp: Long) : CoreEvent()
    data class UserOnline(val userId: Long, val name: String, val status: String) : CoreEvent()
    data class UserOffline(val userId: Long, val name: String) : CoreEvent()
    data class SystemMessage(val text: String) : CoreEvent()
}

/** 微信式文件卡片（接收/传输状态类系统消息里的 <a href> 链接）。 */
data class FileCard(
    val url: String,
    val name: String,
    val status: String,
)

/** 聊天页一条消息。system=true 时居中灰字渲染（无气泡）。 */
data class ChatMsg(
    val id: Long,
    val own: Boolean,
    val text: String,
    val markdown: Boolean,
    val time: Long, // epoch millis（C 侧 ChatMessage 时间戳）
    val system: Boolean = false,
    val fileCard: FileCard? = null,
)

/** 会话列表一行。unread 由 Kotlin 维护（进入聊天页清零）。 */
data class Session(
    val userId: Long,
    val name: String,
    val online: Boolean,
    val privateChatId: Long = 0L,
    val source: String = "LAN",
    val messages: List<ChatMsg> = emptyList(),
    val unread: Int = 0,
) {
    val lastMsg: ChatMsg? get() = messages.lastOrNull()
    val lastActivity: Long get() = lastMsg?.time ?: 0L
}

data class UiState(
    val coreVersion: String = "(加载中)",
    val coreRunning: Boolean = false,
    /** 按最后活跃时间倒序。 */
    val sessions: List<Session> = emptyList(),
    /** 最近的核心事件（单行摘要，设置页冒烟证据）。 */
    val events: List<String> = emptyList(),
)

class CoreViewModel : ViewModel() {

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    /** 当前打开的会话（聊天页）。离开置 null；此时收到的消息计未读。 */
    @Volatile
    var openUserId: Long? = null
        private set

    /** 内存会话模型；历史持久化属 A4。 */
    private val sessions = LinkedHashMap<Long, Session>()
    private var nextMsgId = 1L
    private val eventLog = ArrayDeque<String>()

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

    /** A1.4/A2：手动直连对端（IP:端口），与自动发现同一条连接路径。 */
    fun connectPeer(ip: String, port: Int) {
        CoreBridge.connectPeer(ip, port)
    }

    fun openChat(userId: Long) {
        openUserId = userId
        val s = sessions[userId] ?: return
        sessions[userId] = s.copy(unread = 0)
        publishSessions()
    }

    fun closeChat() {
        openUserId = null
    }

    /** 聊天页发送：乐观上屏（message_sent 回显只进事件日志）。 */
    fun sendChat(userId: Long, text: String, markdown: Boolean) {
        val trimmed = text.trim()
        if (trimmed.isEmpty()) return
        CoreBridge.sendTo(userId, trimmed, markdown)
        appendMessage(userId) {
            ChatMsg(
                id = nextMsgId++,
                own = true,
                text = trimmed,
                markdown = markdown,
                time = System.currentTimeMillis(),
            )
        }
        // A2 冻结前漏掉的一行：不发布状态，LazyColumn 永远不重组——消息发得出去但不上屏
        publishSessions()
    }

    // ------------------------------------------------------------------
    // 事件解析（全部容错：坏 JSON / 缺字段不抛异常）
    // ------------------------------------------------------------------

    private fun onRawEvent(json: String) {
        val obj = runCatching { JSONObject(json) }.getOrNull() ?: return
        val type = obj.optString("type")
        if (type.isEmpty()) return

        when (type) {
            "started" -> _uiState.update { it.copy(coreRunning = true) }
            "stopped" -> {
                _uiState.update { it.copy(coreRunning = false) }
                // 核心停止：全部会话转离线（保留消息便于查看）。
                sessions.keys.toList().forEach { id ->
                    sessions[id]?.let { s -> sessions[id] = s.copy(online = false) }
                }
                publishSessions()
            }
            "chat_users_changed" -> syncUsers(obj)
            "message_received" -> onMessageReceived(obj)
            "message_system" -> onMessageSystem(obj)
            // message_sent：乐观 UI 已上屏，这里只记日志。
        }

        pushEvent(summarize(type, obj))
    }

    /** C 侧权威用户快照 → 会话 presence/名称/私聊 chat 映射。 */
    private fun syncUsers(obj: JSONObject) {
        val users = obj.optJSONArray("users") ?: return
        for (i in 0 until users.length()) {
            val u = users.optJSONObject(i) ?: continue
            val id = u.optLong("user_id")
            if (id <= 0L) continue
            val name = u.optString("name").ifBlank { "user #$id" }
            val connected = u.optBoolean("connected")
            val existing = sessions[id]
            sessions[id] = Session(
                userId = id,
                name = name,
                online = connected,
                privateChatId = u.optLong("private_chat_id"),
                source = existing?.source ?: "LAN",
                messages = existing?.messages ?: emptyList(),
                unread = existing?.unread ?: 0,
            )
        }
        // 无消息且已离线的会话不留（在线发现过的空壳清掉）。
        sessions.values.removeAll { !it.online && it.messages.isEmpty() }
        publishSessions()
    }

    private fun onMessageReceived(obj: JSONObject) {
        val fromId = obj.optLong("from_user_id")
        val name = obj.optString("from_user_name").ifBlank { obj.optString("from_user") }
        if (fromId <= 0L) return
        val msg = ChatMsg(
            id = nextMsgId++,
            own = obj.optBoolean("from_local", false),
            text = obj.optString("text"),
            markdown = obj.optBoolean("markdown"),
            time = obj.optLong("timestamp", System.currentTimeMillis()),
        )
        appendMessage(fromId, fallbackName = name) { msg }
        if (openUserId != fromId) {
            sessions[fromId]?.let { sessions[fromId] = it.copy(unread = it.unread + 1) }
        }
        publishSessions()
    }

    /**
     * chat 作用域系统消息（对端连接/文件传输状态…）。desktop 端按
     * DispatchToDefaultAndPrivateChat 同时投默认 chat 和私聊：私聊 chat_id
     * 命中会话则进该会话；默认 chat（id<=1）广播到所有会话。
     */
    private fun onMessageSystem(obj: JSONObject) {
        val chatId = obj.optLong("chat_id")
        val text = obj.optString("text")
        val time = obj.optLong("timestamp", System.currentTimeMillis())
        val card = if (obj.has("file_name")) FileCard(
            url = obj.optString("file_url"),
            name = obj.optString("file_name"),
            status = obj.optString("file_status"),
        ) else null
        val msg = ChatMsg(
            id = nextMsgId++,
            own = false,
            text = text,
            markdown = false,
            time = time,
            system = true,
            fileCard = card,
        )
        val targets = sessions.values.filter { s ->
            if (chatId > 1L) s.privateChatId == chatId else s.messages.isNotEmpty() || s.online
        }
        if (targets.isEmpty() && chatId <= 1L) return // 尚无会话：只留事件日志
        targets.forEach { s ->
            sessions[s.userId] = s.copy(messages = s.messages + msg)
        }
        publishSessions()
    }

    /** 追加消息；会话不存在时先按 fallbackName 建一个（在线）。 */
    private fun appendMessage(
        sessionUserId: Long,
        fallbackName: String? = null,
        msgFactory: () -> ChatMsg,
    ) {
        val s = sessions[sessionUserId] ?: Session(
            userId = sessionUserId,
            name = fallbackName?.ifBlank { null } ?: "user #$sessionUserId",
            online = true,
        ).also { sessions[sessionUserId] = it }
        sessions[sessionUserId] = s.copy(messages = s.messages + msgFactory())
    }

    private fun publishSessions() {
        _uiState.update { state ->
            state.copy(
                sessions = sessions.values
                    .sortedByDescending { it.lastActivity }
                    .map { if (it.userId == openUserId) it.copy(unread = 0) else it },
            )
        }
    }

    private fun pushEvent(line: String) {
        eventLog.addFirst(line)
        while (eventLog.size > MAX_EVENT_LINES) eventLog.removeLast()
        _uiState.update { it.copy(events = eventLog.toList()) }
    }

    /** 设置页事件映射（A2 补全 A1 遗漏的 user_offline/system/error 等）。 */
    private fun summarize(type: String, obj: JSONObject): String = when (type) {
        "started" -> if (obj.optBoolean("already_running")) "核心已在运行" else "核心已启动 · ${obj.optString("nickname")}"
        "stopped" -> "核心已停止"
        "bridge_ready" -> "JNI 桥就绪 · ${obj.optString("version")}"
        "message_received" -> "收到消息 ${obj.optString("from_user_name")}: ${obj.optString("text")}"
        "message_sent" -> "发出消息 → ${obj.optString("text")}"
        "message_system" -> "会话系统: ${obj.optString("text")}"
        "user_online" -> "用户上线 ${obj.optString("name")}"
        "user_offline" -> "用户离线 ${obj.optString("name")}"
        "local_user_changed" -> "本机用户 ${obj.optString("name")} (${obj.optString("status")})"
        "chat_users_changed" -> "会话用户表更新（${obj.optJSONArray("users")?.length() ?: 0} 人）"
        "system_message" -> "系统: ${obj.optString("text")}"
        "connect_attempt" -> "直连 ${obj.optString("ip")}:${obj.optInt("port")}"
        "error" -> "错误[${obj.optString("stage")}]: ${obj.optString("message")}"
        else -> type
    }

    companion object {
        const val NICKNAME = "yonghu-android"
        private const val MAX_EVENT_LINES = 30
    }
}
