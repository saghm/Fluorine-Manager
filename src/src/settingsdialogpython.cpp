#include "settingsdialogpython.h"

#include "fluorinepaths.h"
#include "ui_settingsdialog.h"

#include <log.h>
#include <uibase/utility.h>

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>

PythonSettingsTab::PythonSettingsTab(Settings& s, SettingsDialog& d)
    : QObject(&d), SettingsTab(s, d)
{
  m_venvPath = fluorineDataDir() + "/python-venv";
  ui->venvPathEdit->setText(m_venvPath);

  // Load saved state
  bool pythonEnabled = QSettings().value("fluorine/python_enabled", false).toBool();
  ui->pythonEnableCheck->setChecked(pythonEnabled);

  detectPython();
  refreshVenvState();

  QObject::connect(ui->pythonRefreshButton, &QPushButton::clicked, this,
                   &PythonSettingsTab::detectPython);
  QObject::connect(ui->createVenvButton, &QPushButton::clicked, this,
                   &PythonSettingsTab::onCreateVenv);
  QObject::connect(ui->deleteVenvButton, &QPushButton::clicked, this,
                   &PythonSettingsTab::onDeleteVenv);
  QObject::connect(ui->openVenvFolderButton, &QPushButton::clicked, this,
                   &PythonSettingsTab::onOpenVenvFolder);
}

void PythonSettingsTab::update()
{
  QSettings().setValue("fluorine/python_enabled",
                       ui->pythonEnableCheck->isChecked());
}

void PythonSettingsTab::detectPython()
{
  m_pythonPath = QStandardPaths::findExecutable("python3");
  if (m_pythonPath.isEmpty()) {
    m_pythonPath = QStandardPaths::findExecutable("python");
  }

  if (m_pythonPath.isEmpty()) {
    ui->pythonPathEdit->setText("");
    ui->pythonVersionValue->setText("Python 3 not found in PATH");
    return;
  }

  ui->pythonPathEdit->setText(m_pythonPath);

  // Get version
  QProcess proc;
  proc.start(m_pythonPath, {"--version"});
  proc.waitForFinished(5000);

  if (proc.exitCode() == 0) {
    QString version = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    if (version.isEmpty()) {
      version = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    }
    ui->pythonVersionValue->setText(version);
  } else {
    ui->pythonVersionValue->setText("Error detecting version");
  }
}

void PythonSettingsTab::refreshVenvState()
{
  QDir venvDir(m_venvPath);
  bool exists = venvDir.exists() && venvDir.exists("bin/python3");

  if (exists) {
    // Check if PyQt6 is installed
    QProcess proc;
    proc.start(m_venvPath + "/bin/python3",
               {"-c", "import PyQt6; print(PyQt6.__file__)"});
    proc.waitForFinished(5000);

    if (proc.exitCode() == 0) {
      ui->venvStatusValue->setText("Active (PyQt6 installed)");
    } else {
      ui->venvStatusValue->setText("Active (PyQt6 NOT installed)");
    }
  } else {
    ui->venvStatusValue->setText("Not created");
  }

  ui->deleteVenvButton->setEnabled(exists);
  ui->openVenvFolderButton->setEnabled(exists);
}

void PythonSettingsTab::onCreateVenv()
{
  if (m_pythonPath.isEmpty()) {
    QMessageBox::warning(parentWidget(), tr("No Python"),
                         tr("Python 3 was not found in PATH.\n\n"
                            "Install Python 3 using your package manager:\n"
                            "  Arch: pacman -S python\n"
                            "  Ubuntu: apt install python3 python3-venv\n"
                            "  Fedora: dnf install python3"));
    return;
  }

  ui->pythonLogOutput->setVisible(true);
  ui->pythonLogOutput->clear();
  ui->venvStatusValue->setText("Creating virtual environment...");

  // Create venv
  {
    QProcess proc;
    proc.start(m_pythonPath, {"-m", "venv", m_venvPath});
    proc.waitForFinished(30000);

    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    QString errors = QString::fromUtf8(proc.readAllStandardError());
    if (!output.isEmpty())
      ui->pythonLogOutput->append(output);
    if (!errors.isEmpty())
      ui->pythonLogOutput->append(errors);

    if (proc.exitCode() != 0) {
      ui->venvStatusValue->setText("Failed to create venv");
      ui->pythonLogOutput->append("venv creation failed with exit code " +
                                  QString::number(proc.exitCode()));
      return;
    }
  }

  ui->pythonLogOutput->append("Virtual environment created. Installing PyQt6...");
  ui->venvStatusValue->setText("Installing PyQt6...");

  // Install PyQt6
  {
    QProcess proc;
    proc.start(m_venvPath + "/bin/pip",
               {"install", "--upgrade", "pip", "PyQt6"});
    proc.waitForFinished(120000);  // PyQt6 is large, give it time

    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    QString errors = QString::fromUtf8(proc.readAllStandardError());
    if (!output.isEmpty())
      ui->pythonLogOutput->append(output);
    if (!errors.isEmpty())
      ui->pythonLogOutput->append(errors);

    if (proc.exitCode() != 0) {
      ui->venvStatusValue->setText("PyQt6 installation failed");
      ui->pythonLogOutput->append("pip install failed with exit code " +
                                  QString::number(proc.exitCode()));
      refreshVenvState();
      return;
    }
  }

  ui->pythonLogOutput->append("PyQt6 installed successfully.");
  MOBase::log::info("Python venv created at {}", m_venvPath);

  refreshVenvState();
}

void PythonSettingsTab::onDeleteVenv()
{
  QDir venvDir(m_venvPath);
  if (!venvDir.exists()) {
    refreshVenvState();
    return;
  }

  auto result = QMessageBox::question(
      parentWidget(), tr("Delete Virtual Environment"),
      tr("Delete the Python virtual environment at:\n%1\n\n"
         "This will disable .py plugin loading until recreated.")
          .arg(m_venvPath));

  if (result != QMessageBox::Yes)
    return;

  if (venvDir.removeRecursively()) {
    ui->pythonLogOutput->setVisible(false);
    MOBase::log::info("Python venv deleted at {}", m_venvPath);
  } else {
    QMessageBox::warning(parentWidget(), tr("Error"),
                         tr("Failed to delete the virtual environment."));
  }

  refreshVenvState();
}

void PythonSettingsTab::onOpenVenvFolder()
{
  QDir venvDir(m_venvPath);
  if (venvDir.exists()) {
    MOBase::shell::Explore(venvDir);
  }
}
