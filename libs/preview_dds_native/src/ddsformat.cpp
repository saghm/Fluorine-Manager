#include "ddsformat.h"

#include <QOpenGLFunctions>
#include <QString>

#include <algorithm>
#include <cstring>

// GL compressed format constants not always in headers
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_RED_RGTC1
#define GL_COMPRESSED_RED_RGTC1 0x8DBB
#endif
#ifndef GL_COMPRESSED_SIGNED_RED_RGTC1
#define GL_COMPRESSED_SIGNED_RED_RGTC1 0x8DBC
#endif
#ifndef GL_COMPRESSED_RG_RGTC2
#define GL_COMPRESSED_RG_RGTC2 0x8DBD
#endif
#ifndef GL_COMPRESSED_SIGNED_RG_RGTC2
#define GL_COMPRESSED_SIGNED_RG_RGTC2 0x8DBE
#endif
#ifndef GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT
#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT 0x8E8F
#endif
#ifndef GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT
#define GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT 0x8E8E
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM 0x8E8D
#endif
#ifndef GL_COMPRESSED_SRGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT 0x8C4C
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT 0x8C4E
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif

static GLFormatInfo dxgiToGL(DXGIFormat fmt)
{
  GLFormatInfo info;
  info.valid = true;

  switch (fmt) {
  // BC1 (DXT1)
  case DXGIFormat::BC1_UNORM:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    break;
  case DXGIFormat::BC1_UNORM_SRGB:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
    break;
  // BC2 (DXT3)
  case DXGIFormat::BC2_UNORM:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    break;
  case DXGIFormat::BC2_UNORM_SRGB:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
    break;
  // BC3 (DXT5)
  case DXGIFormat::BC3_UNORM:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    break;
  case DXGIFormat::BC3_UNORM_SRGB:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
    break;
  // BC4
  case DXGIFormat::BC4_UNORM:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_RED_RGTC1;
    break;
  case DXGIFormat::BC4_SNORM:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_SIGNED_RED_RGTC1;
    break;
  // BC5
  case DXGIFormat::BC5_UNORM:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_RG_RGTC2;
    break;
  case DXGIFormat::BC5_SNORM:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_SIGNED_RG_RGTC2;
    break;
  // BC6H
  case DXGIFormat::BC6H_UF16:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;
    break;
  case DXGIFormat::BC6H_SF16:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT;
    break;
  // BC7
  case DXGIFormat::BC7_UNORM:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_RGBA_BPTC_UNORM;
    break;
  case DXGIFormat::BC7_UNORM_SRGB:
    info.compressed     = true;
    info.internalFormat = GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
    break;

  // Uncompressed RGBA
  case DXGIFormat::R8G8B8A8_UNORM:
  case DXGIFormat::R8G8B8A8_UNORM_SRGB:
    info.internalFormat = GL_RGBA8;
    info.format         = GL_RGBA;
    info.type           = GL_UNSIGNED_BYTE;
    break;
  case DXGIFormat::R8G8B8A8_UINT:
    info.internalFormat = GL_RGBA8UI;
    info.format         = GL_RGBA_INTEGER;
    info.type           = GL_UNSIGNED_BYTE;
    info.sampler        = SamplerType::UInt;
    break;
  case DXGIFormat::R8G8B8A8_SNORM:
    info.internalFormat = GL_RGBA8_SNORM;
    info.format         = GL_RGBA;
    info.type           = GL_BYTE;
    break;
  case DXGIFormat::R8G8B8A8_SINT:
    info.internalFormat = GL_RGBA8I;
    info.format         = GL_RGBA_INTEGER;
    info.type           = GL_BYTE;
    info.sampler        = SamplerType::SInt;
    break;

  // BGRA
  case DXGIFormat::B8G8R8A8_UNORM:
  case DXGIFormat::B8G8R8A8_UNORM_SRGB:
    info.internalFormat = GL_RGBA8;
    info.format         = GL_BGRA;
    info.type           = GL_UNSIGNED_BYTE;
    break;
  case DXGIFormat::B8G8R8X8_UNORM:
  case DXGIFormat::B8G8R8X8_UNORM_SRGB:
    info.internalFormat = GL_RGBA8;
    info.format         = GL_BGRA;
    info.type           = GL_UNSIGNED_BYTE;
    break;

  // 16-bit float
  case DXGIFormat::R16G16B16A16_FLOAT:
    info.internalFormat = GL_RGBA16F;
    info.format         = GL_RGBA;
    info.type           = GL_HALF_FLOAT;
    break;
  case DXGIFormat::R16G16B16A16_UNORM:
    info.internalFormat = GL_RGBA16;
    info.format         = GL_RGBA;
    info.type           = GL_UNSIGNED_SHORT;
    break;
  case DXGIFormat::R16G16B16A16_UINT:
    info.internalFormat = GL_RGBA16UI;
    info.format         = GL_RGBA_INTEGER;
    info.type           = GL_UNSIGNED_SHORT;
    info.sampler        = SamplerType::UInt;
    break;
  case DXGIFormat::R16G16B16A16_SNORM:
    info.internalFormat = GL_RGBA16_SNORM;
    info.format         = GL_RGBA;
    info.type           = GL_SHORT;
    break;
  case DXGIFormat::R16G16B16A16_SINT:
    info.internalFormat = GL_RGBA16I;
    info.format         = GL_RGBA_INTEGER;
    info.type           = GL_SHORT;
    info.sampler        = SamplerType::SInt;
    break;

  // 32-bit float
  case DXGIFormat::R32G32B32A32_FLOAT:
    info.internalFormat = GL_RGBA32F;
    info.format         = GL_RGBA;
    info.type           = GL_FLOAT;
    break;
  case DXGIFormat::R32G32B32_FLOAT:
    info.internalFormat = GL_RGB32F;
    info.format         = GL_RGB;
    info.type           = GL_FLOAT;
    break;

  // RG formats
  case DXGIFormat::R8G8_UNORM:
    info.internalFormat = GL_RG8;
    info.format         = GL_RG;
    info.type           = GL_UNSIGNED_BYTE;
    break;
  case DXGIFormat::R16G16_FLOAT:
    info.internalFormat = GL_RG16F;
    info.format         = GL_RG;
    info.type           = GL_HALF_FLOAT;
    break;
  case DXGIFormat::R16G16_UNORM:
    info.internalFormat = GL_RG16;
    info.format         = GL_RG;
    info.type           = GL_UNSIGNED_SHORT;
    break;
  case DXGIFormat::R32G32_FLOAT:
    info.internalFormat = GL_RG32F;
    info.format         = GL_RG;
    info.type           = GL_FLOAT;
    break;

  // R formats
  case DXGIFormat::R8_UNORM:
    info.internalFormat = GL_R8;
    info.format         = GL_RED;
    info.type           = GL_UNSIGNED_BYTE;
    break;
  case DXGIFormat::R16_FLOAT:
    info.internalFormat = GL_R16F;
    info.format         = GL_RED;
    info.type           = GL_HALF_FLOAT;
    break;
  case DXGIFormat::R16_UNORM:
    info.internalFormat = GL_R16;
    info.format         = GL_RED;
    info.type           = GL_UNSIGNED_SHORT;
    break;
  case DXGIFormat::R32_FLOAT:
    info.internalFormat = GL_R32F;
    info.format         = GL_RED;
    info.type           = GL_FLOAT;
    break;
  case DXGIFormat::A8_UNORM:
    info.internalFormat = GL_R8;
    info.format         = GL_RED;
    info.type           = GL_UNSIGNED_BYTE;
    break;

  // Packed formats
  case DXGIFormat::R10G10B10A2_UNORM:
    info.internalFormat = GL_RGB10_A2;
    info.format         = GL_RGBA;
    info.type           = GL_UNSIGNED_INT_2_10_10_10_REV;
    break;
  case DXGIFormat::R11G11B10_FLOAT:
    info.internalFormat = GL_R11F_G11F_B10F;
    info.format         = GL_RGB;
    info.type           = GL_UNSIGNED_INT_10F_11F_11F_REV;
    break;
  case DXGIFormat::B5G6R5_UNORM:
    info.internalFormat = GL_RGB565;
    info.format         = GL_RGB;
    info.type           = GL_UNSIGNED_SHORT_5_6_5;
    break;
  case DXGIFormat::B5G5R5A1_UNORM:
    info.internalFormat = GL_RGB5_A1;
    info.format         = GL_BGRA;
    info.type           = GL_UNSIGNED_SHORT_1_5_5_5_REV;
    break;
  case DXGIFormat::B4G4R4A4_UNORM:
    info.internalFormat = GL_RGBA4;
    info.format         = GL_BGRA;
    info.type           = GL_UNSIGNED_SHORT_4_4_4_4_REV;
    break;

  default:
    info.valid = false;
    break;
  }

  return info;
}

