package com.yongbeep.app.ui

import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.yongbeep.app.core.ChatMsg
import com.yongbeep.app.core.CoreViewModel
import com.yongbeep.app.core.Session

/**
 * A2 聊天页：头部返回/对方名+状态、消息流（居中灰时间戳 5 分钟规则、
 * own 右绿 r8 气泡+右头像、peer 左白 1px 边气泡+左头像、markdown 简单
 * 粗体/代码渲染、文件卡片）、底部输入区（emoji/附件占位、MD toggle、发送）。
 */
@Composable
fun ChatPage(
    session: Session,
    coreRunning: Boolean,
    onBack: () -> Unit,
    onSend: (text: String, markdown: Boolean) -> Unit,
) {
    BackHandler(onBack = onBack)

    Column(Modifier.fillMaxSize().background(YongColors.Bg)) {
        ChatHeader(session, onBack)
        HorizontalDivider(color = YongColors.Line, thickness = 0.5.dp)
        MessageList(Modifier.weight(1f), session.messages, session.name)
        InputBar(coreRunning, onSend)
    }
}

// ---------------------------------------------------------------------
// 头部
// ---------------------------------------------------------------------

@Composable
private fun ChatHeader(session: Session, onBack: () -> Unit) {
    Surface(color = Color.White) {
        Row(
            Modifier.fillMaxWidth().height(52.dp).padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                "‹",
                fontSize = 26.sp,
                color = YongColors.Ink,
                modifier = Modifier
                    .clipCircle()
                    .clickable(onClick = onBack)
                    .padding(horizontal = 10.dp, vertical = 4.dp),
            )
            Box(
                Modifier
                    .size(34.dp)
                    .background(avatarColor(session.name), RoundedCornerShape(6.dp)),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    session.name.trim().take(1).uppercase(),
                    color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.Medium,
                )
            }
            Spacer(Modifier.width(10.dp))
            Column {
                Text(
                    session.name,
                    fontSize = 17.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = YongColors.Ink,
                    maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
                Text(
                    if (session.online) "● 在线" else "● 离线",
                    fontSize = 11.sp,
                    color = if (session.online) YongColors.Green else YongColors.Sub,
                )
            }
        }
    }
}

private fun Modifier.clipCircle(): Modifier =
    this.then(Modifier.clip(CircleShape))

// ---------------------------------------------------------------------
// 消息流
// ---------------------------------------------------------------------

private sealed class Entry {
    data class Stamp(val text: String) : Entry()
    data class Msg(val msg: ChatMsg) : Entry()
}

@Composable
private fun MessageList(modifier: Modifier, messages: List<ChatMsg>, peerName: String) {
    val entries = remember(messages) { buildEntries(messages) }
    val listState = rememberLazyListState()
    LaunchedEffect(entries.size, messages.lastOrNull()?.id) {
        if (entries.isNotEmpty()) listState.animateScrollToItem(entries.lastIndex)
    }
    LazyColumn(
        modifier = modifier.fillMaxWidth(),
        state = listState,
        contentPadding = PaddingValues(vertical = 10.dp),
    ) {
        items(entries, key = { entry ->
            when (entry) {
                is Entry.Stamp -> "stamp-${entries.indexOf(entry)}-${entry.text}"
                is Entry.Msg -> "msg-${entry.msg.id}"
            }
        }) { entry ->
            when (entry) {
                is Entry.Stamp -> TimestampLine(entry.text)
                is Entry.Msg -> MessageEntry(entry.msg, peerName)
            }
        }
    }
}

/** 桌面 ChatMessageModel 同款规则：首条/间隔>=5min/跨天显示居中时间戳。 */
private fun buildEntries(messages: List<ChatMsg>): List<Entry> {
    val result = mutableListOf<Entry>()
    var prev: Long? = null
    messages.forEach { m ->
        if (shouldShowTimestamp(prev, m.time)) result += Entry.Stamp(formatStampTime(m.time))
        result += Entry.Msg(m)
        prev = m.time
    }
    return result
}

@Composable
private fun TimestampLine(text: String) {
    Box(Modifier.fillMaxWidth().padding(vertical = 6.dp), contentAlignment = Alignment.Center) {
        Text(text, fontSize = 11.sp, color = YongColors.Sub)
    }
}

@Composable
private fun MessageEntry(msg: ChatMsg, peerName: String) {
    when {
        msg.fileCard != null -> FileCardRow(msg)
        msg.system -> SystemLine(msg.text)
        msg.own -> OwnBubble(msg)
        else -> PeerBubble(msg, peerName)
    }
}

