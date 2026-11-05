package com.yongbeep.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.yongbeep.app.core.CoreViewModel

/**
 * A1.1 壳工程：验证 Gradle/AGP/Compose 工具链 + 模拟器点亮。
 * A1.3：设置页接入 JNI 桥（libybcore.so + Qt6 运行库）真实核心。
 */
class MainActivity : ComponentActivity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    // Qt 的安卓胶水层需要 class loader + activity context（QtBluetooth 的
    // adapter 查找、QStandardPaths 安卓路径都经 QtNative.getContext()）。
    // 核心库在自有线程上无头运行，不走 QtActivity，必须在此手动注入。
    org.qtproject.qt.android.YbQtGlue.setup(classLoader, this)
    setContent { YongBEEPApp() }
  }
}

// 微信色板（与桌面端 YongTheme 一致）
private val YbGreen = Color(0xFF07C160)
private val YbBubble = Color(0xFF95EC69)
private val YbRed = Color(0xFFFA5151)
private val YbInk = Color(0xFF191919)
private val YbSub = Color(0xFFB2B2B2)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun YongBEEPApp() {
  var tab by remember { mutableIntStateOf(0) }
  val titles = listOf("消息", "设备", "设置")
  Scaffold(
    topBar = {
      CenterAlignedTopAppBar(
        title = { Text("YongBEEP", fontWeight = FontWeight.SemiBold, fontSize = 17.sp) },
        colors = TopAppBarDefaults.centerAlignedTopAppBarColors(containerColor = Color.White)
      )
    },
    bottomBar = {
      NavigationBar(containerColor = Color.White) {
        titles.forEachIndexed { i, t ->
          NavigationBarItem(
            selected = tab == i,
            onClick = { tab = i },
            icon = {
              Text(
                if (i == 0) "💬" else if (i == 1) "🔵" else "⚙️",
                fontSize = 20.sp
              )
            },
            label = { Text(t, fontSize = 11.sp) },
            colors = NavigationBarItemDefaults.colors(
              selectedTextColor = YbGreen, indicatorColor = Color(0xFFE8F8EF)
            )
          )
        }
      }
    },
    containerColor = Color(0xFFF7F7F7)
  ) { pad ->
    when (tab) {
      0 -> A1ChatPage(pad)
      1 -> PlaceholderPage(pad, "设备", "A3：扫描附近 YongBEEP 设备\n（蓝牙栈已在核心库就绪）")
      else -> SettingsPage(pad)
    }
  }
}

/** A1.4 最小验收页：连 IP:端口 + 发消息 + 事件回显（A2 换成正式聊天 UI）。 */
@Composable
fun A1ChatPage(pad: PaddingValues) {
  val viewModel: CoreViewModel = viewModel()
  val state by viewModel.uiState.collectAsState()
  var host by remember { mutableStateOf("10.0.2.2") }
  var port by remember { mutableStateOf("6475") }
  var text by remember { mutableStateOf("") }
  val listState = rememberScrollState()

  Column(Modifier.fillMaxSize().padding(pad).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
    Text("A1.4 端到端验收", fontSize = 16.sp, fontWeight = FontWeight.SemiBold, color = YbInk)
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
      OutlinedTextField(host, { host = it }, label = { Text("对端 IP") }, modifier = Modifier.weight(2f), singleLine = true)
      OutlinedTextField(port, { port = it }, label = { Text("端口") }, modifier = Modifier.weight(1f), singleLine = true)
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
      Button(
        onClick = { viewModel.connectPeer(host, port.toIntOrNull() ?: 6475) },
        colors = ButtonDefaults.buttonColors(containerColor = YbGreen),
        enabled = state.coreRunning
      ) { Text("连接") }
      Text("核心: ${if (state.coreRunning) "运行中 ✓" else "未启动"}", fontSize = 12.sp, color = YbSub, modifier = Modifier.align(Alignment.CenterVertically))
    }
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
      OutlinedTextField(text, { text = it }, label = { Text("消息") }, modifier = Modifier.weight(1f), singleLine = true)
      Button(
        onClick = { if (text.isNotBlank()) { viewModel.sendChat(text); text = "" } },
        colors = ButtonDefaults.buttonColors(containerColor = YbGreen),
        enabled = state.coreRunning && text.isNotBlank()
      ) { Text("发送") }
    }
    Text("事件流", fontSize = 12.sp, color = YbSub)
    Column(Modifier.weight(1f).verticalScroll(listState).fillMaxWidth()) {
      state.events.takeLast(200).reversed().forEach { ev ->
        Text(ev, fontSize = 12.sp, color = when {
          ev.contains("message_received") -> YbInk
          ev.contains("error") -> YbRed
          else -> YbSub
        }, modifier = Modifier.padding(vertical = 2.dp))
      }
    }
    LaunchedEffect(state.events.size) { listState.scrollTo(0) }
  }
}

