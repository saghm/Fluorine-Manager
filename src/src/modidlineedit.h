#ifndef MODIDLINEEDIT_H
#define MODIDLINEEDIT_H

#include <QLineEdit>

class ModIDLineEdit : public QLineEdit
{
  Q_OBJECT

public:
  explicit ModIDLineEdit(QWidget* parent = nullptr);
  explicit ModIDLineEdit(const QString& text, QWidget* parent = nullptr);

public:
  bool event(QEvent* event) override;

signals:
  void linkClicked(QString);
};

#endif  // MODIDLINEEDIT_H
