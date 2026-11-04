# M1 蓝牙 PoC 测试计划（BtPoc）

目的：在写正式代码前，验证 Qt Bluetooth 经典 RFCOMM 在 **macOS 25.6 ↔ Windows** 上的可用性与吞吐，产出 go/no-go 结论。

## 构建

**macOS**（Qt6 via Homebrew）：
```bash
cd poc-bt
/opt/homebrew/opt/qt/bin/qmake BtPoc.pro && make -j8
./BtPoc.app/Contents/MacOS/BtPoc   # 或 debug 目录下可执行文件
```

**Windows**（Qt6 MSVC + Qt Creator）：打开 `BtPoc.pro` → Release 构建。
> 注意首次运行 Windows 可能提示"允许蓝牙"，选允许；macOS 若弹出蓝牙权限请求同意即可。

## 测试矩阵

| # | 场景 | 操作 | 记录 | 通过标准 |
|---|---|---|---|---|
| T1 | 已配对·文本 | 两机先系统配对 → Mac 开"监听"，Win 扫描→连接→发文本 | 是否触发系统配对弹窗；文本是否互通 | 双向文本 OK |
| T2 | 已配对·吞吐 | 连接状态下点"跑 1MB 吞吐"（双向各跑 3 次取均值） | 写入侧 KB/s 与端到端 KB/s（日志 ★ 行） | **≥100 KB/s**（go/no-go 线） |
| T3 | 未配对 | Win 侧不配对直接扫描→连接 | 未配对设备是否出现在列表；连接是否成功或被拒 | 仅记录（不阻塞 go） |
| T4 | 稳定性 | 连续 10 次 1MB；传输中关闭蓝牙再恢复 | 是否卡死；错误提示；能否重连成功 | 无卡死，可恢复 |
| T5 | 角色反转 | Win 开监听，Mac 做客户端 | 是否可用（WinRT RFCOMM server 能力） | 仅记录 |

## 结论记录（测试后填写）

| 项 | 结果 |
|---|---|
| T1 文本互通 | |
| T2 吞吐（均值，写入/端到端） | |
| T3 未配对路径 | |
| T4 稳定性 | |
| T5 角色反转 | |
| **Go / No-Go** | |

- **Go**：进入 M2（传输层集成），按方案 A 实现
- **No-Go（macOS 侧失败）**：转方案 B（BLE）重新 PoC
- **No-Go（Windows 侧失败）**：评估方案 C（Windows 原生 WinRT + Mac Qt 混合）
