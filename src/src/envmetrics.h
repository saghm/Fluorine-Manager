#ifndef ENV_METRICS_H
#define ENV_METRICS_H

#include <QString>
#include <vector>

namespace env
{

// information about a monitor
//
class Display
{
public:
  Display(QString adapter, QString monitorDevice, bool primary);

  // display name of the adapter running the monitor
  //
  const QString& adapter() const;

  // internal device name of the monitor, this is not a display name
  //
  const QString& monitorDevice() const;

  // whether this monitor is the primary
  //
  bool primary() const;

  // resolution
  //
  int resX() const;
  int resY() const;

  // dpi
  //
  int dpi() const;

  // refresh rate in hz
  //
  int refreshRate() const;

  // string representation
  //
  QString toString() const;

private:
  QString m_adapter;
  QString m_monitorDevice;
  bool m_primary;
  int m_resX{0}, m_resY{0};
  int m_dpi{0};
  int m_refreshRate{0};

  void getSettings();
};

// holds various information about Windows metrics
//
class Metrics
{
public:
  Metrics();

  // list of displays on the system
  //
  const std::vector<Display>& displays() const;

  // full resolution
  //
  static QRect desktopGeometry() ;

private:
  std::vector<Display> m_displays;

  void getDisplays();
};

}  // namespace env

#endif  // ENV_METRICS_H
