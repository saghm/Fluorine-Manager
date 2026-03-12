#include "ddsfile.h"

#include <QFile>

#include <algorithm>
#include <cstring>

bool DDSFile::loadFromFile(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  return parse(file.readAll());
}

bool DDSFile::loadFromData(const QByteArray& data)
{
  return parse(data);
}

bool DDSFile::parse(const QByteArray& data)
{
  const char* ptr = data.constData();
  int offset      = 0;
  int size        = data.size();

  // Magic number
  if (size < 4)
    return false;
  uint32_t magic;
  std::memcpy(&magic, ptr + offset, 4);
  offset += 4;
  if (magic != DDS_MAGIC)
    return false;

  // DDS header
  if (size - offset < static_cast<int>(sizeof(DDSHeader)))
    return false;
  DDSHeader header;
  std::memcpy(&header, ptr + offset, sizeof(DDSHeader));
  offset += sizeof(DDSHeader);

  if (header.dwSize != 124)
    return false;

  // DXT10 extended header
  DDSHeaderDXT10 dxt10{};
  bool hasDXT10 = false;
  if ((header.ddspf.dwFlags & DDPF_FOURCC) &&
      header.ddspf.dwFourCC == makeFourCC('D', 'X', '1', '0')) {
    if (size - offset < static_cast<int>(sizeof(DDSHeaderDXT10)))
      return false;
    std::memcpy(&dxt10, ptr + offset, sizeof(DDSHeaderDXT10));
    offset += sizeof(DDSHeaderDXT10);
    hasDXT10 = true;
  }

  m_width  = header.dwWidth;
  m_height = header.dwHeight;

  // Determine GL format
  m_glFormat = getGLFormat(header.ddspf, hasDXT10 ? &dxt10 : nullptr);
  if (!m_glFormat.valid) {
    return false;
  }

  // Determine DXGI format for size calculations
  if (hasDXT10) {
    m_dxgiFormat = dxt10.dxgiFormat;
  } else if (header.ddspf.dwFlags & DDPF_FOURCC) {
    m_dxgiFormat = fourCCToDXGI(header.ddspf.dwFourCC);
  } else {
    m_dxgiFormat = DXGIFormat::UNKNOWN;
  }

  // Description
  m_description = formatDescription(header.ddspf, hasDXT10 ? &dxt10 : nullptr);
  m_description +=
      QString(" | %1x%2").arg(m_width).arg(m_height);

  // Cubemap detection
  m_cubemap  = false;
  int layers = 1;
  if (header.dwCaps2 & DDSCAPS2_CUBEMAP) {
    m_cubemap = true;
    layers    = 0;
    if (header.dwCaps2 & DDSCAPS2_CUBEMAP_POSITIVEX) layers++;
    if (header.dwCaps2 & DDSCAPS2_CUBEMAP_NEGATIVEX) layers++;
    if (header.dwCaps2 & DDSCAPS2_CUBEMAP_POSITIVEY) layers++;
    if (header.dwCaps2 & DDSCAPS2_CUBEMAP_NEGATIVEY) layers++;
    if (header.dwCaps2 & DDSCAPS2_CUBEMAP_POSITIVEZ) layers++;
    if (header.dwCaps2 & DDSCAPS2_CUBEMAP_NEGATIVEZ) layers++;
    m_description += " | Cubemap";
  } else {
    m_description += " | 2D";
  }

  // Mipmap count
  int mipCount = 1;
  if (header.dwFlags & DDSD_MIPMAPCOUNT) {
    mipCount = std::max(1u, header.dwMipMapCount);
  }
  if (mipCount > 1) {
    m_description += QString(" | %1 mips").arg(mipCount);
  }

  // Read pixel data for each face and mip level
  m_faces.resize(layers);
  for (int face = 0; face < layers; ++face) {
    m_faces[face].mips.resize(mipCount);
    uint32_t w = m_width;
    uint32_t h = m_height;
    for (int mip = 0; mip < mipCount; ++mip) {
      uint32_t dataSize = mipDataSize(m_dxgiFormat, header.ddspf, w, h);
      if (offset + static_cast<int>(dataSize) > size) {
        // Truncated file, keep what we have
        m_faces[face].mips.resize(mip);
        break;
      }
      m_faces[face].mips[mip].width  = w;
      m_faces[face].mips[mip].height = h;
      m_faces[face].mips[mip].data =
          QByteArray(ptr + offset, static_cast<int>(dataSize));
      offset += dataSize;
      w = std::max(1u, w / 2);
      h = std::max(1u, h / 2);
    }
  }

  return !m_faces.isEmpty() && !m_faces[0].mips.isEmpty();
}
