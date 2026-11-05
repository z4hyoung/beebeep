package com.yongbeep.app.ui

import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.yongbeep.app.core.Session

/**
 * A2 会话列表页（替换 A1ChatPage）：
 * 搜索胶囊过滤 + 微信式会话行（40dp 圆角头像/粗体名/单行摘要/时间/
 * 红色未读角标 99+/LAN 来源小标/按下#E2E2E2）+ 底部绿色「＋ 扫描设备」胶囊（A3 占位）。
 */
@Composable
fun SessionsPage(
    sessions: List<Session>,
    onOpenSession: (Long) -> Unit,
) {
    val context = LocalContext.current
    var query by remember { mutableStateOf("") }
    val filtered = remember(sessions, query) {
        if (query.isBlank()) sessions
        else sessions.filter { it.name.contains(query, ignoreCase = true) }
    }

    Box(Modifier.fillMaxSize().background(YongColors.Bg)) {
        Column(Modifier.fillMaxSize()) {
            // 顶部搜索胶囊
            Surface(color = Color.White, modifier = Modifier.fillMaxWidth()) {
                Box(
                    Modifier
                        .padding(horizontal = 12.dp, vertical = 8.dp)
                        .fillMaxWidth()
                        .height(32.dp)
                        .background(Color(0xFFF5F5F5), RoundedCornerShape(50)),
                        contentAlignment = Alignment.CenterStart,
                ) {
                    Row(
                        Modifier.padding(horizontal = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text("🔍", fontSize = 13.sp)
                        Spacer(Modifier.width(6.dp))
                        SearchField(query, { query = it }, "搜索")
                    }
                }
            }
            HorizontalDivider(color = YongColors.Line, thickness = 0.5.dp)

            if (sessions.isEmpty()) {
                EmptySessions()
            } else {
                LazyColumn(Modifier.fillMaxSize()) {
                    items(filtered, key = { it.userId }) { session ->
                        SessionRow(session, onClick = { onOpenSession(session.userId) })
                        HorizontalDivider(
                            color = YongColors.Line.copy(alpha = 0.6f),
                            thickness = 0.5.dp,
                            modifier = Modifier.padding(start = 68.dp),
                        )
                    }
                }
            }
        }

        // 底部绿色「＋ 扫描设备」胶囊（A3 前占位）
        Surface(
            onClick = {
                Toast.makeText(context, "设备扫描将在 A3 提供，敬请期待", Toast.LENGTH_SHORT).show()
            },
            shape = RoundedCornerShape(50),
            color = YongColors.Green,
            shadowElevation = 6.dp,
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = 20.dp),
        ) {
            Text(
                "＋ 扫描设备",
                color = Color.White,
                fontSize = 15.sp,
                fontWeight = FontWeight.Medium,
                modifier = Modifier.padding(horizontal = 22.dp, vertical = 10.dp),
            )
        }
    }
}

/** 无边框透明搜索输入（外层胶囊自绘）。 */
@Composable
private fun SearchField(value: String, onValueChange: (String) -> Unit, hint: String) {
    var focused by remember { mutableStateOf(false) }
    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        singleLine = true,
        textStyle = LocalTextStyle.current.copy(fontSize = 14.sp, color = YongColors.Ink),
        decorationBox = { inner ->
            Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.CenterStart) {
                if (!focused && value.isEmpty()) {
                    Text(hint, fontSize = 13.sp, color = YongColors.Sub)
                }
                inner()
            }
        },
        modifier = Modifier
            .fillMaxWidth()
            .onFocusChanged { focused = it.isFocused },
    )
}

@Composable
private fun EmptySessions() {
    Column(
        Modifier.fillMaxSize().padding(horizontal = 40.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text("📭", fontSize = 40.sp)
        Spacer(Modifier.height(14.dp))
        Text(
            "暂无会话——在设置页启动核心后等待发现，\n或从设备页直连",
            fontSize = 14.sp,
            color = YongColors.Sub,
            lineHeight = 22.sp,
        )
        Spacer(Modifier.height(8.dp))
        Text(
            "开发者：长按顶部标题可手动直连对端",
            fontSize = 11.sp,
            color = YongColors.Sub.copy(alpha = 0.7f),
        )
    }
}

@Composable
private fun SessionRow(session: Session, onClick: () -> Unit) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    Row(
        Modifier
            .fillMaxWidth()
            .background(if (pressed) YongColors.Selected else Color.Transparent)
            .clickable(interactionSource = interaction, indication = null, onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // 40dp 圆角头像 + 未读角标
        Box {
            Box(
                Modifier
                    .size(40.dp)
                    .background(avatarColor(session.name), RoundedCornerShape(8.dp)),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    session.name.trim().take(1).uppercase(),
                    color = Color.White,
                    fontSize = 17.sp,
                    fontWeight = FontWeight.Medium,
                )
            }
            if (session.unread > 0) {
                Box(
                    Modifier
                        .align(Alignment.TopEnd)
                        .offset(x = 4.dp, y = (-4).dp)
                        .background(YongColors.Red, RoundedCornerShape(50))
                        .padding(horizontal = 5.dp, vertical = 1.dp),
                ) {
                    Text(
                        if (session.unread > 99) "99+" else "${session.unread}",
                        color = Color.White,
                        fontSize = 10.sp,
                    )
                }
            }
        }
        Spacer(Modifier.width(12.dp))

        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    session.name,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = if (session.online) YongColors.Ink else YongColors.MsgGray,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false),
                )
                Spacer(Modifier.width(6.dp))
                // 来源小标（LAN；蓝牙来源 A3 接入）
                Box(
                    Modifier
                        .border(0.5.dp, YongColors.Sub.copy(alpha = 0.6f), RoundedCornerShape(2.dp))
                        .padding(horizontal = 3.dp),
                ) {
                    Text(session.source, fontSize = 8.sp, color = YongColors.Sub, lineHeight = 10.sp)
                }
                Spacer(Modifier.weight(1f))
                Text(
                    formatSessionTime(session.lastActivity),
                    fontSize = 11.sp,
                    color = YongColors.Sub,
                )
            }
            Spacer(Modifier.height(3.dp))
            Text(
                session.lastMsg?.let { m ->
                    when {
                        m.fileCard != null -> "[文件] ${m.fileCard.name}"
                        m.system -> m.text
                        m.own -> "我: ${m.text}"
                        else -> m.text
                    }
                } ?: (if (session.online) "已连接，等待消息…" else "离线"),
                fontSize = 13.sp,
                color = YongColors.MsgGray,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}