DXGIFormat fourCCToDXGI(uint32_t fourCC)
{
  if (fourCC == makeFourCC('D', 'X', 'T', '1'))
    return DXGIFormat::BC1_UNORM;
  if (fourCC == makeFourCC('D', 'X', 'T', '3'))
    return DXGIFormat::BC2_UNORM;
  if (fourCC == makeFourCC('D', 'X', 'T', '5'))
    return DXGIFormat::BC3_UNORM;
  if (fourCC == makeFourCC('B', 'C', '4', 'U') ||
      fourCC == makeFourCC('A', 'T', 'I', '1'))
    return DXGIFormat::BC4_UNORM;
  if (fourCC == makeFourCC('B', 'C', '4', 'S'))
    return DXGIFormat::BC4_SNORM;
  if (fourCC == makeFourCC('A', 'T', 'I', '2') ||
      fourCC == makeFourCC('B', 'C', '5', 'U'))
    return DXGIFormat::BC5_UNORM;
  if (fourCC == makeFourCC('B', 'C', '5', 'S'))
    return DXGIFormat::BC5_SNORM;

  // Numeric FourCC codes for float/half-float formats
  switch (fourCC) {
  case 36:
    return DXGIFormat::R16G16B16A16_UNORM;
  case 110:
    return DXGIFormat::R16G16B16A16_SNORM;
  case 111:
    return DXGIFormat::R16_FLOAT;
  case 112:
    return DXGIFormat::R16G16_FLOAT;
  case 113:
    return DXGIFormat::R16G16B16A16_FLOAT;
  case 114:
    return DXGIFormat::R32_FLOAT;
  case 115:
    return DXGIFormat::R32G32_FLOAT;
  case 116:
    return DXGIFormat::R32G32B32A32_FLOAT;
  default:
    return DXGIFormat::UNKNOWN;
  }
}

