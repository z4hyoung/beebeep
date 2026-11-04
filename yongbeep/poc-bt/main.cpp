/*
 * YongBEEP M1 PoC — 经典蓝牙 RFCOMM 吞吐/连通性验证（Mac ↔ Windows）
 *
 * 验证矩阵（对应测试计划 docs/02-技术设计.md §M1）：
 *   T1 已配对：Server(Mac 监听) ← Client(Win 主动连) 文本互通
 *   T2 已配对：1MB 吞吐 ≥ 100 KB/s（写入侧与端到端两个数字）
 *   T3 未配对：Client 能否发现/连接（Win10 1607+ 理论支持，实测记录）
 *   T4 稳定性：连续 10 次 1MB 传输无卡死；传输中断开连接的恢复
 *
 * 用法：两端都启动本程序；
 *   Server 端：点"开始监听"（注册 SDP 服务，UUID 固定）
 *   Client 端：点"扫描"→ 列表选设备 → "连接" → "发文本"/"跑吞吐"
 */
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QElapsedTimer>
#include <QTimer>
#include <QDateTime>

#include <QBluetoothAddress>
#include <QBluetoothServer>
#include <QBluetoothServiceInfo>
#include <QBluetoothSocket>
#include <QBluetoothUuid>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothLocalDevice>

static const QString kServiceUuid = "7c9e2f0a-52d4-4b8e-9a3f-6d1c0f8b2e45";
static const QString kServiceName = "YongBEEP PoC";
static const qint64 kThroughputBytes = 1024 * 1024; // 1MB

class PoC : public QWidget {
  Q_OBJECT
public:
  PoC() : m_server(nullptr), m_socket(nullptr), m_agent(nullptr),
          m_roleLabel(new QLabel("本机: " + QBluetoothLocalDevice().name() + "  |  无连接")) {
    auto* root = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    m_listenBtn = new QPushButton("开始监听(Server)");
    m_scanBtn   = new QPushButton("扫描设备(Client)");
    m_connectBtn= new QPushButton("连接选中设备");
    m_textBtn   = new QPushButton("发文本");
    m_tpBtn     = new QPushButton("跑 1MB 吞吐");
    for (auto* b : {m_listenBtn, m_scanBtn, m_connectBtn, m_textBtn, m_tpBtn}) top->addWidget(b);
    root->addWidget(m_roleLabel);
    root->addLayout(top);
    m_devices = new QListWidget; root->addWidget(m_devices);
    auto* say = new QHBoxLayout; m_input = new QLineEdit("hello from poc"); say->addWidget(m_input);
    root->addLayout(say);
    m_log = new QTextEdit; m_log->setReadOnly(true); root->addWidget(m_log);

    connect(m_listenBtn, &QPushButton::clicked, this, &PoC::startServer);
    connect(m_scanBtn,   &QPushButton::clicked, this, &PoC::startDiscovery);
    connect(m_connectBtn,&QPushButton::clicked, this, &PoC::connectSelected);
    connect(m_textBtn,   &QPushButton::clicked, this, [this]{ if(m_socket && m_socket->state()==QBluetoothSocket::SocketState::ConnectedState) m_socket->write(("TXT:"+m_input->text()+"\n").toUtf8()); else log("未连接"); });
    connect(m_tpBtn,     &QPushButton::clicked, this, &PoC::runThroughput);

    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, [this]{
      if (m_tpActive && m_socket) {
        qint64 ms = m_tpTimer.elapsed();
        if (ms > 0) log(QString("进度 %1/%2 KB  写入吞吐 %3 KB/s").arg(m_bytesWritten/1024).arg(kThroughputBytes/1024).arg(m_bytesWritten*1.0/1024/(ms/1000.0)));
      }
    });
  }

private slots:
  void log(const QString& s) { m_log->append(QTime::currentTime().toString("hh:mm:ss ") + s); }

  void startServer() {
    delete m_server; m_server = nullptr;
    m_server = new QBluetoothServer(QBluetoothServiceInfo::RfcommProtocol, this);
    connect(m_server, &QBluetoothServer::newConnection, this, [this]{
      auto* s = m_server->nextPendingConnection();
      if (!s) return;
      attachSocket(s, "Server(被连)");
    });
    const QBluetoothServiceInfo si = m_server->listen(QBluetoothUuid(kServiceUuid), kServiceName);
    if (!si.isValid()) {
      log("!! listen 失败, QBluetoothServer::error=" + QString::number(int(m_server->error()))); return;
    }
    log("Server 监听中，SDP UUID=" + kServiceUuid);
    m_roleLabel->setText(m_roleLabel->text() + "  |  监听中");
  }

  void startDiscovery() {
    delete m_agent; m_agent = new QBluetoothDeviceDiscoveryAgent(this);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, [this](const QBluetoothDeviceInfo& d){
      m_devices->addItem(d.name().isEmpty() ? d.address().toString() : d.name() + " [" + d.address().toString() + "]");
      m_found.insert(m_devices->count()-1, d);
    });
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, [this]{ log("扫描完成，共 " + QString::number(m_found.size()) + " 台（未配对设备能 否出现，记录进 T3）"); });
    connect(m_agent, QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(&QBluetoothDeviceDiscoveryAgent::errorOccurred), this,
            [this](QBluetoothDeviceDiscoveryAgent::Error e){ log("!! 扫描错误 " + QString::number(e) + ": " + m_agent->errorString()); });
    m_devices->clear(); m_found.clear(); m_agent->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
    log("开始经典蓝牙设备扫描…");
  }

  void connectSelected() {
    auto row = m_devices->currentRow(); if (row < 0 || !m_found.contains(row)) { log("先选一台设备"); return; }
    const QBluetoothDeviceInfo d = m_found.value(row);
    auto* s = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    log("连接 " + d.name() + " …（未配对则观察是否触发系统配对=T3）");
    connect(s, &QBluetoothSocket::connected, this, [this]{ log("connected()"); });
    attachSocket(s, "Client(主动连)");
    s->connectToService(d.address(), QBluetoothUuid(kServiceUuid), QIODevice::ReadWrite);
  }

  void runThroughput() {
    if (!m_socket || m_socket->state() != QBluetoothSocket::SocketState::ConnectedState) { log("未连接"); return; }
    m_tpActive = true; m_bytesWritten = 0;
    m_buffer = QByteArray(kThroughputBytes, Qt::Uninitialized);
    m_tpTimer.start(); m_statsTimer->start(1000);
    log("开始 1MB 吞吐测试…");
    pumpWrite();
  }

