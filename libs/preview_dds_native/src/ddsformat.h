#ifndef DDSFORMAT_H
#define DDSFORMAT_H

#include <cstdint>
#include <functional>
#include <vector>

#include <QByteArray>

// DDS file magic number
constexpr uint32_t DDS_MAGIC = 0x20534444;  // "DDS "

// DDS_PIXELFORMAT flags
constexpr uint32_t DDPF_ALPHAPIXELS = 0x1;
constexpr uint32_t DDPF_ALPHA       = 0x2;
constexpr uint32_t DDPF_FOURCC      = 0x4;
constexpr uint32_t DDPF_RGB         = 0x40;
constexpr uint32_t DDPF_YUV         = 0x200;
constexpr uint32_t DDPF_LUMINANCE   = 0x20000;

// DDS_HEADER flags
constexpr uint32_t DDSD_CAPS        = 0x1;
constexpr uint32_t DDSD_HEIGHT      = 0x2;
constexpr uint32_t DDSD_WIDTH       = 0x4;
constexpr uint32_t DDSD_PITCH       = 0x8;
constexpr uint32_t DDSD_PIXELFORMAT = 0x1000;
constexpr uint32_t DDSD_MIPMAPCOUNT = 0x20000;
constexpr uint32_t DDSD_LINEARSIZE  = 0x80000;
constexpr uint32_t DDSD_DEPTH       = 0x800000;

// DDS_HEADER caps2
constexpr uint32_t DDSCAPS2_CUBEMAP           = 0x200;
constexpr uint32_t DDSCAPS2_CUBEMAP_POSITIVEX = 0x400;
constexpr uint32_t DDSCAPS2_CUBEMAP_NEGATIVEX = 0x800;
constexpr uint32_t DDSCAPS2_CUBEMAP_POSITIVEY = 0x1000;
constexpr uint32_t DDSCAPS2_CUBEMAP_NEGATIVEY = 0x2000;
constexpr uint32_t DDSCAPS2_CUBEMAP_POSITIVEZ = 0x4000;
constexpr uint32_t DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x8000;
constexpr uint32_t DDSCAPS2_VOLUME            = 0x200000;

constexpr uint32_t DDSCAPS2_CUBEMAP_ALLFACES =
    DDSCAPS2_CUBEMAP_POSITIVEX | DDSCAPS2_CUBEMAP_NEGATIVEX |
    DDSCAPS2_CUBEMAP_POSITIVEY | DDSCAPS2_CUBEMAP_NEGATIVEY |
    DDSCAPS2_CUBEMAP_POSITIVEZ | DDSCAPS2_CUBEMAP_NEGATIVEZ;

// DXGI formats (subset covering common DDS textures)
enum class DXGIFormat : uint32_t {
  UNKNOWN                    = 0,
  R32G32B32A32_FLOAT         = 2,
  R32G32B32A32_UINT          = 3,
  R32G32B32A32_SINT          = 4,
  R32G32B32_FLOAT            = 6,
  R32G32B32_UINT             = 7,
  R32G32B32_SINT             = 8,
  R16G16B16A16_FLOAT         = 10,
  R16G16B16A16_UNORM         = 11,
  R16G16B16A16_UINT          = 12,
  R16G16B16A16_SNORM         = 13,
  R16G16B16A16_SINT          = 14,
  R32G32_FLOAT               = 16,
  R32G32_UINT                = 17,
  R32G32_SINT                = 18,
  R10G10B10A2_UNORM          = 24,
  R10G10B10A2_UINT           = 25,
  R11G11B10_FLOAT            = 26,
  R8G8B8A8_UNORM             = 28,
  R8G8B8A8_UNORM_SRGB        = 29,
  R8G8B8A8_UINT              = 30,
  R8G8B8A8_SNORM             = 31,
  R8G8B8A8_SINT              = 32,
  R16G16_FLOAT               = 34,
  R16G16_UNORM               = 35,
  R16G16_UINT                = 36,
  R16G16_SNORM               = 37,
  R16G16_SINT                = 38,
  R32_FLOAT                  = 41,
  R32_UINT                   = 42,
  R32_SINT                   = 43,
  R8G8_UNORM                 = 49,
  R8G8_UINT                  = 50,
  R8G8_SNORM                 = 51,
  R8G8_SINT                  = 52,
  R16_FLOAT                  = 54,
  R16_UNORM                  = 56,
  R16_UINT                   = 57,
  R16_SNORM                  = 58,
  R16_SINT                   = 59,
  R8_UNORM                   = 61,
  R8_UINT                    = 62,
  R8_SNORM                   = 63,
  R8_SINT                    = 64,
  A8_UNORM                   = 65,
  BC1_UNORM                  = 71,
  BC1_UNORM_SRGB             = 72,
  BC2_UNORM                  = 74,
  BC2_UNORM_SRGB             = 75,
  BC3_UNORM                  = 77,
  BC3_UNORM_SRGB             = 78,
  BC4_UNORM                  = 80,
  BC4_SNORM                  = 81,
  BC5_UNORM                  = 83,
  BC5_SNORM                  = 84,
  B5G6R5_UNORM               = 85,
  B5G5R5A1_UNORM             = 86,
  B8G8R8A8_UNORM             = 87,
  B8G8R8X8_UNORM             = 88,
  B8G8R8A8_UNORM_SRGB        = 91,
  B8G8R8X8_UNORM_SRGB        = 93,
  BC6H_UF16                  = 95,
  BC6H_SF16                  = 96,
  BC7_UNORM                  = 98,
  BC7_UNORM_SRGB             = 99,
  B4G4R4A4_UNORM             = 115,
};

