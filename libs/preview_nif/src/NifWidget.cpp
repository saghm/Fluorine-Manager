#include "NifWidget.h"
#include "NifExtensions.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions_2_1>
#include <QOpenGLVersionFunctionsFactory>
using OpenGLFunctions = QOpenGLFunctions_2_1;

NifWidget::NifWidget(
    std::shared_ptr<nifly::NifFile> nifFile,
    MOBase::IOrganizer* moInfo,
    bool debugContext,
    QWidget* parent,
    Qt::WindowFlags f)
    : QOpenGLWidget(parent, f),
      m_NifFile{ nifFile },
      m_MOInfo{ moInfo },
      m_TextureManager{ std::make_unique<TextureManager>(moInfo) },
      m_ShaderManager{ std::make_unique<ShaderManager>(moInfo) }
{
    QSurfaceFormat format;
    // CompatibilityProfile — CoreProfile only exists for 3.2+, so the old
    // CoreProfile+2.1 combo was getting silently downgraded, leaving Qt
    // to pick a driver path we don't control. Compatibility 2.1 is the
    // actual legal pair for the fixed-function bits this widget uses.
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1);
    format.setDepthBufferSize(24);
    // RGBX framebuffer — no alpha channel so compositor can't see through.
    format.setAlphaBufferSize(0);

    if (debugContext) {
        format.setOption(QSurfaceFormat::DebugContext);
        m_Logger = new QOpenGLDebugLogger(this);
    }

    setFormat(format);
}

NifWidget::~NifWidget()
{
    cleanup();
}

void NifWidget::mousePressEvent(QMouseEvent* event)
{
    m_MousePos = event->globalPosition().toPoint();
}

void NifWidget::mouseMoveEvent(QMouseEvent* event)
{
    auto pos = event->globalPosition().toPoint();
    auto delta = pos - m_MousePos;
    m_MousePos = pos;

    switch (event->buttons()) {
    case Qt::LeftButton:
    {
        m_Camera->rotate(delta.x() * 0.5, delta.y() * 0.5);
    } break;
    case Qt::MiddleButton:
    {
        float viewDX = m_Camera->distance() / m_ViewportWidth;
        float viewDY = m_Camera->distance() / m_ViewportHeight;

        QMatrix4x4 r;
        r.rotate(-m_Camera->yaw(), 0.0f, 1.0f, 0.0f);
        r.rotate(-m_Camera->pitch(), 1.0f, 0.0f, 0.0f);

        auto pan = r * QVector4D(-delta.x() * viewDX, delta.y() * viewDY, 0.0f, 0.0f);

        m_Camera->pan(QVector3D(pan));
    } break;
    case Qt::RightButton:
    {
        if (event->modifiers() == Qt::ShiftModifier) {
            m_Camera->zoomDistance(delta.y() * 0.1f);
        }
    } break;
    }
}

void NifWidget::wheelEvent(QWheelEvent* event)
{
    m_Camera->zoomFactor(1.0f - (event->angleDelta().y() / 120.0f * 0.38f));
}

void NifWidget::initializeGL()
{
    if (m_Logger) {
        m_Logger->initialize();
        connect(
            m_Logger,
            &QOpenGLDebugLogger::messageLogged,
            this,
            [](const QOpenGLDebugMessage& debugMessage){
                auto msg = tr("OpenGL debug message: %1").arg(debugMessage.message());
                qDebug("%s", qUtf8Printable(msg));
            });
    }

    auto shapes = m_NifFile->GetShapes();
    for (auto& shape : shapes) {
        if (shape->flags & TriShape::Hidden) {
            continue;
        }

        m_GLShapes.emplace_back(m_NifFile.get(), shape, m_TextureManager.get());
    }

    m_Camera = SharedCamera;
    if (m_Camera.isNull()) {
        m_Camera = { new Camera(), &Camera::deleteLater };
        SharedCamera = m_Camera;

        float largestRadius = 0.0f;
        for (auto& shape : shapes) {
            auto bounds = GetBoundingSphere(m_NifFile.get(), shape);

            if (bounds.radius > largestRadius) {
                largestRadius = bounds.radius;

                m_Camera->setDistance(bounds.radius * 2.4f);
                m_Camera->setLookAt({ -bounds.center.x, bounds.center.z, bounds.center.y });
            }
        }
    }

    updateCamera();

    connect(
        m_Camera.get(),
        &Camera::cameraMoved,
        this,
        [this](){
            updateCamera();
            update();
        });

    auto f = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(
        QOpenGLContext::currentContext());

    f->glEnable(GL_DEPTH_TEST);
    f->glDepthFunc(GL_LEQUAL);
    f->glClearColor(0.18, 0.18, 0.18, 1.0);

    // Persistent polygon offset state — actual per-shape bias is set in
    // paintGL() by draw order so coplanar overlays tie-break deterministically.
    f->glEnable(GL_POLYGON_OFFSET_FILL);
}

