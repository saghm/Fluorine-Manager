#include "omodinstaller.h"

#include <uibase/guessedvalue.h>
#include <uibase/iinstallationmanager.h>
#include <uibase/log.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <zlib.h>
#include <lzma.h>

#include <cstring>
#include <stdexcept>

// ============================================================================
// BinaryReader
// ============================================================================

OmodInstaller::BinaryReader::BinaryReader(const QByteArray& data) : m_data(data) {}

uint8_t OmodInstaller::BinaryReader::readByte()
{
  if (m_pos >= m_data.size())
    throw std::runtime_error("BinaryReader: read past end");
  return static_cast<uint8_t>(m_data[m_pos++]);
}

int32_t OmodInstaller::BinaryReader::readInt32LE()
{
  if (m_pos + 4 > m_data.size())
    throw std::runtime_error("BinaryReader: read past end");
  int32_t val;
  std::memcpy(&val, m_data.constData() + m_pos, 4);
  m_pos += 4;
  return val;
}

uint32_t OmodInstaller::BinaryReader::readUInt32LE()
{
  if (m_pos + 4 > m_data.size())
    throw std::runtime_error("BinaryReader: read past end");
  uint32_t val;
  std::memcpy(&val, m_data.constData() + m_pos, 4);
  m_pos += 4;
  return val;
}

int64_t OmodInstaller::BinaryReader::readInt64LE()
{
  if (m_pos + 8 > m_data.size())
    throw std::runtime_error("BinaryReader: read past end");
  int64_t val;
  std::memcpy(&val, m_data.constData() + m_pos, 8);
  m_pos += 8;
  return val;
}

void OmodInstaller::BinaryReader::skip(int bytes)
{
  m_pos += bytes;
  if (m_pos > m_data.size())
    throw std::runtime_error("BinaryReader: skip past end");
}

int OmodInstaller::BinaryReader::read7BitEncodedInt()
{
  int result = 0;
  int shift  = 0;
  while (true) {
    uint8_t b = readByte();
    result |= (b & 0x7F) << shift;
    shift += 7;
    if ((b & 0x80) == 0)
      break;
  }
  return result;
}

QString OmodInstaller::BinaryReader::readNetString()
{
  int length = read7BitEncodedInt();
  if (length == 0)
    return {};
  QByteArray raw = readBytes(length);
  return QString::fromUtf8(raw);
}

QByteArray OmodInstaller::BinaryReader::readBytes(int count)
{
  if (m_pos + count > m_data.size())
    throw std::runtime_error("BinaryReader: read past end");
  QByteArray result = m_data.mid(m_pos, count);
  m_pos += count;
  return result;
}

bool OmodInstaller::BinaryReader::atEnd() const
{
  return m_pos >= m_data.size();
}

// ============================================================================
// OmodInstaller
// ============================================================================

OmodInstaller::OmodInstaller() = default;

bool OmodInstaller::init(MOBase::IOrganizer* organizer)
{
  m_organizer = organizer;
  return true;
}

QString OmodInstaller::name() const
{
  return "OMOD Installer (Native)";
}

QString OmodInstaller::localizedName() const
{
  return "OMOD Installer (Native)";
}

QString OmodInstaller::author() const
{
  return "Fluorine Manager";
}

QString OmodInstaller::description() const
{
  return "Installer for .omod archives (Oblivion Mod Manager format)";
}

MOBase::VersionInfo OmodInstaller::version() const
{
  return MOBase::VersionInfo(1, 0, 0);
}

QList<MOBase::PluginSetting> OmodInstaller::settings() const
{
  return {};
}

unsigned int OmodInstaller::priority() const
{
  return 500;
}

bool OmodInstaller::isManualInstaller() const
{
  return false;
}

bool OmodInstaller::isArchiveSupported(
    std::shared_ptr<const MOBase::IFileTree> tree) const
{
  if (!tree)
    return false;

  // An OMOD zip always contains a "config" entry.
  for (auto it = tree->begin(); it != tree->end(); ++it) {
    auto entry = *it;
    if (entry && !entry->isDir() && entry->name().compare("config", Qt::CaseInsensitive) == 0)
      return true;
  }
  return false;
}

bool OmodInstaller::isArchiveSupported(const QString& archiveName) const
{
  return archiveName.toLower().endsWith(".omod");
}

std::set<QString> OmodInstaller::supportedExtensions() const
{
  return {"omod"};
}

