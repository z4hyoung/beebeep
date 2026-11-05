package com.yongbeep.app

import android.content.Context
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.yongbeep.app.core.CoreViewModel
import com.yongbeep.app.ui.ChatPage
import com.yongbeep.app.ui.SettingsPage
import com.yongbeep.app.ui.SessionsPage
import com.yongbeep.app.ui.YongColors

/**
 * A1.1 壳工程 → A2 聊天 UI：
 * 消息（会话列表）/设备（A3 占位）/设置 三页 + 会话内聊天页（返回键回）。
 * 隐藏开发者入口：长按顶部标题弹 IP:Port 直连对话框（A1.4 验收路径）。
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

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun YongBEEPApp() {
  val viewModel: CoreViewModel = viewModel()
  val state by viewModel.uiState.collectAsState()
  var tab by remember { mutableIntStateOf(0) }
  var openSessionId by remember { mutableStateOf<Long?>(null) }
  var showConnectDialog by remember { mutableStateOf(false) }
  val titles = listOf("消息", "设备", "设置")

  val openSession = openSessionId?.let { id -> state.sessions.firstOrNull { it.userId == id } }
  if (openSessionId != null && openSession == null) {
    // 会话被核心清掉（对端消失且无消息）：退回列表。
    LaunchedEffect(openSessionId) { openSessionId = null }
  }

  if (openSession != null) {
    ChatPage(
      session = openSession,
      coreRunning = state.coreRunning,
      onBack = { openSessionId = null; viewModel.closeChat() },
      onSend = { text, markdown -> viewModel.sendChat(openSession.userId, text, markdown) },
    )
  } else {
    Scaffold(
      topBar = {
        CenterAlignedTopAppBar(
          title = {
            TitleWithLongPress(
              text = if (tab == 0) "YongBEEP" else titles[tab],
              onLongPress = { showConnectDialog = true },
            )
          },
          actions = {
            // 右上角核心状态小圆点
            Box(
              Modifier
                .padding(end = 16.dp)
                .size(8.dp)
                .background(
                  if (state.coreRunning) YongColors.Green else Color(0xFFC9C9C9),
                  CircleShape,
                )
            )
          },
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
                selectedTextColor = YongColors.Green, indicatorColor = Color(0xFFE8F8EF)
              )
            )
          }
        }
      },
      containerColor = YongColors.Bg
    ) { pad ->
      when (tab) {
        0 -> Box(Modifier.padding(pad)) {
          SessionsPage(
            sessions = state.sessions,
            onOpenSession = { id ->
              openSessionId = id
              viewModel.openChat(id)
            },
          )
        }
        1 -> PlaceholderPage(pad, "设备", "A3：扫描附近 YongBEEP 设备\n（蓝牙栈已在核心库就绪）")
        else -> Box(Modifier.padding(pad)) {
          SettingsPage(
            state = state,
            onStart = viewModel::startCore,
            onStop = viewModel::stopCore,
          )
        }
      }
    }
  }

  if (showConnectDialog) {
    ConnectPeerDialog(
      coreRunning = state.coreRunning,
      onDismiss = { showConnectDialog = false },
      onConnect = { ip, port ->
        showConnectDialog = false
        viewModel.connectPeer(ip, port)
      },
    )
  }
}

/** 标题（长按 = 隐藏开发者直连入口，A1.4 验收路径）。 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun TitleWithLongPress(text: String, onLongPress: () -> Unit) {
  Text(
    text,
    fontWeight = FontWeight.SemiBold,
    fontSize = 17.sp,
    modifier = Modifier.combinedClickable(onClick = {}, onLongClick = onLongPress),
  )
}

/** 手动直连对话框：IP:Port（记忆上次输入；核心未运行时提示先启动）。 */
@Composable
private fun ConnectPeerDialog(
  coreRunning: Boolean,
  onDismiss: () -> Unit,
  onConnect: (ip: String, port: Int) -> Unit,
) {
  val context = LocalContext.current
  val prefs = remember { context.getSharedPreferences("yongbeep", Context.MODE_PRIVATE) }
  var ip by remember { mutableStateOf(prefs.getString("last_peer_ip", "") ?: "") }
  var port by remember { mutableStateOf(prefs.getString("last_peer_port", "") ?: "") }

  AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text("开发者直连", fontWeight = FontWeight.SemiBold, fontSize = 17.sp) },
    text = {
      Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        if (!coreRunning) {
          Text("核心未运行：请先到设置页启动核心", fontSize = 12.sp, color = YongColors.Red)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
          OutlinedTextField(
            ip, { ip = it },
            label = { Text("对端 IP") },
            placeholder = { Text("10.0.2.2") },
            singleLine = true,
            modifier = Modifier.weight(2f),
          )
          OutlinedTextField(
            port, { port = it },
            label = { Text("端口") },
            placeholder = { Text("6475") },
            singleLine = true,
            modifier = Modifier.weight(1f),
          )
        }
        Text("与自动发现同一条连接路径（Core::checkNetworkAddress）", fontSize = 11.sp, color = YongColors.Sub)
      }
    },
    confirmButton = {
      TextButton(
        onClick = {
          val p = port.toIntOrNull() ?: 6475
          prefs.edit()
            .putString("last_peer_ip", ip)
            .putString("last_peer_port", p.toString())
            .apply()
          onConnect(ip.ifBlank { "10.0.2.2" }, p)
        },
        enabled = coreRunning,
      ) { Text("连接", color = if (coreRunning) YongColors.Green else YongColors.Sub, fontWeight = FontWeight.SemiBold) }
    },
    dismissButton = {
      TextButton(onClick = onDismiss) { Text("取消", color = YongColors.Sub) }
    },
    containerColor = Color.White,
    shape = RoundedCornerShape(14.dp),
  )
}

@Composable
fun PlaceholderPage(pad: PaddingValues, title: String, body: String) {
  Column(
    Modifier.fillMaxSize().padding(pad).padding(24.dp),
    horizontalAlignment = Alignment.CenterHorizontally,
    verticalArrangement = Arrangement.Center
  ) {
    Text(title, fontSize = 22.sp, fontWeight = FontWeight.SemiBold, color = YongColors.Ink)
    Spacer(Modifier.height(10.dp))
    Text(body, fontSize = 14.sp, color = YongColors.Sub, lineHeight = 22.sp)
    Spacer(Modifier.height(18.dp))
    Surface(color = YongColors.Bubble, shape = MaterialTheme.shapes.small) {
      Text("  A3 · 敬请期待  ", fontSize = 13.sp, color = YongColors.Ink, modifier = Modifier.padding(8.dp))
    }
  }
}