/** 居中灰字系统行（对端连接/状态推送等真实核心数据的接收证明）。 */
@Composable
private fun SystemLine(text: String) {
    Box(Modifier.fillMaxWidth().padding(horizontal = 32.dp, vertical = 4.dp), contentAlignment = Alignment.Center) {
        Text(
            text,
            fontSize = 11.sp,
            color = YongColors.Sub,
            lineHeight = 16.sp,
            textAlign = androidx.compose.ui.text.style.TextAlign.Center,
        )
    }
}

@Composable
private fun OwnBubble(msg: ChatMsg) {
    Row(
        Modifier.fillMaxWidth().padding(start = 64.dp, end = 12.dp, top = 4.dp, bottom = 4.dp),
        horizontalArrangement = Arrangement.End,
        verticalAlignment = Alignment.Top,
    ) {
        Bubble(color = YongColors.Bubble, border = null, msg = msg)
        Spacer(Modifier.width(8.dp))
        Avatar(CoreViewModel.NICKNAME)
    }
}

@Composable
private fun PeerBubble(msg: ChatMsg, peerName: String) {
    Row(
        Modifier.fillMaxWidth().padding(start = 12.dp, end = 64.dp, top = 4.dp, bottom = 4.dp),
        horizontalArrangement = Arrangement.Start,
        verticalAlignment = Alignment.Top,
    ) {
        Avatar(peerName)
        Spacer(Modifier.width(8.dp))
        Bubble(color = Color.White, border = YongColors.Line, msg = msg)
    }
}

@Composable
private fun Avatar(name: String) {
    Box(
        Modifier
            .size(36.dp)
            .background(avatarColor(name), RoundedCornerShape(6.dp)),
        contentAlignment = Alignment.Center,
    ) {
        Text(name.trim().take(1).uppercase(), color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.Medium)
    }
}

@Composable
private fun Bubble(color: Color, border: Color?, msg: ChatMsg) {
    val shape = RoundedCornerShape(8.dp)
    Surface(
        color = color,
        shape = shape,
        border = border?.let { androidx.compose.foundation.BorderStroke(1.dp, it) },
        modifier = Modifier.widthIn(max = 264.dp),
    ) {
        Text(
            if (msg.markdown) markdownText(msg.text) else AnnotatedString(msg.text),
            fontSize = 15.sp,
            lineHeight = 21.sp,
            color = YongColors.Ink,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
        )
    }
}

/** 文件卡片：白底圆角 + 图标 + 文件名 + 状态（desktop FileCard 样式）。 */
@Composable
private fun FileCardRow(msg: ChatMsg) {
    val card = msg.fileCard ?: return
    Row(
        Modifier.fillMaxWidth().padding(start = 48.dp, end = 48.dp, top = 4.dp, bottom = 4.dp),
        horizontalArrangement = if (msg.own) Arrangement.End else Arrangement.Start,
    ) {
        Surface(
            color = Color.White,
            shape = RoundedCornerShape(8.dp),
            border = androidx.compose.foundation.BorderStroke(1.dp, YongColors.Line),
            modifier = Modifier.widthIn(max = 264.dp),
        ) {
            Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                Box(
                    Modifier
                        .size(38.dp)
                        .background(Color(0xFFF2F2F2), RoundedCornerShape(6.dp)),
                    contentAlignment = Alignment.Center,
                ) { Text("📄", fontSize = 20.sp) }
                Spacer(Modifier.width(10.dp))
                Column {
                    Text(
                        card.name,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Medium,
                        color = YongColors.Ink,
                        maxLines = 1, overflow = TextOverflow.Ellipsis,
                    )
                    Text(card.status.ifBlank { "文件" }, fontSize = 11.sp, color = YongColors.Sub)
                }
            }
        }
    }
}

// ---------------------------------------------------------------------
// 底部输入区
// ---------------------------------------------------------------------

