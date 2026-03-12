#ifndef DDSWIDGET_H
#define DDSWIDGET_H

#include "ddsfile.h"

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>

class DDSWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
  Q_OBJECT

public:
  explicit DDSWidget(const DDSFile& dds, QWidget* parent = nullptr);
  ~DDSWidget() override;

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;

private:
  void uploadTexture();
  void setupShaders();

  const DDSFile& m_dds;
  QOpenGLTexture* m_texture      = nullptr;
  QOpenGLShaderProgram* m_shader = nullptr;
  QOpenGLBuffer m_vbo;
  float m_aspectRatio = 1.0f;
};

#endif  // DDSWIDGET_H
