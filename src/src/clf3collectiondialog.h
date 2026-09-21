#pragma once

#include "clf3collectioncompat.h"
#include "clf3collectionhost.h"
#include "clf3processcontroller.h"
#include <QCache>
#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QQueue>

class QComboBox;
class QLineEdit;
class QListWidget;
class QLabel;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QPlainTextEdit;
class QProgressBar;
class QStackedWidget;

class Clf3CollectionDialog : public QDialog
{
  Q_OBJECT
public:
  // Temporary product gate: browsing remains available while installation is WIP.
  static constexpr bool InstallationEnabled = false;
  explicit Clf3CollectionDialog(Clf3CollectionHost::AuthRequest auth,
                                QWidget* parent = nullptr,
                                bool browseOnOpen = true,
                                QNetworkAccessManager* publicNetwork = nullptr);
  ~Clf3CollectionDialog() override;
  QString createdInstanceDir() const;
  bool shouldOpen() const { return m_open; }
  bool isBusy() const { return m_busy || m_controller.isRunning(); }
  void done(int result) override;
  void setDetectedGames(const QHash<QString, QString>& paths) { m_detectedGames = paths; }

signals:
  void busyChanged(bool busy);
  void nexusConnectionRequested();

private:
  Clf3ProcessController m_controller;
  Clf3CollectionHost m_host;
  Clf3CollectionHost m_images;
  Clf3CollectionCompat m_compat;
  QStackedWidget* m_pages{};
  QWidget* m_reviewPanel{};
  QPushButton *m_back{}, *m_configure{};
  QLabel *m_resultCount{}, *m_selection{};
  QCache<QString, QIcon> m_thumbnails{ 128 };
  QQueue<QString> m_thumbnailQueue;
  int m_activeThumbnails{ 0 };
  quint64 m_thumbnailGeneration{ 0 };
  QComboBox *m_games{}, *m_sort{};
  QLineEdit *m_search{}, *m_source{}, *m_game{}, *m_cache{}, *m_output{}, *m_package{}, *m_ini{},
    *m_masterlist{};
  QListWidget *m_catalog{}, *m_members{};
  QLabel *m_status{}, *m_details{}, *m_engine{};
  QSpinBox* m_revision{};
  QCheckBox* m_adult{};
  QPlainTextEdit* m_review{};
  QProgressBar* m_progress{};
  QPushButton *m_planButton{}, *m_install{}, *m_resume{}, *m_cancel{}, *m_add{}, *m_openButton{},
    *m_manual{}, *m_browser{}, *m_previous{}, *m_next{};
  QJsonObject m_capabilities, m_plan, m_locator, m_sources, m_artifacts, m_currentArtifact,
    m_resumeExpected;
  QJsonObject m_localSources, m_workerRequest;
  QHash<QString, QJsonObject> m_support;
  QHash<QString, QString> m_detectedGames;
  QList<QPushButton*> m_pathButtons;
  QString m_job, m_identity, m_packagePath, m_plannedSource, m_brokerConsumer, m_pendingError;
  int m_page{ 0 }, m_artifactIndex{ 0 };
  bool m_busy{ false }, m_dirty{ true }, m_published{ false }, m_open{ false },
    m_rendering{ false }, m_installStarted{ false };
  int m_deferredClose{ -1 };
  quint64 m_catalogGeneration{ 0 };
  QStringList selectedOptional() const;
  QString pendingPath() const;
  void browse();
  void pumpThumbnails();
  QString gameLabel(const QString& domain) const;
  void updateSelection();
  void reviewPlan();
  bool usesLocalWorker() const;
  bool installationAvailable() const;
  void reviewLocalPackage();
  void showPlan(const QJsonObject& plan);
  void markDirty();
  void setBusy(bool busy);
  void fail(const QString& message);
  bool save();
  void resume();
  void install();
  void acquireNext();
  void acquireCurrent(const QString& nxm = {});
  void runWorker();
  void published(const QString& reportPath);
  void cancel();
};