/** A1.3 设置页：nativeVersion 真实返回值 + 核心状态 + 启停按钮 + 事件证据。 */
@Composable
fun SettingsPage(pad: PaddingValues) {
  val viewModel: CoreViewModel = viewModel()
  val state by viewModel.uiState.collectAsState()

  Column(
    Modifier.fillMaxSize().padding(pad).padding(20.dp).verticalScroll(rememberScrollState()),
    verticalArrangement = Arrangement.spacedBy(14.dp)
  ) {
    Text("核心引擎", fontSize = 20.sp, fontWeight = FontWeight.SemiBold, color = YbInk)

    Surface(color = Color.White, shape = MaterialTheme.shapes.medium, tonalElevation = 1.dp) {
      Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        SettingRow("核心版本", state.coreVersion, mono = true)
        HorizontalDivider(color = Color(0xFFF0F0F0))
        SettingRow("核心状态", if (state.coreRunning) "运行中" else "已停止",
          valueColor = if (state.coreRunning) YbGreen else YbSub)
        HorizontalDivider(color = Color(0xFFF0F0F0))
        SettingRow("昵称", CoreViewModel.NICKNAME)
        HorizontalDivider(color = Color(0xFFF0F0F0))
        SettingRow("协议", "proto=96 · ECDH+Rijndael-256")
      }
    }

    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
      Button(
        onClick = viewModel::startCore,
        enabled = !state.coreRunning,
        colors = ButtonDefaults.buttonColors(containerColor = YbGreen),
        modifier = Modifier.weight(1f),
      ) { Text(if (state.coreRunning) "启动核心…" else "启动核心") }
      OutlinedButton(
        onClick = viewModel::stopCore,
        enabled = state.coreRunning,
        modifier = Modifier.weight(1f),
      ) { Text("停止核心", color = YbRed) }
    }

    Text("核心事件", fontSize = 16.sp, fontWeight = FontWeight.SemiBold, color = YbInk)
    Surface(color = Color.White, shape = MaterialTheme.shapes.medium, tonalElevation = 1.dp) {
      if (state.events.isEmpty()) {
        Text(
          "（尚无事件：点击「启动核心」后\nSettings load → Core::start → 事件回传）",
          fontSize = 13.sp, color = YbSub, lineHeight = 20.sp,
          modifier = Modifier.fillMaxWidth().padding(16.dp),
        )
      } else {
        Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
          state.events.take(12).forEach { line ->
            Text("· $line", fontSize = 12.sp, color = YbInk, lineHeight = 17.sp)
          }
        }
      }
    }

    Text(
      "YongBEEP for Android 0.1.0 · A1.3 JNI 冒烟",
      fontSize = 11.sp, color = YbSub,
      modifier = Modifier.align(Alignment.CenterHorizontally),
    )
  }
}

@Composable
private fun SettingRow(label: String, value: String, mono: Boolean = false, valueColor: Color = YbInk) {
  Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
    Text(label, fontSize = 14.sp, color = YbSub)
    Text(
      value, fontSize = 14.sp, color = valueColor, fontWeight = FontWeight.Medium,
      fontFamily = if (mono) FontFamily.Monospace else FontFamily.Default,
    )
  }
}

@Composable
fun PlaceholderPage(pad: PaddingValues, title: String, body: String) {
  Column(
    Modifier.fillMaxSize().padding(pad).padding(24.dp),
    horizontalAlignment = Alignment.CenterHorizontally,
    verticalArrangement = Arrangement.Center
  ) {
    Text(title, fontSize = 22.sp, fontWeight = FontWeight.SemiBold, color = YbInk)
    Spacer(Modifier.height(10.dp))
    Text(body, fontSize = 14.sp, color = YbSub, lineHeight = 22.sp)
    Spacer(Modifier.height(18.dp))
    Surface(color = YbBubble, shape = MaterialTheme.shapes.small) {
      Text("  A1 · 工具链已点亮  ", fontSize = 13.sp, color = YbInk, modifier = Modifier.padding(8.dp))
    }
  }
}