// Helper: count trailing zeros / bit shift for a mask
static int maskShift(uint32_t mask)
{
  if (mask == 0)
    return 0;
  int shift = 0;
  while ((mask & 1) == 0) {
    mask >>= 1;
    ++shift;
  }
  return shift;
}

static int maskBits(uint32_t mask)
{
  int count = 0;
  while (mask) {
    count += mask & 1;
    mask >>= 1;
  }
  return count;
}

// Build a converter for arbitrary bitmask pixel formats
static GLFormatInfo buildBitmaskFormat(const DDSPixelFormat& pf)
{
  GLFormatInfo info;
  info.valid = true;

  int rShift = maskShift(pf.dwRBitMask);
  int gShift = maskShift(pf.dwGBitMask);
  int bShift = maskShift(pf.dwBBitMask);
  int aShift = maskShift(pf.dwABitMask);
  int rBits  = maskBits(pf.dwRBitMask);
  int gBits  = maskBits(pf.dwGBitMask);
  int bBits  = maskBits(pf.dwBBitMask);
  int aBits  = maskBits(pf.dwABitMask);
  int bpp    = pf.dwRGBBitCount;

  // Try to match common uncompressed formats directly
  if (bpp == 32 && pf.dwRBitMask == 0x000000FF && pf.dwGBitMask == 0x0000FF00 &&
      pf.dwBBitMask == 0x00FF0000 &&
      (pf.dwABitMask == 0xFF000000 || pf.dwABitMask == 0)) {
    info.internalFormat = GL_RGBA8;
    info.format         = GL_RGBA;
    info.type           = GL_UNSIGNED_BYTE;
    return info;
  }
  if (bpp == 32 && pf.dwRBitMask == 0x00FF0000 && pf.dwGBitMask == 0x0000FF00 &&
      pf.dwBBitMask == 0x000000FF) {
    info.internalFormat = GL_RGBA8;
    info.format         = GL_BGRA;
    info.type           = GL_UNSIGNED_BYTE;
    return info;
  }

  // Generic converter: extract channels and pack into RGBA8
  uint32_t rMask = pf.dwRBitMask;
  uint32_t gMask = pf.dwGBitMask;
  uint32_t bMask = pf.dwBBitMask;
  uint32_t aMask = pf.dwABitMask;
  bool hasAlpha  = (pf.dwFlags & DDPF_ALPHAPIXELS) && aMask != 0;
  int bytesPerPixel = bpp / 8;

  info.internalFormat = GL_RGBA8;
  info.format         = GL_RGBA;
  info.type           = GL_UNSIGNED_BYTE;
  info.converter      = [=](const QByteArray& data, int w, int h) -> QByteArray {
    QByteArray result(w * h * 4, '\0');
    const uint8_t* src = reinterpret_cast<const uint8_t*>(data.constData());
    uint8_t* dst       = reinterpret_cast<uint8_t*>(result.data());

    for (int i = 0; i < w * h; ++i) {
      uint32_t pixel = 0;
      std::memcpy(&pixel, src + i * bytesPerPixel,
                   std::min(bytesPerPixel, 4));

      int r = rBits > 0 ? ((pixel & rMask) >> rShift) * 255 / ((1 << rBits) - 1) : 0;
      int g = gBits > 0 ? ((pixel & gMask) >> gShift) * 255 / ((1 << gBits) - 1) : 0;
      int b = bBits > 0 ? ((pixel & bMask) >> bShift) * 255 / ((1 << bBits) - 1) : 0;
      int a = hasAlpha && aBits > 0
                  ? ((pixel & aMask) >> aShift) * 255 / ((1 << aBits) - 1)
                  : 255;

      dst[i * 4 + 0] = static_cast<uint8_t>(r);
      dst[i * 4 + 1] = static_cast<uint8_t>(g);
      dst[i * 4 + 2] = static_cast<uint8_t>(b);
      dst[i * 4 + 3] = static_cast<uint8_t>(a);
    }
    return result;
  };

  return info;
}

