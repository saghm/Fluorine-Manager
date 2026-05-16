/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "selectiondialog.h"
#include "ui_selectiondialog.h"

#include <QCommandLinkButton>
#include <QHBoxLayout>
#include <QIcon>
#include <QMessageBox>
#include <QStyle>
#include <QToolButton>
#include <QWidget>

SelectionDialog::SelectionDialog(const QString& description, QWidget* parent,
                                 const QSize& iconSize)
    : QDialog(parent), ui(new Ui::SelectionDialog),  m_IconSize(iconSize)
{
  ui->setupUi(this);

  ui->descriptionLabel->setText(description);
}

SelectionDialog::~SelectionDialog()
{
  delete ui;
}

void SelectionDialog::addChoice(const QString& buttonText, const QString& description,
                                const QVariant& data)
{
  QAbstractButton* button =
      new QCommandLinkButton(buttonText, description, ui->buttonBox);
  if (m_IconSize.isValid()) {
    button->setIconSize(m_IconSize);
  }
  button->setProperty("data", data);
  ui->buttonBox->addButton(button, QDialogButtonBox::AcceptRole);
  if (data.isValid())
    m_ValidateByData = true;
}

void SelectionDialog::addChoice(const QIcon& icon, const QString& buttonText,
                                const QString& description, const QVariant& data)
{
  QAbstractButton* button =
      new QCommandLinkButton(buttonText, description, ui->buttonBox);
  if (m_IconSize.isValid()) {
    button->setIconSize(m_IconSize);
  }
  button->setIcon(icon);
  button->setProperty("data", data);
  ui->buttonBox->addButton(button, QDialogButtonBox::AcceptRole);
  if (data.isValid())
    m_ValidateByData = true;
}

void SelectionDialog::addChoice(const QString& buttonText,
                                const QString& description,
                                const QVariant& data,
                                std::function<bool()> onDelete)
{
  // Custom row: [QCommandLinkButton (stretch)] [trash QToolButton]. Added
  // directly to the same vertical layout the button box sits in, so it
  // visually matches the rows produced by the standard addChoice().
  auto* row    = new QWidget(ui->scrollAreaWidgetContents);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);

  auto* choice = new QCommandLinkButton(buttonText, description, row);
  if (m_IconSize.isValid()) {
    choice->setIconSize(m_IconSize);
  }
  choice->setProperty("data", data);

  auto* trash = new QToolButton(row);
  trash->setAutoRaise(true);
  QIcon trashIcon = QIcon::fromTheme(QStringLiteral("user-trash"));
  if (trashIcon.isNull()) {
    trashIcon = QIcon::fromTheme(QStringLiteral("edit-delete"));
  }
  if (trashIcon.isNull()) {
    trashIcon = style()->standardIcon(QStyle::SP_TrashIcon);
  }
  trash->setIcon(trashIcon);
  trash->setToolTip(tr("Delete this backup"));
  trash->setAccessibleName(tr("Delete backup '%1'").arg(buttonText));

  layout->addWidget(choice, /*stretch=*/1);
  layout->addWidget(trash, /*stretch=*/0);

  // Insert ABOVE the (empty) buttonBox so visual order matches insertion
  // order regardless of how many standard rows have been added.
  auto* parentLayout =
      qobject_cast<QVBoxLayout*>(ui->scrollAreaWidgetContents->layout());
  if (parentLayout) {
    const int idx = parentLayout->indexOf(ui->buttonBox);
    if (idx >= 0) {
      parentLayout->insertWidget(idx, row);
    } else {
      parentLayout->addWidget(row);
    }
  }

  if (data.isValid()) {
    m_ValidateByData = true;
  }

  connect(choice, &QAbstractButton::clicked, this, [this, choice]() {
    m_Choice = choice;
    if (!m_ValidateByData || m_Choice->property("data").isValid()) {
      accept();
    } else {
      reject();
    }
  });

  connect(trash, &QToolButton::clicked, this,
          [this, row, choice, buttonText, onDelete]() {
            const auto ans = QMessageBox::question(
                this, tr("Delete backup?"),
                tr("Delete backup '%1'? This cannot be undone.").arg(buttonText),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ans != QMessageBox::Yes) {
              return;
            }
            if (!onDelete) {
              row->setEnabled(false);
              return;
            }
            if (onDelete()) {
              if (m_Choice == choice) {
                m_Choice = nullptr;
              }
              row->setVisible(false);
              row->deleteLater();
            } else {
              QMessageBox::warning(
                  this, tr("Delete failed"),
                  tr("Failed to delete backup '%1'. Check the log for details.")
                      .arg(buttonText));
            }
          });
}

int SelectionDialog::numChoices() const
{
  // Count standard rows (inside buttonBox) plus delete-enabled rows (whose
  // QCommandLinkButton lives in a custom row widget under
  // scrollAreaWidgetContents). Visibility check excludes rows the user has
  // already deleted via the trash button.
  int n = 0;
  const auto buttons =
      ui->scrollAreaWidgetContents->findChildren<QCommandLinkButton*>();
  for (const auto* b : buttons) {
    if (b->isVisibleTo(ui->scrollAreaWidgetContents)) {
      ++n;
    }
  }
  return n;
}

QVariant SelectionDialog::getChoiceData()
{
  return m_Choice->property("data");
}

QString SelectionDialog::getChoiceString()
{
  if ((m_Choice == nullptr) ||
      (m_ValidateByData && !m_Choice->property("data").isValid())) {
    return {};
  } else {
    return m_Choice->text();
  }
}

QString SelectionDialog::getChoiceDescription()
{
  if (m_Choice == nullptr)
    return {};
  else
    return m_Choice->accessibleDescription();
}

void SelectionDialog::disableCancel()
{
  ui->cancelButton->setEnabled(false);
  ui->cancelButton->setHidden(true);
}

void SelectionDialog::on_buttonBox_clicked(QAbstractButton* button)
{
  m_Choice = button;
  if (!m_ValidateByData || m_Choice->property("data").isValid()) {
    this->accept();
  } else {
    this->reject();
  }
}

void SelectionDialog::on_cancelButton_clicked()
{
  this->reject();
}