// D3D10 resource dimension
enum class D3D10ResourceDimension : uint32_t {
  Unknown   = 0,
  Buffer    = 1,
  Texture1D = 2,
  Texture2D = 3,
  Texture3D = 4,
};

#pragma pack(push, 1)

struct DDSPixelFormat {
  uint32_t dwSize;
  uint32_t dwFlags;
  uint32_t dwFourCC;
  uint32_t dwRGBBitCount;
  uint32_t dwRBitMask;
  uint32_t dwGBitMask;
  uint32_t dwBBitMask;
  uint32_t dwABitMask;
};

struct DDSHeader {
  uint32_t dwSize;
  uint32_t dwFlags;
  uint32_t dwHeight;
  uint32_t dwWidth;
  uint32_t dwPitchOrLinearSize;
  uint32_t dwDepth;
  uint32_t dwMipMapCount;
  uint32_t dwReserved1[11];
  DDSPixelFormat ddspf;
  uint32_t dwCaps;
  uint32_t dwCaps2;
  uint32_t dwCaps3;
  uint32_t dwCaps4;
  uint32_t dwReserved2;
};

struct DDSHeaderDXT10 {
  DXGIFormat dxgiFormat;
  D3D10ResourceDimension resourceDimension;
  uint32_t miscFlag;
  uint32_t arraySize;
  uint32_t miscFlags2;
};

#pragma pack(pop)

// Sampler type for shader selection
enum class SamplerType { Float, UInt, SInt };

// OpenGL format info
struct GLFormatInfo {
  bool valid       = false;
  bool compressed  = false;
  uint32_t internalFormat = 0;
  uint32_t format         = 0;  // only for uncompressed
  uint32_t type           = 0;  // only for uncompressed
  SamplerType sampler     = SamplerType::Float;
  // Optional converter for non-standard bitmask formats
  std::function<QByteArray(const QByteArray&, int, int)> converter;
};

// Inline helper: make a FourCC from 4 chars
constexpr uint32_t makeFourCC(char a, char b, char c, char d)
{
  return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
         (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
}

// Convert a FourCC code to DXGI format
DXGIFormat fourCCToDXGI(uint32_t fourCC);

// Resolve the OpenGL format from a DDS pixel format + optional DXT10 header
GLFormatInfo getGLFormat(const DDSPixelFormat& pf, const DDSHeaderDXT10* dxt10);

// Calculate mip level data size in bytes
uint32_t mipDataSize(DXGIFormat fmt, const DDSPixelFormat& pf,
                     uint32_t width, uint32_t height);

// Get a human-readable format description
QString formatDescription(const DDSPixelFormat& pf, const DDSHeaderDXT10* dxt10);

#endif  // DDSFORMAT_H