GLFormatInfo getGLFormat(const DDSPixelFormat& pf, const DDSHeaderDXT10* dxt10)
{
  // DX10 extended header takes priority
  if (dxt10) {
    return dxgiToGL(dxt10->dxgiFormat);
  }

  // FourCC compressed or float formats
  if (pf.dwFlags & DDPF_FOURCC) {
    DXGIFormat dxgi = fourCCToDXGI(pf.dwFourCC);
    if (dxgi != DXGIFormat::UNKNOWN) {
      return dxgiToGL(dxgi);
    }
    GLFormatInfo info;
    info.valid = false;
    return info;
  }

  // Uncompressed with bitmasks
  if (pf.dwFlags & (DDPF_RGB | DDPF_LUMINANCE | DDPF_YUV | DDPF_ALPHA)) {
    return buildBitmaskFormat(pf);
  }

  GLFormatInfo info;
  info.valid = false;
  return info;
}

static bool isBlockCompressed(DXGIFormat fmt)
{
  switch (fmt) {
  case DXGIFormat::BC1_UNORM:
  case DXGIFormat::BC1_UNORM_SRGB:
  case DXGIFormat::BC2_UNORM:
  case DXGIFormat::BC2_UNORM_SRGB:
  case DXGIFormat::BC3_UNORM:
  case DXGIFormat::BC3_UNORM_SRGB:
  case DXGIFormat::BC4_UNORM:
  case DXGIFormat::BC4_SNORM:
  case DXGIFormat::BC5_UNORM:
  case DXGIFormat::BC5_SNORM:
  case DXGIFormat::BC6H_UF16:
  case DXGIFormat::BC6H_SF16:
  case DXGIFormat::BC7_UNORM:
  case DXGIFormat::BC7_UNORM_SRGB:
    return true;
  default:
    return false;
  }
}

static int blockSize(DXGIFormat fmt)
{
  switch (fmt) {
  case DXGIFormat::BC1_UNORM:
  case DXGIFormat::BC1_UNORM_SRGB:
  case DXGIFormat::BC4_UNORM:
  case DXGIFormat::BC4_SNORM:
    return 8;
  default:
    return 16;
  }
}

