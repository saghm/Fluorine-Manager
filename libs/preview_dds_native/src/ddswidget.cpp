#include "ddswidget.h"

#include <QDebug>

#include <algorithm>
#include <cstring>

// Vertex data: position (x,y) + texcoord (u,v)
static const float quadVertices[] = {
    // pos      // tex
    -1.0f, -1.0f, 0.0f, 1.0f,
    1.0f,  -1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f,  0.0f, 0.0f,
    1.0f,  1.0f,  1.0f, 0.0f,
};

static const char* vertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
uniform float uAspect;
uniform float uWidgetAspect;
void main() {
    vec2 pos = aPos;
    float ratio = uAspect / uWidgetAspect;
    if (ratio > 1.0)
        pos.y *= 1.0 / ratio;
    else
        pos.x *= ratio;
    gl_Position = vec4(pos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char* fragmentShaderSrc = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D uTexture;
void main() {
    vec4 texel = texture(uTexture, vTexCoord);
    // Checkerboard for transparency
    vec2 checker = floor(vTexCoord * 16.0);
    float c = mod(checker.x + checker.y, 2.0);
    vec3 bg = mix(vec3(0.6), vec3(0.4), c);
    fragColor = vec4(mix(bg, texel.rgb, texel.a), 1.0);
}
)";

static const char* cubemapFragSrc = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 fragColor;
uniform samplerCube uTexture;
void main() {
    // Spherical projection for cubemap preview
    float theta = vTexCoord.x * 6.28318530718;
    float phi = vTexCoord.y * 3.14159265359;
    vec3 dir = vec3(sin(phi) * cos(theta), cos(phi), sin(phi) * sin(theta));
    vec4 texel = texture(uTexture, dir);
    vec2 checker = floor(vTexCoord * 16.0);
    float c = mod(checker.x + checker.y, 2.0);
    vec3 bg = mix(vec3(0.6), vec3(0.4), c);
    fragColor = vec4(mix(bg, texel.rgb, texel.a), 1.0);
}
)";

DDSWidget::DDSWidget(const DDSFile& dds, QWidget* parent)
    : QOpenGLWidget(parent), m_dds(dds), m_vbo(QOpenGLBuffer::VertexBuffer)
{
  if (dds.height() > 0) {
    m_aspectRatio =
        static_cast<float>(dds.width()) / static_cast<float>(dds.height());
  }
}

DDSWidget::~DDSWidget()
{
  makeCurrent();
  delete m_texture;
  delete m_shader;
  m_vbo.destroy();
  doneCurrent();
}

void DDSWidget::initializeGL()
{
  initializeOpenGLFunctions();

  glClearColor(0.2f, 0.2f, 0.2f, 1.0f);

  // VBO
  m_vbo.create();
  m_vbo.bind();
  m_vbo.allocate(quadVertices, sizeof(quadVertices));

  setupShaders();
  uploadTexture();
}

void DDSWidget::setupShaders()
{
  m_shader = new QOpenGLShaderProgram(this);
  m_shader->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSrc);

  if (m_dds.isCubemap()) {
    m_shader->addShaderFromSourceCode(QOpenGLShader::Fragment, cubemapFragSrc);
  } else {
    m_shader->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSrc);
  }

  m_shader->link();
}

