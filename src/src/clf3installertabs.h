#pragma once

#include <QTabWidget>
#include <functional>

class Clf3CollectionDialog;

// Both browsers share the installer window. Start Collections discovery when
// its tab is first opened, and retain both browsers when switching tabs.
class Clf3InstallerTabs : public QTabWidget
{
  Q_OBJECT
public:
  using CollectionFactory = std::function<Clf3CollectionDialog*(QWidget*)>;
  Clf3InstallerTabs(QWidget* wabbajack, CollectionFactory factory, QWidget* parent = nullptr);
  Clf3CollectionDialog* collections() const { return m_collections; }

signals:
  void collectionsFinished(int result);
  void nexusConnectionRequested();

private:
  Clf3CollectionDialog* m_collections{};
};
