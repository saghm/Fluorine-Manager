#include "envmetrics.h"

#include <QGuiApplication>
#include <QRect>
#include <QScreen>

namespace env
{

Display::Display(QString adapter, QString monitorDevice, bool primary)
    : m_adapter(std::move(adapter)), m_monitorDevice(std::move(monitorDevice)),
      m_primary(primary), m_resX(0), m_resY(0), m_dpi(0), m_refreshRate(0)
{
  getSettings();
}

const QString& Display::adapter() const
{
  return m_adapter;
}

const QString& Display::monitorDevice() const
{
  return m_monitorDevice;
}

bool Display::primary()
{
  return m_primary;
}

int Display::resX() const
{
  return m_resX;
}

int Display::resY() const
{
  return m_resY;
}

int Display::dpi()
{
  return m_dpi;
}

int Display::refreshRate() const
{
  return m_refreshRate;
}

QString Display::toString() const
{
  return QString("%1*%2 %3hz dpi=%4 on %5%6")
      .arg(m_resX)
      .arg(m_resY)
      .arg(m_refreshRate)
      .arg(m_dpi)
      .arg(m_adapter)
      .arg(m_primary ? " (primary)" : "");
}

void Display::getSettings()
{
  const auto screens = QGuiApplication::screens();
  for (auto* screen : screens) {
    if (screen->name() == m_monitorDevice) {
      const auto geo = screen->geometry();
      m_resX         = geo.width();
      m_resY         = geo.height();
      m_refreshRate  = qRound(screen->refreshRate());
      m_dpi          = qRound(screen->logicalDotsPerInch());
      return;
    }
  }

  if (auto* primary = QGuiApplication::primaryScreen()) {
    const auto geo = primary->geometry();
    m_resX         = geo.width();
    m_resY         = geo.height();
    m_refreshRate  = qRound(primary->refreshRate());
    m_dpi          = qRound(primary->logicalDotsPerInch());
  }
}

Metrics::Metrics()
{
  getDisplays();
}

const std::vector<Display>& Metrics::displays() const
{
  return m_displays;
}

QRect Metrics::desktopGeometry() const
{
  if (auto* primary = QGuiApplication::primaryScreen()) {
    return primary->virtualGeometry();
  }
  return QRect();
}

void Metrics::getDisplays()
{
  const auto screens = QGuiApplication::screens();
  for (auto* screen : screens) {
    const bool isPrimary = (screen == QGuiApplication::primaryScreen());
    m_displays.emplace_back(screen->manufacturer() + " " + screen->model(),
                            screen->name(), isPrimary);
  }
}

}  // namespace env
