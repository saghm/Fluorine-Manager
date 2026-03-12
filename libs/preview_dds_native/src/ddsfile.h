#ifndef DDSFILE_H
#define DDSFILE_H

#include "ddsformat.h"

#include <QByteArray>
#include <QString>
#include <QVector>

struct DDSMipLevel {
  uint32_t width;
  uint32_t height;
  QByteArray data;
};

struct DDSFace {
  QVector<DDSMipLevel> mips;
};

class DDSFile
{
public:
  bool loadFromFile(const QString& path);
  bool loadFromData(const QByteArray& data);

  uint32_t width() const { return m_width; }
  uint32_t height() const { return m_height; }
  bool isCubemap() const { return m_cubemap; }
  int faceCount() const { return m_faces.size(); }
  int mipCount() const { return m_faces.isEmpty() ? 0 : m_faces[0].mips.size(); }
  const DDSFace& face(int i) const { return m_faces[i]; }
  const GLFormatInfo& glFormat() const { return m_glFormat; }
  QString description() const { return m_description; }

private:
  bool parse(const QByteArray& data);

  uint32_t m_width    = 0;
  uint32_t m_height   = 0;
  bool m_cubemap      = false;
  GLFormatInfo m_glFormat;
  DXGIFormat m_dxgiFormat = DXGIFormat::UNKNOWN;
  QString m_description;
  QVector<DDSFace> m_faces;
};

#endif  // DDSFILE_H
