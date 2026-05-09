#include "multiprocess.h"
#include "utility.h"

#include <QLocalSocket>
#include <QThread>
#include <log.h>
#include <report.h>

static const char s_Key[]  = "mo-43d1a3ad-eeb0-4818-97c9-eda5216c29b5";
static const int s_Timeout = 5000;

using MOBase::reportError;

// Liveness probe: a real primary owns a QLocalServer listening on s_Key.
// If we can connect, a primary exists. If not, the shm/socket is stale
// (prior crash left SysV segment + unix socket file behind on Linux).
static bool primaryAlive()
{
  QLocalSocket probe;
  probe.connectToServer(s_Key, QIODevice::WriteOnly);
  const bool alive = probe.waitForConnected(500);
  if (alive) {
    probe.disconnectFromServer();
  }
  return alive;
}

MOMultiProcess::MOMultiProcess(bool allowMultiple, QObject* parent)
    : QObject(parent)
{
  m_SharedMem.setKey(s_Key);

  if (m_SharedMem.create(1)) {
    m_OwnsSM = true;
  } else {
    auto error = m_SharedMem.error();

    if (error == QSharedMemory::AlreadyExists && !allowMultiple) {
      // Primary instance likely exists. Try to attach as ephemeral process.
      if (m_SharedMem.attach()) {
        // Verify a primary is actually listening. Stale SysV shm on Linux
        // survives crashes — attach succeeds against a dead segment.
        if (primaryAlive()) {
          m_Ephemeral = true;
        } else {
          MOBase::log::debug(
              "stale shared memory detected (no primary listener), "
              "claiming ownership");
          m_SharedMem.detach();
          if (m_SharedMem.create(1)) {
            m_OwnsSM = true;
            error    = QSharedMemory::NoError;
          } else {
            // Another attached client keeps refcount > 0; on Linux Qt only
            // frees the segment when the last attacher detaches. Re-attach
            // as a secondary so the constructor returns in a usable state
            // and sendMessage() can still try to deliver the request via
            // the unix socket — it would otherwise leave both ownership
            // flags clear and silently no-op every later call.
            const QString reclaimError = m_SharedMem.errorString();
            if (m_SharedMem.attach()) {
              MOBase::log::warn(
                  "could not reclaim stale shared memory ({}), "
                  "running as secondary",
                  reclaimError);
              m_Ephemeral = true;
              error       = QSharedMemory::NoError;
            } else {
              MOBase::log::warn(
                  "could not reclaim or re-attach stale shared memory "
                  "(reclaim: {}, reattach: {})",
                  reclaimError, m_SharedMem.errorString());
              error = m_SharedMem.error();
            }
          }
        }
      } else {
        // Handle races with stale shared memory state:
        // between create() and attach(), the owner may have disappeared.
        auto attachError = m_SharedMem.error();

        if (attachError == QSharedMemory::NotFound ||
            attachError == QSharedMemory::KeyError ||
            attachError == QSharedMemory::UnknownError) {
          MOBase::log::debug(
              "shared memory attach race: {} ({}), retrying as owner",
              m_SharedMem.errorString(), static_cast<int>(attachError));
          if (m_SharedMem.create(1)) {
            m_OwnsSM = true;
            error    = QSharedMemory::NoError;
          } else {
            error = m_SharedMem.error();
          }
        } else {
          MOBase::log::debug("shared memory attach failed: {} ({})",
                             m_SharedMem.errorString(),
                             static_cast<int>(attachError));
          error = attachError;
        }
      }
    }

    if (!m_OwnsSM && !m_Ephemeral && error != QSharedMemory::NoError &&
        error != QSharedMemory::AlreadyExists) {
      throw MOBase::MyException(tr("SHM error: %1").arg(m_SharedMem.errorString()));
    }
  }

  if (m_OwnsSM) {
    connect(&m_Server, SIGNAL(newConnection()), this, SLOT(receiveMessage()),
            Qt::QueuedConnection);
    // Clear any stale unix socket file from a prior crashed primary,
    // otherwise listen() fails with AddressInUseError on Linux.
    QLocalServer::removeServer(s_Key);
    // has to be called before listen
    m_Server.setSocketOptions(QLocalServer::WorldAccessOption);
    if (!m_Server.listen(s_Key)) {
      MOBase::log::warn("QLocalServer listen failed: {}",
                           m_Server.errorString().toStdString());
    }
  }
}

void MOMultiProcess::sendMessage(const QString& message)
{
  if (m_OwnsSM) {
    // nobody there to receive the message
    return;
  }
  QLocalSocket socket(this);

  bool connected = false;
  for (int i = 0; i < 2 && !connected; ++i) {
    if (i > 0) {
      QThread::msleep(250);
    }

    // other process may be just starting up
    socket.connectToServer(s_Key, QIODevice::WriteOnly);
    connected = socket.waitForConnected(s_Timeout);
  }

  if (!connected) {
    reportError(
        tr("failed to connect to running process: %1").arg(socket.errorString()));
    return;
  }

  socket.write(message.toUtf8());
  if (!socket.waitForBytesWritten(s_Timeout)) {
    if (socket.bytesToWrite()) {
      reportError(tr("failed to communicate with running process: %1")
                      .arg(socket.errorString()));
    }
  }

  socket.disconnectFromServer();
  socket.waitForDisconnected();
}

void MOMultiProcess::receiveMessage()
{
  QLocalSocket* socket = m_Server.nextPendingConnection();
  if (!socket) {
    return;
  }

  if (!socket->waitForReadyRead(s_Timeout)) {
    // check if there are bytes available; if so, it probably means the data was
    // already received by the time waitForReadyRead() was called and the
    // connection has been closed
    const auto av = socket->bytesAvailable();

    if (av <= 0) {
      // primaryAlive() liveness probes connect then disconnect without sending
      // any data — these are expected, not errors. A real secondary that
      // failed to deliver its payload will leave a non-PeerClosed error.
      const auto err = socket->error();
      const bool isProbe = err == QLocalSocket::PeerClosedError ||
                           err == QLocalSocket::UnknownSocketError;
      if (isProbe) {
        MOBase::log::debug(
            "secondary closed without sending data (liveness probe)");
      } else {
        MOBase::log::error("failed to receive data from secondary process: {}",
                           socket->errorString());

        reportError(tr("failed to receive data from secondary process: %1")
                        .arg(socket->errorString()));
      }
      return;
    }
  }

  QString const message = QString::fromUtf8(socket->readAll().constData());
  emit messageSent(message);
  socket->disconnectFromServer();
}