uint32_t mipDataSize(DXGIFormat fmt, const DDSPixelFormat& pf,
                     uint32_t width, uint32_t height)
{
  if (isBlockCompressed(fmt)) {
    uint32_t blocksW = std::max(1u, (width + 3) / 4);
    uint32_t blocksH = std::max(1u, (height + 3) / 4);
    return blocksW * blocksH * blockSize(fmt);
  }

  // Uncompressed: use bits per pixel
  uint32_t bpp = pf.dwRGBBitCount;
  if (bpp == 0) {
    // Estimate from DXGI format for non-bitmask formats
    switch (fmt) {
    case DXGIFormat::R32G32B32A32_FLOAT:
      bpp = 128;
      break;
    case DXGIFormat::R32G32B32_FLOAT:
      bpp = 96;
      break;
    case DXGIFormat::R16G16B16A16_FLOAT:
    case DXGIFormat::R16G16B16A16_UNORM:
    case DXGIFormat::R16G16B16A16_SNORM:
    case DXGIFormat::R32G32_FLOAT:
      bpp = 64;
      break;
    case DXGIFormat::R8G8B8A8_UNORM:
    case DXGIFormat::R8G8B8A8_UNORM_SRGB:
    case DXGIFormat::B8G8R8A8_UNORM:
    case DXGIFormat::B8G8R8X8_UNORM:
    case DXGIFormat::R16G16_FLOAT:
    case DXGIFormat::R16G16_UNORM:
    case DXGIFormat::R32_FLOAT:
    case DXGIFormat::R10G10B10A2_UNORM:
    case DXGIFormat::R11G11B10_FLOAT:
      bpp = 32;
      break;
    case DXGIFormat::R8G8_UNORM:
    case DXGIFormat::R16_FLOAT:
    case DXGIFormat::R16_UNORM:
    case DXGIFormat::B5G6R5_UNORM:
    case DXGIFormat::B5G5R5A1_UNORM:
    case DXGIFormat::B4G4R4A4_UNORM:
      bpp = 16;
      break;
    case DXGIFormat::R8_UNORM:
    case DXGIFormat::A8_UNORM:
      bpp = 8;
      break;
    default:
      bpp = 32;
      break;
    }
  }
  return width * height * bpp / 8;
}

static const char* dxgiFormatName(DXGIFormat fmt)
{
  switch (fmt) {
  case DXGIFormat::BC1_UNORM:
    return "BC1_UNORM (DXT1)";
  case DXGIFormat::BC1_UNORM_SRGB:
    return "BC1_UNORM_SRGB";
  case DXGIFormat::BC2_UNORM:
    return "BC2_UNORM (DXT3)";
  case DXGIFormat::BC2_UNORM_SRGB:
    return "BC2_UNORM_SRGB";
  case DXGIFormat::BC3_UNORM:
    return "BC3_UNORM (DXT5)";
  case DXGIFormat::BC3_UNORM_SRGB:
    return "BC3_UNORM_SRGB";
  case DXGIFormat::BC4_UNORM:
    return "BC4_UNORM";
  case DXGIFormat::BC4_SNORM:
    return "BC4_SNORM";
  case DXGIFormat::BC5_UNORM:
    return "BC5_UNORM";
  case DXGIFormat::BC5_SNORM:
    return "BC5_SNORM";
  case DXGIFormat::BC6H_UF16:
    return "BC6H_UF16";
  case DXGIFormat::BC6H_SF16:
    return "BC6H_SF16";
  case DXGIFormat::BC7_UNORM:
    return "BC7_UNORM";
  case DXGIFormat::BC7_UNORM_SRGB:
    return "BC7_UNORM_SRGB";
  case DXGIFormat::R8G8B8A8_UNORM:
    return "R8G8B8A8_UNORM";
  case DXGIFormat::R8G8B8A8_UNORM_SRGB:
    return "R8G8B8A8_UNORM_SRGB";
  case DXGIFormat::B8G8R8A8_UNORM:
    return "B8G8R8A8_UNORM";
  case DXGIFormat::R16G16B16A16_FLOAT:
    return "R16G16B16A16_FLOAT";
  case DXGIFormat::R32G32B32A32_FLOAT:
    return "R32G32B32A32_FLOAT";
  default:
    return "Unknown";
  }
}

QString formatDescription(const DDSPixelFormat& pf, const DDSHeaderDXT10* dxt10)
{
  if (dxt10) {
    return QString("DXGI: %1").arg(dxgiFormatName(dxt10->dxgiFormat));
  }

  if (pf.dwFlags & DDPF_FOURCC) {
    char cc[5] = {0};
    std::memcpy(cc, &pf.dwFourCC, 4);
    DXGIFormat dxgi = fourCCToDXGI(pf.dwFourCC);
    if (dxgi != DXGIFormat::UNKNOWN) {
      return QString("FourCC: %1 (%2)").arg(cc).arg(dxgiFormatName(dxgi));
    }
    return QString("FourCC: %1").arg(cc);
  }

  return QString("%1-bit R:0x%2 G:0x%3 B:0x%4 A:0x%5")
      .arg(pf.dwRGBBitCount)
      .arg(pf.dwRBitMask, 0, 16)
      .arg(pf.dwGBitMask, 0, 16)
      .arg(pf.dwBBitMask, 0, 16)
      .arg(pf.dwABitMask, 0, 16);
}
