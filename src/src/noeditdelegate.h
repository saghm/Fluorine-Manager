#ifndef NOEDITDELEGATE_H
#define NOEDITDELEGATE_H

#include <QStyledItemDelegate>

class NoEditDelegate : public QStyledItemDelegate
{
public:
  NoEditDelegate(QObject* parent = nullptr);
  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const override;
};

#endif  // NOEDITDELEGATE_H
