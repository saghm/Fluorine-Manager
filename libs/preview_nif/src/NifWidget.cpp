#include "NifWidget.h"
#include "NifExtensions.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions_2_1>
#include <QOpenGLVersionFunctionsFactory>
#include <QLabel>
#include <QVBoxLayout>
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
    // The shaders and fixed-function state require desktop compatibility GL.
    // Drivers can return a different context, so validate it in initializeGL.
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::NoProfile);
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
    auto* layout = new QVBoxLayout(this);
    m_ErrorLabel = new QLabel(this);
    m_ErrorLabel->setObjectName("nifPreviewError");
    m_ErrorLabel->setTextFormat(Qt::PlainText);
    m_ErrorLabel->setWordWrap(true);
    m_ErrorLabel->setAlignment(Qt::AlignCenter);
    m_ErrorLabel->hide();
    layout->addWidget(m_ErrorLabel);
}

NifWidget::~NifWidget()
{
    disconnect(m_ContextCleanup);
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

    if (!m_Initialized || m_Camera.isNull()) {
        event->ignore();
        return;
    }

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
    if (!m_Initialized || m_Camera.isNull()) {
        event->ignore();
        return;
    }
    m_Camera->zoomFactor(1.0f - (event->angleDelta().y() / 120.0f * 0.38f));
}

void NifWidget::initializeGL()
{
    m_Initialized = false;
    m_ErrorLabel->hide();
    auto* f = QOpenGLVersionFunctionsFactory::get<OpenGLFunctions>(context());
    if (!f || !f->initializeOpenGLFunctions()) {
        showError(tr("NIF preview requires desktop OpenGL 2.1 compatibility functions. "
                     "The current graphics context does not support them."));
        return;
    }
    disconnect(m_ContextCleanup);
    m_ContextCleanup = connect(context(), &QOpenGLContext::aboutToBeDestroyed,
                               this, &NifWidget::cleanup);
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

    disconnect(m_CameraConnection);
    m_CameraConnection = connect(
        m_Camera.get(),
        &Camera::cameraMoved,
        this,
        [this](){
            updateCamera();
            update();
        });

    f->glEnable(GL_DEPTH_TEST);
    f->glDepthFunc(GL_LEQUAL);
    f->glClearColor(0.18, 0.18, 0.18, 1.0);

    // Persistent polygon offset state — actual per-shape bias is set in
    // paintGL() by draw order so coplanar overlays tie-break deterministically.
    f->glEnable(GL_POLYGON_OFFSET_FILL);
    m_Initialized = true;
}

void NifWidget::paintGL()
{
    if (!m_Initialized) return;
    auto f = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(
        QOpenGLContext::currentContext());
    if (!f) {
        showError(tr("The graphics context for NIF preview is no longer available."));
        return;
    }

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
        } else {
            showError(tr("Could not initialize the NIF preview shaders. See the log for details."));
            return;
        }
    }
}

void NifWidget::resizeGL(int w, int h)
{
    w = qMax(w, 1);
    h = qMax(h, 1);
    QMatrix4x4 m;
    m.perspective(40.0f, static_cast<float>(w) / h, 0.1f, 10000.0f);

    m_ProjectionMatrix = m;
    m_ViewportWidth = w;
    m_ViewportHeight = h;
}

void NifWidget::cleanup()
{
    // Also handle context replacement on reparenting. The destructor explicitly
    // disconnects first, so Qt cannot call back into destroyed derived members.
    disconnect(m_ContextCleanup);
    disconnect(m_CameraConnection);
    m_Initialized = false;
    if (context()) makeCurrent();

    for (auto& shape : m_GLShapes) {
        shape.destroy();
    }
    m_GLShapes.clear();

    m_TextureManager->cleanup();
    m_ShaderManager->cleanup();
    if (context()) doneCurrent();
}

void NifWidget::showError(const QString& message)
{
    m_Initialized = false;
    qWarning("%s", qUtf8Printable(message));
    m_ErrorLabel->setText(message);
    m_ErrorLabel->show();
}

void NifWidget::updateCamera()
{
    if (m_Camera.isNull()) return;
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
