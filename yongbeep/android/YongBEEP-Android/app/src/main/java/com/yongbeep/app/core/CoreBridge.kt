package com.yongbeep.app.core

import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow

/**
 * A1.3 JNI 桥 Kotlin 侧。
 *
 * 加载顺序 = 依赖序（ybcore NEEDED: Qt6Gui/Bluetooth/Network/Core，其中
 * Bluetooth→Network→Core，Gui→Core；System.loadLibrary 不做传递解析）。
 * Qt for Android 的库文件名带 `_arm64-v8a` 后缀。
 *
 * 线程模型：所有 native* 方法只是向专用核心线程投递 queued 调用，可从任意
 * 线程安全调用；[onCoreEvent] 由 C 侧在核心线程通过 AttachCurrentThread
 * 回调，这里只做 tryEmit 转发，不做任何重活。
 */
object CoreBridge {

    /** 核心事件原始 JSON 流（type=started/stopped/message_received/...）。 */
    private val _events = MutableSharedFlow<String>(
        replay = 0,
        extraBufferCapacity = 128,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val events: SharedFlow<String> = _events

    /** nativeVersion() 的真实返回值，类初始化（加载 .so）成功后立即可用。 */
    @Volatile
    var version: String = "(not loaded)"
        private set

    @Volatile
    private var initialized = false

    init {
        System.loadLibrary("Qt6Core_arm64-v8a")
        System.loadLibrary("Qt6Network_arm64-v8a")
        System.loadLibrary("Qt6Gui_arm64-v8a")
        System.loadLibrary("Qt6Bluetooth_arm64-v8a")
        System.loadLibrary("ybcore")
        version = nativeVersion()
    }

    @Synchronized
    fun ensureInit() {
        if (!initialized) {
            nativeInit(this)
            initialized = true
        }
    }

    fun start(nickname: String) {
        ensureInit()
        nativeStart(nickname)
    }

    fun stop() {
        if (initialized) nativeStop()
    }

    fun send(text: String, markdown: Boolean) {
        if (initialized) nativeSend(text, markdown)
    }

    /** A2：定向发送——走该用户的私聊 chat（无则回退默认 chat 广播）。 */
    fun sendTo(userId: Long, text: String, markdown: Boolean) {
        if (initialized) nativeSendTo(userId, text, markdown)
    }

    fun connectPeer(ip: String, port: Int) {
        ensureInit()
        nativeConnectPeer(ip, port)
    }

    /**
     * C 侧回调（核心线程）。实例方法而非 @JvmStatic：C 侧持有的是本单例的
     * jobject 全局引用，走 CallVoidMethod 实例调用路径。
     */
    fun onCoreEvent(json: String) {
        _events.tryEmit(json)
    }

    private external fun nativeInit(callback: Any)
    external fun nativeStart(nickname: String)
    external fun nativeStop()
    external fun nativeSend(text: String, markdown: Boolean)
    external fun nativeSendTo(userId: Long, text: String, markdown: Boolean)
    external fun nativeVersion(): String
    external fun nativeConnectPeer(ip: String, port: Int)
}
