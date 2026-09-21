#pragma once

#include <QJsonObject>
#include <QObject>
#include <atomic>
#include <functional>
#include <memory>

// Compatibility with the credential-free worker shipped in CLF3 0.2.6.
// Planning, staging, FOMOD replay, ordering and publication still run in CLF3.
class Clf3CollectionCompat : public QObject
{
  Q_OBJECT
public:
  explicit Clf3CollectionCompat(QObject* parent = nullptr);
  ~Clf3CollectionCompat() override;
  static bool supports(const QJsonObject& capabilities);
  static QJsonObject gameSupport(const QJsonObject& capabilities, const QJsonObject& game);
  void prepare(const QString& package,
               const QString& game,
               const QJsonObject& support,
               const QString& engine);
  void verifyPublication(const QJsonObject& request, const QJsonObject& support);
  void cancel();
signals:
  void prepared(QString runtime, QJsonObject sourcesByIndex);
  void verified(QString reportPath);
  void failed(QString message);

private:
  using Token = std::shared_ptr<std::atomic_bool>;
  Token m_cancelled;
  void run(std::function<QJsonObject(Token)> work, std::function<void(QJsonObject)> complete);
};