void NifWidget::paintGL()
{
    auto f = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(
        QOpenGLContext::currentContext());

    // Force the framebuffer to be fully opaque. Sequence:
    //   1. Unmask alpha + clear → writes RGB=dark grey, A=1.0
    //   2. Mask alpha off → subsequent shape draws can't touch FB alpha
    // Without this, alpha-blended shapes mutate FB alpha and Qt composites
    // the dialog background through the preview area (see-through bug).
    f->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    f->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

    for (auto& shape : m_GLShapes) {
        // Small polygon offset only on decal-flagged shapes to break z-ties
        // with their coincident base mesh. Progressive/large offsets push
        // depth out of the valid [0,1] range at far camera distances,
        // which culls decals (the moss/road at zoom-out bug).
        if (shape.isDecal) {
            f->glPolygonOffset(-1.0f, -1.0f);
        } else {
            f->glPolygonOffset(0.0f, 0.0f);
        }

        auto program = m_ShaderManager->getProgram(shape.shaderType);
        if (program && program->isLinked() && program->bind()) {
            auto binder = QOpenGLVertexArrayObject::Binder(shape.vertexArray);

            auto& modelMatrix = shape.modelMatrix;
            auto modelViewMatrix = m_ViewMatrix * modelMatrix;
            auto mvpMatrix = m_ProjectionMatrix * modelViewMatrix;

            program->setUniformValue("worldMatrix", modelMatrix);
            program->setUniformValue("viewMatrix", m_ViewMatrix);
            program->setUniformValue("modelViewMatrix", modelViewMatrix);
            program->setUniformValue("modelViewMatrixInverse", modelViewMatrix.inverted());
            program->setUniformValue("normalMatrix", modelViewMatrix.normalMatrix());
            program->setUniformValue("mvpMatrix", mvpMatrix);
            program->setUniformValue("lightDirection", QVector3D(0, 0, 1));

            shape.setupShaders(program);

            if (shape.indexBuffer && shape.indexBuffer->isCreated()) {
                shape.indexBuffer->bind();
                f->glDrawElements(GL_TRIANGLES, shape.elements, GL_UNSIGNED_SHORT, nullptr);
                shape.indexBuffer->release();
            }

            program->release();
        }
    }
}

void NifWidget::resizeGL(int w, int h)
{
    QMatrix4x4 m;
    m.perspective(40.0f, static_cast<float>(w) / h, 0.1f, 10000.0f);

    m_ProjectionMatrix = m;
    m_ViewportWidth = w;
    m_ViewportHeight = h;
}

void NifWidget::cleanup()
{
    // Must run from ~NifWidget (not from the QOpenGLContext::aboutToBeDestroyed
    // signal), because the signal fires after derived-class members have
    // already been torn down — iterating m_GLShapes from a signal slot then
    // walks freed memory. Keep cleanup synchronous in the dtor.
    if (!context()) {
        return;
    }

    makeCurrent();

    for (auto& shape : m_GLShapes) {
        shape.destroy();
    }
    m_GLShapes.clear();

    m_TextureManager->cleanup();
}

void NifWidget::updateCamera()
{
    QMatrix4x4 m;
    m.translate(0.0f, 0.0f, -m_Camera->distance());
    m.rotate(m_Camera->pitch(), 1.0f, 0.0f, 0.0f);
    m.rotate(m_Camera->yaw(), 0.0f, 1.0f, 0.0f);
    m.translate(-m_Camera->lookAt());
    m *= QMatrix4x4{
        -1, 0, 0, 0,
         0, 0, 1, 0,
         0, 1, 0, 0,
         0, 0, 0, 1,
    };
    m_ViewMatrix = m;
}