void DDSWidget::uploadTexture()
{
  if (m_dds.faceCount() == 0 || m_dds.mipCount() == 0)
    return;

  const auto& fmt = m_dds.glFormat();

  if (m_dds.isCubemap()) {
    m_texture = new QOpenGLTexture(QOpenGLTexture::TargetCubeMap);
  } else {
    m_texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
  }

  m_texture->setAutoMipMapGenerationEnabled(false);
  m_texture->setMipLevels(m_dds.mipCount());

  if (m_dds.isCubemap()) {
    m_texture->setSize(m_dds.width(), m_dds.height());
    // Set format for allocation
    if (fmt.compressed) {
      m_texture->setFormat(
          static_cast<QOpenGLTexture::TextureFormat>(fmt.internalFormat));
    } else {
      m_texture->setFormat(
          static_cast<QOpenGLTexture::TextureFormat>(fmt.internalFormat));
    }
    m_texture->allocateStorage();

    static const QOpenGLTexture::CubeMapFace cubeMapFaces[] = {
        QOpenGLTexture::CubeMapPositiveX, QOpenGLTexture::CubeMapNegativeX,
        QOpenGLTexture::CubeMapPositiveY, QOpenGLTexture::CubeMapNegativeY,
        QOpenGLTexture::CubeMapPositiveZ, QOpenGLTexture::CubeMapNegativeZ,
    };

    int numFaces = std::min(m_dds.faceCount(), 6);
    for (int f = 0; f < numFaces; ++f) {
      const auto& face = m_dds.face(f);
      for (int m = 0; m < face.mips.size(); ++m) {
        const auto& mip = face.mips[m];
        QByteArray pixelData = mip.data;
        if (fmt.converter) {
          pixelData = fmt.converter(mip.data, mip.width, mip.height);
        }
        if (fmt.compressed) {
          m_texture->setCompressedData(
              m, 0, cubeMapFaces[f],
              pixelData.size(),
              pixelData.constData());
        } else {
          m_texture->setData(
              m, 0, cubeMapFaces[f],
              static_cast<QOpenGLTexture::PixelFormat>(fmt.format),
              static_cast<QOpenGLTexture::PixelType>(fmt.type),
              pixelData.constData());
        }
      }
    }
  } else {
    m_texture->setSize(m_dds.width(), m_dds.height());
    if (fmt.compressed) {
      m_texture->setFormat(
          static_cast<QOpenGLTexture::TextureFormat>(fmt.internalFormat));
    } else {
      m_texture->setFormat(
          static_cast<QOpenGLTexture::TextureFormat>(fmt.internalFormat));
    }
    m_texture->allocateStorage();

    const auto& face = m_dds.face(0);
    for (int m = 0; m < face.mips.size(); ++m) {
      const auto& mip = face.mips[m];
      QByteArray pixelData = mip.data;
      if (fmt.converter) {
        pixelData = fmt.converter(mip.data, mip.width, mip.height);
      }
      if (fmt.compressed) {
        m_texture->setCompressedData(
            m, 0,
            pixelData.size(),
            pixelData.constData());
      } else {
        m_texture->setData(
            m, 0,
            static_cast<QOpenGLTexture::PixelFormat>(fmt.format),
            static_cast<QOpenGLTexture::PixelType>(fmt.type),
            pixelData.constData());
      }
    }
  }

  m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
  if (fmt.sampler != SamplerType::Float) {
    m_texture->setMinMagFilters(QOpenGLTexture::Nearest,
                                QOpenGLTexture::Nearest);
  } else {
    m_texture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear,
                                QOpenGLTexture::Linear);
  }
}

void DDSWidget::resizeGL(int w, int h)
{
  glViewport(0, 0, w, h);
}

void DDSWidget::paintGL()
{
  glClear(GL_COLOR_BUFFER_BIT);

  if (!m_texture || !m_shader)
    return;

  m_shader->bind();
  m_texture->bind();

  float widgetAspect = width() > 0 && height() > 0
                           ? static_cast<float>(width()) / height()
                           : 1.0f;
  m_shader->setUniformValue("uAspect", m_aspectRatio);
  m_shader->setUniformValue("uWidgetAspect", widgetAspect);
  m_shader->setUniformValue("uTexture", 0);

  m_vbo.bind();
  m_shader->enableAttributeArray(0);
  m_shader->enableAttributeArray(1);
  m_shader->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
  m_shader->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2,
                               4 * sizeof(float));

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  m_shader->disableAttributeArray(0);
  m_shader->disableAttributeArray(1);
  m_vbo.release();
  m_texture->release();
  m_shader->release();
}
