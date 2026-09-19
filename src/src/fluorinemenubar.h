#pragma once

#include <QMenuBar>

class FluorineMenuBar : public QMenuBar
{
public:
  using QMenuBar::QMenuBar;

protected:
  void mousePressEvent(QMouseEvent* event) override;
  bool closeActivePopup(QMouseEvent* event);
};
