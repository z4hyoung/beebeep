package com.yongbeep.app.ui

import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

/**
 * 微信色板（与桌面端 YongTheme / 已批准 HTML 原型一致）。
 * #07C160 主绿 / #95EC69 气泡绿 / #FA5151 警示红 / #191919 墨字 /
 * #B2B2B2 次要字 / #E7E7E7 分隔线 / #F5F5F5 页面底 / #E2E2E2 选中态。
 */
object YongColors {
    val Green = androidx.compose.ui.graphics.Color(0xFF07C160)
    val Bubble = androidx.compose.ui.graphics.Color(0xFF95EC69)
    val Red = androidx.compose.ui.graphics.Color(0xFFFA5151)
    val Ink = androidx.compose.ui.graphics.Color(0xFF191919)
    val Sub = androidx.compose.ui.graphics.Color(0xFFB2B2B2)
    val MsgGray = androidx.compose.ui.graphics.Color(0xFF9B9B9B)
    val Line = androidx.compose.ui.graphics.Color(0xFFE7E7E7)
    val Bg = androidx.compose.ui.graphics.Color(0xFFF5F5F5)
    val Selected = androidx.compose.ui.graphics.Color(0xFFE2E2E2)
    val White = androidx.compose.ui.graphics.Color.White
}

/** 会话头像底色：按名称哈希从微信系低饱和色板取色，保证同名同色。 */
fun avatarColor(name: String): androidx.compose.ui.graphics.Color {
    val palette = listOf(
        0xFF7EC0EE, 0xFF8FD4A0, 0xFFF0B26B, 0xFFB3A2E8,
        0xFF7EC8B8, 0xFFE59BA4, 0xFF9BB7D9, 0xFFC9BA8E,
    )
    val idx = (name.hashCode().let { if (it < 0) -it else it }) % palette.size
    return androidx.compose.ui.graphics.Color(palette[idx])
}

private fun LocalDate(millis: Long): LocalDate =
    Instant.ofEpochMilli(millis).atZone(ZoneId.systemDefault()).toLocalDate()

private fun hhmm(millis: Long): String {
    val t = Instant.ofEpochMilli(millis).atZone(ZoneId.systemDefault())
    return String.format("%02d:%02d", t.hour, t.minute)
}

/** 会话列表行时间：今天 hh:mm / 昨天 / 今年 MM-dd / 更早 yyyy-MM-dd（同桌面 ChatListModel）。 */
fun formatSessionTime(millis: Long): String {
    val date = LocalDate(millis)
    val today = LocalDate(System.currentTimeMillis())
    return when {
        date == today -> hhmm(millis)
        date == today.minusDays(1) -> "昨天"
        date.year == today.year -> String.format("%02d-%02d", date.monthValue, date.dayOfMonth)
        else -> date.toString()
    }
}

/** 聊天页居中时间戳：今天 hh:mm，更早 "MM-dd hh:mm"（同桌面 timestampTextFor）。 */
fun formatStampTime(millis: Long): String {
    val date = LocalDate(millis)
    val today = LocalDate(System.currentTimeMillis())
    return if (date == today) hhmm(millis)
    else String.format("%02d-%02d %s", date.monthValue, date.dayOfMonth, hhmm(millis))
}

/** 桌面 ChatMessageModel 的 5 分钟规则：首条 / 间隔>=5min / 跨天 时显示时间戳。 */
fun shouldShowTimestamp(prevMillis: Long?, curMillis: Long): Boolean {
    if (prevMillis == null) return true
    if (curMillis - prevMillis >= 5 * 60 * 1000L) return true
    return LocalDate(prevMillis) != LocalDate(curMillis)
}
