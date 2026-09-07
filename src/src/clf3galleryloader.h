#pragma once

#include "clf3enginemanager.h"
#include <QJsonDocument>

// Gallery commands need the managed engine even before an installation exists.
class Clf3GalleryLoader : public QObject
{
  Q_OBJECT
public:
  explicit Clf3GalleryLoader(QObject* parent = nullptr,
                             QNetworkAccessManager* network = nullptr,
                             const QString& cacheRoot = {});
  ~Clf3GalleryLoader() override;
  void load(bool refresh = false);
  void cancel();
  bool isBusy() const { return m_busy; }

signals:
  void statusChanged(QString message);
  void loaded(QJsonDocument document);
  void failed(QString message);

private:
  Clf3EngineManager m_engine;
  QProcess m_process;
  QTimer m_timeout;
  QByteArray m_output;
  bool m_busy{false};
  bool m_refresh{false};
  void start(const QString& path);
  void fail(const QString& message);
};
