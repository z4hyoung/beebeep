package com.yongbeep.app.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.yongbeep.app.core.CoreViewModel
import com.yongbeep.app.core.UiState

/**
 * A2 设置页（A1.3 迁移 + 事件映射补全）：
 * 核心版本/状态/启停 + 核心事件流（user_online/message_system/
 * chat_users_changed/error 等 A1 遗漏类型已在 CoreViewModel 补全）。
 */
@Composable
fun SettingsPage(
    state: UiState,
    onStart: () -> Unit,
    onStop: () -> Unit,
) {
    Column(
        Modifier
            .fillMaxSize()
            .padding(20.dp)
            .verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Text("核心引擎", fontSize = 20.sp, fontWeight = FontWeight.SemiBold, color = YongColors.Ink)

        Surface(color = Color.White, shape = RoundedCornerShape(10.dp), tonalElevation = 1.dp) {
            Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SettingRow("核心版本", state.coreVersion, mono = true)
                HorizontalDivider(color = Color(0xFFF0F0F0))
                SettingRow(
                    "核心状态", if (state.coreRunning) "运行中" else "已停止",
                    valueColor = if (state.coreRunning) YongColors.Green else YongColors.Sub,
                )
                HorizontalDivider(color = Color(0xFFF0F0F0))
                SettingRow("昵称", CoreViewModel.NICKNAME)
                HorizontalDivider(color = Color(0xFFF0F0F0))
                SettingRow("会话数", "${state.sessions.size}")
                HorizontalDivider(color = Color(0xFFF0F0F0))
                SettingRow("协议", "proto=96 · ECDH+Rijndael-256")
            }
        }

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(
                onClick = onStart,
                enabled = !state.coreRunning,
                colors = ButtonDefaults.buttonColors(containerColor = YongColors.Green),
                modifier = Modifier.weight(1f),
            ) { Text(if (state.coreRunning) "启动核心…" else "启动核心") }
            OutlinedButton(
                onClick = onStop,
                enabled = state.coreRunning,
                modifier = Modifier.weight(1f),
            ) { Text("停止核心", color = YongColors.Red) }
        }

        Text("核心事件", fontSize = 16.sp, fontWeight = FontWeight.SemiBold, color = YongColors.Ink)
        Surface(color = Color.White, shape = RoundedCornerShape(10.dp), tonalElevation = 1.dp) {
            if (state.events.isEmpty()) {
                Text(
                    "（尚无事件：点击「启动核心」后\nSettings load → Core::start → 事件回传）",
                    fontSize = 13.sp, color = YongColors.Sub, lineHeight = 20.sp,
                    modifier = Modifier.fillMaxWidth().padding(16.dp),
                )
            } else {
                Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    state.events.take(12).forEach { line ->
                        Text(
                            "· $line",
                            fontSize = 12.sp,
                            lineHeight = 17.sp,
                            color = when {
                                line.startsWith("错误") -> YongColors.Red
                                line.startsWith("用户上线") || line.startsWith("核心已启动") -> YongColors.Green
                                else -> YongColors.Ink
                            },
                        )
                    }
                }
            }
        }

        Text(
            "YongBEEP for Android 0.1.0 · A2 聊天 UI",
            fontSize = 11.sp, color = YongColors.Sub,
            modifier = Modifier.align(Alignment.CenterHorizontally),
        )
    }
}

@Composable
private fun SettingRow(label: String, value: String, mono: Boolean = false, valueColor: Color = YongColors.Ink) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, fontSize = 14.sp, color = YongColors.Sub)
        Text(
            value, fontSize = 14.sp, color = valueColor, fontWeight = FontWeight.Medium,
            fontFamily = if (mono) FontFamily.Monospace else FontFamily.Default,
        )
    }
}
