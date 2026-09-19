#include "fluorinemenubar.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>

void FluorineMenuBar::mousePressEvent(QMouseEvent* event)
{
  if (QGuiApplication::platformName().startsWith("wayland") && closeActivePopup(event)) {
    return;
  }
  QMenuBar::mousePressEvent(event);
}

bool FluorineMenuBar::closeActivePopup(QMouseEvent* event)
{
  auto* action = actionAt(event->position().toPoint());
  if (event->button() != Qt::LeftButton || !action || action != activeAction() ||
      !action->isEnabled() || !action->isVisible() || !action->menu() ||
      !action->menu()->isVisible()) {
    return false;
  }

  // Qt 6.11's click-to-close path hides the popup, then grabs the mouse on
  // the menu bar's ordinary top-level window. Wayland only permits popup
  // grabs. Use Qt's keyboard dismissal path to reset its popup/focus state.
  action->menu()->setAttribute(Qt::WA_NoMouseReplay);
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QMenuBar::keyPressEvent(&escape);

  // A real release may stay with the now-hidden Wayland popup. Reset Qt's
  // pressed state immediately so moving across the bar cannot reopen a menu.
  QMouseEvent release(QEvent::MouseButtonRelease, event->position(),
                       event->globalPosition(), Qt::LeftButton,
                       event->buttons() & ~Qt::LeftButton, event->modifiers(),
                       event->pointingDevice());
  QMenuBar::mouseReleaseEvent(&release);
  event->accept();
  return true;
}
