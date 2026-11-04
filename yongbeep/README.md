# YongBEEP 工作区总览

> 基于 BeeBEEP 5.9.2-dev（官方 SVN r1609，GPLv3）改造的**极简蓝牙 IM**：
> Mac ↔ Windows 邻近直连（各自不同网络），微信式界面，只保留**文本聊天 + ≤5MB 文件传输**。

## 目录结构

```
YongBEEP/
├── source/                  # 主代码库（git）
│   ├── main                 # 官方基线原样（tag: baseline-svn-r1609），永不修改
│   ├── dev                  # 集成分支（所有已验收 feature 的合并点）
│   └── feature/*            # 功能分支（bt-transport / bt-inline-file / ui-wechat / ui-polish）
├── build/
│   ├── m0-qt6/              # 主工程影子构建目录（qmake+make）
│   └── poc-bt/              # 蓝牙 PoC 构建
├── poc-bt/                  # M1 蓝牙 RFCOMM 吞吐测试器（独立小工程）
├── docs/
│   ├── 00-方案建议书.md      # G0 已批
│   ├── 01-产品设计.md        # PRD（MoSCoW/原型/验收标准）
│   ├── 02-技术设计.md        # TDD（架构/ADR/改动清单）
│   ├── 03-测试方案.md        # S4 测试计划与报告
│   ├── guide-windows-setup.md # Windows 机环境搭建+M1 实测指引
│   ├── analysis/            # 三份源码深度分析（core/gui/build，带 file:line）
│   └── ui-screenshots/      # UI 截图验收产物
└── RESEARCH-REPORT.md       # 最初的技术调研（选型依据）
```

## 常用命令

```bash
# 构建（在 build/m0-qt6）
cd build/m0-qt6
/opt/homebrew/opt/qt/bin/qmake ~/Workspace/YongBEEP/source/beebeep-desktop.pro
make -j8
# 产物: source/test/yongbeep.app

# 冒烟 + 截图（不依赖录屏权限）
HOME=/tmp/yb-smoke source/test/yongbeep.app/Contents/MacOS/yongbeep --demo-chat \
  --screenshot docs/ui-screenshots/xxx.png

# 蓝牙 PoC
cd build/poc-bt && make
```

## 研发流程（阶段门）

G0 方案 ✅ → G1 产品 ✅ → G2 技术 ✅ → G3 代码走查（每 feature）✅进行中 → G4 双机验收 ⛔待 Windows 机
用户已授权"连续执行至 block 点"；当前 block：M1 双机蓝牙实测需 Windows 配合（见 guide-windows-setup.md）。

## 许可证合规

GPLv3：自用零义务；如向他人分发编译版，本仓库保持公开即满足源码提供义务。