private:
  void attachSocket(QBluetoothSocket* s, const QString& role) {
    if (m_socket) { m_socket->disconnectFromService(); m_socket->deleteLater(); }
    m_socket = s;
    connect(s, QOverload<QBluetoothSocket::SocketError>::of(&QBluetoothSocket::errorOccurred), this,
            [this](QBluetoothSocket::SocketError){ log("!! socket 错误: " + m_socket->errorString()); });
    connect(s, &QBluetoothSocket::disconnected, this, [this]{ log(" disconnected"); m_tpActive=false; m_statsTimer->stop(); });
    connect(s, &QBluetoothSocket::readyRead, this, [this]{
      m_rx.append(m_socket->readAll());
      int nl; while ((nl = m_rx.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_rx.left(nl)); m_rx.remove(0, nl+1);
        if (line.startsWith("TXT:")) log("对方: " + line.mid(4));
        else if (line.startsWith("TP:")) { // TP:<totalBytes>
          m_tpExpected = line.mid(3).toLongLong(); m_rx.clear(); m_tpE2E.start(); log("对方开始吞吐测试 " + QString::number(m_tpExpected) + " 字节"); }
        else if (line.startsWith("E2E:")) { // 对方回执：E2E:<bytes> <ms>
          log("★ 端到端结果: " + line.mid(4)); }
        else if (m_tpExpected > 0) { /* 纯数据负载 */ }
      }
      if (m_tpExpected > 0) {
        if (m_rx.size() >= m_tpExpected) {
          const qint64 ms = m_tpE2E.elapsed();
          log(QString("接收完成 %1 字节，服务端视角耗时 %2 ms (%3 KB/s)").arg(m_tpExpected).arg(ms).arg(m_tpExpected*1.0/1024/(qMax<qint64>(ms,1)/1000.0)));
          m_socket->write(QString("E2E:%1 %2\n").arg(m_tpExpected).arg(ms).toUtf8());
          m_tpExpected = 0; m_rx.clear();
        }
      }
    });
    connect(s, &QBluetoothSocket::bytesWritten, this, [this](qint64 n){
      if (!m_tpActive) return;
      m_bytesWritten += n;
      if (m_buffer.isEmpty() && m_bytesWritten >= kThroughputBytes) {
        m_tpActive = false; m_statsTimer->stop();
        const qint64 ms = m_tpTimer.elapsed();
        log(QString("★ 写入侧吞吐: %1 KB/s (%2 ms / 1MB)").arg(kThroughputBytes*1.0/1024/(qMax<qint64>(ms,1)/1000.0)).arg(ms));
      } else pumpWrite();
    });
    log("socket 已挂接，角色=" + role);
    m_roleLabel->setText("本机: " + QBluetoothLocalDevice().name() + "  |  " + role);
  }
  void pumpWrite() {
    if (!m_tpActive || !m_socket) return;
    while (m_socket->bytesToWrite() < 64*1024 && !m_buffer.isEmpty()) {
      const qint64 chunk = qMin<qint64>(m_buffer.size(), 32*1024);
      const qint64 w = m_socket->write(m_buffer.constData(), chunk);
      if (w <= 0) break;
      m_buffer.remove(0, w);
    }
  }

  QBluetoothServer* m_server = nullptr;
  QBluetoothSocket* m_socket = nullptr;
  QBluetoothDeviceDiscoveryAgent* m_agent = nullptr;
  QMap<int, QBluetoothDeviceInfo> m_found;
  QPushButton *m_listenBtn, *m_scanBtn, *m_connectBtn, *m_textBtn, *m_tpBtn;
  QListWidget* m_devices; QLineEdit* m_input; QTextEdit* m_log; QLabel* m_roleLabel;
  QTimer* m_statsTimer;
  QByteArray m_buffer, m_rx;
  bool m_tpActive = false; qint64 m_bytesWritten = 0;
  qint64 m_tpExpected = 0; QElapsedTimer m_tpTimer, m_tpE2E;
};

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  PoC w; w.resize(720, 640); w.show();
  return app.exec();
}
#include "main.moc"