OmodInstaller::EInstallResult OmodInstaller::install(
    MOBase::GuessedValue<QString>& modName, QString /*gameName*/,
    const QString& archiveName, const QString& /*version*/, int /*nexusID*/)
{
  try {
    return doInstall(modName, archiveName);
  } catch (const std::exception& e) {
    MOBase::log::error("OMOD install failed: {}", e.what());
    return RESULT_FAILED;
  }
}

OmodInstaller::EInstallResult OmodInstaller::doInstall(
    MOBase::GuessedValue<QString>& modName, const QString& archiveName)
{
  // Extract the .omod (zip) to a temp directory using unzip.
  QTemporaryDir omodTmp;
  if (!omodTmp.isValid()) {
    MOBase::log::error("OMOD: failed to create temp directory");
    return RESULT_FAILED;
  }

  {
    QProcess unzip;
    unzip.setWorkingDirectory(omodTmp.path());
    unzip.start("unzip", {"-o", archiveName, "-d", omodTmp.path()});
    unzip.waitForFinished(60000);
    if (unzip.exitCode() != 0) {
      MOBase::log::error("OMOD: unzip failed: {}",
                         unzip.readAllStandardError().toStdString());
      return RESULT_FAILED;
    }
  }

  // Read and parse the config entry.
  QString configPath = QDir(omodTmp.path()).filePath("config");
  if (!QFile::exists(configPath)) {
    MOBase::log::warn("OMOD: no config entry found");
    return RESULT_NOTATTEMPTED;
  }

  QFile configFile(configPath);
  if (!configFile.open(QIODevice::ReadOnly)) {
    MOBase::log::error("OMOD: could not read config");
    return RESULT_FAILED;
  }
  QByteArray configData = configFile.readAll();
  configFile.close();

  OmodConfig config = parseConfig(configData);
  if (!config.modName.isEmpty()) {
    modName.update(config.modName, MOBase::GUESS_META);
  }

  // Create a separate temp directory for the extracted files.
  QTemporaryDir extractTmp;
  if (!extractTmp.isValid()) {
    MOBase::log::error("OMOD: failed to create extraction temp directory");
    return RESULT_FAILED;
  }

  // Extract data and plugins streams.
  extractStream(omodTmp.path(), "data", "data.crc", config.compressionType,
                extractTmp.path());
  extractStream(omodTmp.path(), "plugins", "plugins.crc", config.compressionType,
                extractTmp.path());

  // Copy readme files if present.
  QDir omodDir(omodTmp.path());
  QStringList entries = omodDir.entryList(QDir::Files);
  for (const QString& entry : entries) {
    QString lower = entry.toLower();
    if (lower == "readme" || lower.startsWith("readme.")) {
      QFile::copy(omodDir.filePath(entry),
                  QDir(extractTmp.path()).filePath(entry));
    }
  }

  // Verify we have files to install.
  QDirIterator dirIt(extractTmp.path(), QDir::Files, QDirIterator::Subdirectories);
  if (!dirIt.hasNext()) {
    MOBase::log::warn("OMOD: no files extracted");
    return RESULT_FAILED;
  }

  // Repackage extracted files as a standard zip for MO2's installer.
  QString repackPath = QDir(extractTmp.path()).filePath("_repack.zip");
  {
    QProcess zip;
    zip.setWorkingDirectory(extractTmp.path());
    // Collect all files relative to extractTmp, excluding the repack zip itself.
    QStringList zipArgs;
    zipArgs << "-r" << repackPath << ".";
    zipArgs << "-x" << "./_repack.zip";
    zip.start("zip", zipArgs);
    zip.waitForFinished(120000);
    if (zip.exitCode() != 0) {
      MOBase::log::error("OMOD: zip repackaging failed: {}",
                         zip.readAllStandardError().toStdString());
      return RESULT_FAILED;
    }
  }

  return manager()->installArchive(modName, repackPath);
}

// ============================================================================
// OMOD binary format parsing
// ============================================================================

OmodInstaller::OmodConfig OmodInstaller::parseConfig(const QByteArray& data)
{
  BinaryReader reader(data);
  OmodConfig config;

  config.fileVersion = reader.readByte();
  config.modName     = reader.readNetString();
  config.major       = reader.readInt32LE();
  config.minor       = reader.readInt32LE();
  config.authorName  = reader.readNetString();
  config.email       = reader.readNetString();
  config.website     = reader.readNetString();
  config.desc        = reader.readNetString();

  // Windows FILETIME (8 bytes) - skip.
  reader.skip(8);

  // Compression type: 0 = deflate, 1 = lzma.
  config.compressionType = reader.readByte();

  return config;
}