@Composable
private fun InputBar(coreRunning: Boolean, onSend: (String, Boolean) -> Unit) {
    val context = LocalContext.current
    var text by remember { mutableStateOf("") }
    var markdown by remember { mutableStateOf(false) }

    Surface(color = Color.White) {
        Column(Modifier.navigationBarsPadding().imePadding()) {
            HorizontalDivider(color = YongColors.Line, thickness = 0.5.dp)
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 6.dp, vertical = 6.dp),
                verticalAlignment = Alignment.Bottom,
            ) {
                // 😀 emoji 选择器占位（A4）
                Text(
                    "😀", fontSize = 22.sp,
                    modifier = Modifier
                        .clipCircle()
                        .clickable { Toast.makeText(context, "表情选择将在 A4 提供，敬请期待", Toast.LENGTH_SHORT).show() }
                        .padding(6.dp),
                )
                // 📎 文件发送占位（A3）
                Text(
                    "📎", fontSize = 20.sp,
                    modifier = Modifier
                        .clipCircle()
                        .clickable { Toast.makeText(context, "文件发送将在 A3 提供，敬请期待", Toast.LENGTH_SHORT).show() }
                        .padding(6.dp),
                )
                // MD toggle
                Box(
                    Modifier
                        .padding(4.dp)
                        .background(
                            if (markdown) YongColors.Green else Color.Transparent,
                            RoundedCornerShape(50),
                        )
                        .border(1.dp, if (markdown) YongColors.Green else YongColors.Sub, RoundedCornerShape(50))
                        .clickable { markdown = !markdown }
                        .padding(horizontal = 8.dp, vertical = 3.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        "MD", fontSize = 10.sp, fontWeight = FontWeight.Bold,
                        color = if (markdown) Color.White else YongColors.Sub,
                    )
                }
                Spacer(Modifier.width(2.dp))
                // 输入框
                Box(
                    Modifier
                        .weight(1f)
                        .background(Color(0xFFF5F5F5), RoundedCornerShape(50))
                        .padding(horizontal = 14.dp, vertical = 9.dp),
                ) {
                    if (text.isEmpty()) {
                        Text(
                            if (markdown) "Markdown 消息…" else "消息",
                            fontSize = 14.sp, color = YongColors.Sub,
                        )
                    }
                    BasicTextField(
                        value = text,
                        onValueChange = { text = it },
                        textStyle = LocalTextStyle.current.copy(fontSize = 14.sp, color = YongColors.Ink),
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
                Spacer(Modifier.width(4.dp))
                // 发送按钮：绿圆 ➤；空文本/核心未运行时置灰
                val canSend = coreRunning && text.isNotBlank()
                Box(
                    Modifier
                        .size(36.dp)
                        .background(if (canSend) YongColors.Green else Color(0xFFD9D9D9), CircleShape)
                        .clickable(enabled = canSend) {
                            onSend(text, markdown)
                            text = ""
                        },
                    contentAlignment = Alignment.Center,
                ) {
                    Text("➤", color = Color.White, fontSize = 16.sp)
                }
            }
        }
    }
}

// ---------------------------------------------------------------------
// Markdown 简单渲染：```代码块``` / `行内码` / **粗体**
// ---------------------------------------------------------------------

private fun markdownText(text: String): AnnotatedString = buildAnnotatedString {
    var rest = text
    while (true) {
        val fenceStart = rest.indexOf("```")
        if (fenceStart < 0) {
            appendInline(this, rest)
            break
        }
        val fenceEnd = rest.indexOf("```", fenceStart + 3)
        if (fenceEnd < 0) {
            appendInline(this, rest)
            break
        }
        appendInline(this, rest.substring(0, fenceStart))
        withStyle(
            SpanStyle(
                fontFamily = FontFamily.Monospace,
                fontSize = 12.sp,
                background = Color(0xFFF2F2F2),
            )
        ) {
            append(rest.substring(fenceStart + 3, fenceEnd).trim('\n'))
        }
        rest = rest.substring(fenceEnd + 3)
    }
}

private fun appendInline(b: AnnotatedString.Builder, s: String) {
    var i = 0
    while (i < s.length) {
        when {
            s.startsWith("**", i) -> {
                val end = s.indexOf("**", i + 2)
                if (end > 0) {
                    b.withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { b.append(s.substring(i + 2, end)) }
                    i = end + 2
                } else {
                    b.append(s[i]); i++
                }
            }
            s[i] == '`' -> {
                val end = s.indexOf('`', i + 1)
                if (end > 0) {
                    b.withStyle(
                        SpanStyle(
                            fontFamily = FontFamily.Monospace,
                            fontSize = 12.sp,
                            background = Color(0xFFF2F2F2),
                        )
                    ) { b.append(s.substring(i + 1, end)) }
                    i = end + 1
                } else {
                    b.append(s[i]); i++
                }
            }
            else -> {
                b.append(s[i]); i++
            }
        }
    }
}