std::vector<OmodInstaller::CrcEntry>
OmodInstaller::parseCrcFile(const QByteArray& data)
{
  BinaryReader reader(data);
  int count = reader.read7BitEncodedInt();

  std::vector<CrcEntry> entries;
  entries.reserve(count);

  for (int i = 0; i < count; ++i) {
    CrcEntry entry;
    entry.path = reader.readNetString();
    entry.crc  = reader.readUInt32LE();
    entry.size = reader.readInt64LE();
    entries.push_back(std::move(entry));
  }

  return entries;
}

QByteArray OmodInstaller::decompressStream(const QByteArray& data,
                                           uint8_t compressionType)
{
  if (compressionType == 0) {
    // Raw deflate (zlib with wbits = -15, no header).
    z_stream strm{};
    if (inflateInit2(&strm, -15) != Z_OK) {
      throw std::runtime_error("inflateInit2 failed");
    }

    strm.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    strm.avail_in = static_cast<uInt>(data.size());

    QByteArray result;
    char buffer[65536];

    int ret;
    do {
      strm.next_out  = reinterpret_cast<Bytef*>(buffer);
      strm.avail_out = sizeof(buffer);
      ret            = inflate(&strm, Z_NO_FLUSH);
      if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
        inflateEnd(&strm);
        throw std::runtime_error("zlib inflate failed");
      }
      int have = sizeof(buffer) - strm.avail_out;
      result.append(buffer, have);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return result;

  } else if (compressionType == 1) {
    // LZMA decompression.
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret     = lzma_alone_decoder(&strm, UINT64_MAX);
    if (ret != LZMA_OK) {
      throw std::runtime_error("lzma_alone_decoder init failed");
    }

    strm.next_in  = reinterpret_cast<const uint8_t*>(data.constData());
    strm.avail_in = data.size();

    QByteArray result;
    uint8_t buffer[65536];

    do {
      strm.next_out  = buffer;
      strm.avail_out = sizeof(buffer);
      ret            = lzma_code(&strm, LZMA_FINISH);
      if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
        lzma_end(&strm);
        throw std::runtime_error("lzma decompression failed");
      }
      size_t have = sizeof(buffer) - strm.avail_out;
      result.append(reinterpret_cast<const char*>(buffer), static_cast<int>(have));
    } while (ret != LZMA_STREAM_END);

    lzma_end(&strm);
    return result;

  } else {
    throw std::runtime_error(
        std::string("Unknown OMOD compression type: ") +
        std::to_string(compressionType));
  }
}

void OmodInstaller::extractStream(const QString& omodDir, const QString& streamName,
                                  const QString& crcName, uint8_t compressionType,
                                  const QString& outDir)
{
  QDir dir(omodDir);
  QString streamPath = dir.filePath(streamName);
  QString crcPath    = dir.filePath(crcName);

  if (!QFile::exists(streamPath))
    return;

  if (!QFile::exists(crcPath)) {
    MOBase::log::warn("OMOD: {} present but {} missing", streamName.toStdString(),
                      crcName.toStdString());
    return;
  }

  // Read CRC file.
  QFile crcFile(crcPath);
  if (!crcFile.open(QIODevice::ReadOnly))
    return;
  QByteArray crcData = crcFile.readAll();
  crcFile.close();

  std::vector<CrcEntry> fileList = parseCrcFile(crcData);
  if (fileList.empty())
    return;

  // Read and decompress the stream.
  QFile streamFile(streamPath);
  if (!streamFile.open(QIODevice::ReadOnly))
    return;
  QByteArray rawData = streamFile.readAll();
  streamFile.close();

  QByteArray decompressed = decompressStream(rawData, compressionType);

  // Split the decompressed data into individual files.
  int offset = 0;
  for (const CrcEntry& entry : fileList) {
    if (offset + entry.size > decompressed.size()) {
      MOBase::log::warn(
          "OMOD: truncated stream for {} (need {} bytes at offset {}, have {})",
          entry.path.toStdString(), entry.size, offset, decompressed.size());
      break;
    }

    QByteArray fileData = decompressed.mid(offset, static_cast<int>(entry.size));
    offset += static_cast<int>(entry.size);

    // Normalise Windows path separators.
    QString normalPath = entry.path;
    normalPath.replace('\\', '/');

    QString destPath = QDir(outDir).filePath(normalPath);

    // Ensure parent directory exists.
    QDir().mkpath(QFileInfo(destPath).absolutePath());

    QFile outFile(destPath);
    if (outFile.open(QIODevice::WriteOnly)) {
      outFile.write(fileData);
      outFile.close();
    } else {
      MOBase::log::warn("OMOD: could not write {}", destPath.toStdString());
    }
  }
}
