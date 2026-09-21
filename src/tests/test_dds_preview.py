"""Exercise the actual DDS widget with PyQt6 and a desktop GL context.

Run with the bundle's Python/PyQt paths to test the shipped bindings. Headless
machines and interpreters without PyQt report a CTest skip (exit 77).
"""
import importlib.util
import os
from pathlib import Path
import struct
import sys
import types
import unittest
from unittest.mock import patch

try:
    from PyQt6 import sip
    from PyQt6.QtCore import QEventLoop, QTimer
    from PyQt6.QtGui import QSurfaceFormat
    from PyQt6.QtWidgets import QApplication, QWidget
except ImportError:
    print("SKIP: DDS preview tests require PyQt6")
    sys.exit(77)

if (os.environ.get("QT_QPA_PLATFORM") in ("offscreen", "minimal") or
        not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))):
    print("SKIP: DDS preview tests require a desktop OpenGL display")
    sys.exit(77)

source = Path(__file__).resolve().parents[2] / "libs/preview_dds/src"
sys.path.insert(0, str(source))
# Only the plugin host interfaces are stubbed; parsing, shaders, textures and
# all Qt/OpenGL behavior come from the production plugin and shipped bindings.
mobase = types.SimpleNamespace(IPluginPreview=object, IOrganizer=object)
sys.modules["mobase"] = mobase
spec = importlib.util.spec_from_file_location("preview_under_test", source / "DDSPreview.py")
preview = importlib.util.module_from_spec(spec)
preview.mobase = mobase
spec.loader.exec_module(preview)
from DDS import DDSDefinitions


def red_dds(dxgi=None, cubemap=False):
    header = [124, 0x100f, 4, 4, 16, 0, 1] + [0] * 11
    header += [32, 0x41, 0, 32, 0xff, 0xff00, 0xff0000, 0xff000000]
    header += [0x1000, 0xfe00 if cubemap else 0, 0, 0, 0]
    extra = b""
    pixels = bytes([255, 0, 0, 255]) * 16
    if dxgi is not None:
        header[19:26] = [4, int.from_bytes(b"DX10", "little"), 0, 0, 0, 0, 0]
        extra = struct.pack("<5I", dxgi.value, 3, 0, 1, 0)
        pixels = (struct.pack("<HHI", 0xf800, 0, 0) if dxgi.name.endswith("BC1_UNORM")
                  else bytes([1, 0, 0, 1]) * 16)
    dds = preview.DDSFile(b"DDS " + struct.pack("<31I", *header) + extra + pixels * (6 if cubemap else 1),
                         "red.dds")
    dds.load()
    return dds


class DDSPreviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def make_widget(self, profile=None, dds=None):
        widget = preview.DDSWidget(dds or red_dds(), preview.DDSOptions())
        self.addCleanup(lambda: self.destroy(widget))
        if profile is not None:
            fmt = widget.format()
            fmt.setVersion(4, 1)
            fmt.setProfile(profile)
            if profile == QSurfaceFormat.OpenGLContextProfile.CoreProfile:
                fmt.setOption(QSurfaceFormat.FormatOption.DeprecatedFunctions, False)
            widget.setFormat(fmt)
        widget.resize(160, 160)
        return widget

    @staticmethod
    def destroy(widget):
        if not sip.isdeleted(widget):
            widget.cleanup()
            sip.delete(widget)

    def show(self, widget):
        widget.show()
        loop = QEventLoop()
        QTimer.singleShot(50, loop.quit)
        loop.exec()
        self.assertTrue(widget.isValid(), "No usable desktop OpenGL context")

    def assert_red(self, widget):
        self.assertTrue(widget._initialized, widget.errorLabel.text())
        image = widget.grabFramebuffer()
        self.assertFalse(image.isNull())
        self.assertEqual(image.pixelColor(image.width() // 2, image.height() // 2).getRgb(),
                         (255, 0, 0, 255))

    def test_core_and_compatibility_render_and_channel_selection(self):
        for profile in (QSurfaceFormat.OpenGLContextProfile.CoreProfile,
                        QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile):
            with self.subTest(profile=profile):
                widget = self.make_widget(profile)
                self.show(widget)
                self.assertEqual(widget.context().format().profile(), profile)
                self.assert_red(widget)
                channels = preview.DDSChannelManager(preview.ColourChannels.RGBA)
                channels.setChannels(widget.ddsOptions, preview.ColourChannels.G)
                image = widget.grabFramebuffer()
                self.assertEqual(image.pixelColor(80, 80).getRgb(), (0, 0, 0, 255))
                self.destroy(widget)

    def test_integer_compressed_and_cubemap_shaders(self):
        formats = DDSDefinitions.DXGI_FORMAT
        fixtures = [red_dds(formats.DXGI_FORMAT_R8G8B8A8_UINT),
                    red_dds(formats.DXGI_FORMAT_R8G8B8A8_SINT),
                    red_dds(formats.DXGI_FORMAT_BC1_UNORM), red_dds(cubemap=True)]
        for profile in (QSurfaceFormat.OpenGLContextProfile.CoreProfile,
                        QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile):
            for dds in fixtures:
                with self.subTest(profile=profile, texture=dds.getDescription()):
                    widget = self.make_widget(profile, dds)
                    self.show(widget)
                    self.assert_red(widget)
                    self.destroy(widget)

    def test_parent_destruction_cleans_up_without_explicit_widget_cleanup(self):
        parent = QWidget()
        widget = self.make_widget()
        widget.setParent(parent)
        parent.show()
        self.show(widget)
        self.assert_red(widget)
        program = widget.program
        sip.delete(parent)
        self.assertTrue(sip.isdeleted(widget))
        self.assertTrue(sip.isdeleted(program))

    def test_early_resize_paint_and_repeated_cleanup(self):
        widget = self.make_widget()
        widget.resizeGL(0, 0)
        widget.paintGL()
        widget.cleanup()
        widget.cleanup()
        self.assertFalse(widget._initialized)

    def test_missing_functions_display_error_without_aborting(self):
        widget = self.make_widget()
        with patch.object(preview.QOpenGLVersionFunctionsFactory, "get", return_value=None):
            self.show(widget)
        self.assertFalse(widget._initialized)
        self.assertTrue(widget.errorLabel.isVisible())
        self.assertIn("functions are required", widget.errorLabel.text())
        widget.resizeGL(0, 0)
        widget.paintGL()

    def test_shader_failure_displays_error_and_cleans_partial_resources(self):
        widget = self.make_widget()
        with patch.object(preview, "fragmentShaderFloat", "#version 150\ninvalid shader;"):
            self.show(widget)
        self.assertFalse(widget._initialized)
        self.assertTrue(widget.errorLabel.isVisible())
        self.assertIsNotNone(widget.program)
        widget.cleanup()
        self.assertIsNone(widget.program)
        widget.cleanup()

    def test_unsupported_texture_displays_error(self):
        widget = self.make_widget()
        with patch.object(widget.ddsFile, "asQOpenGLTexture", return_value=None):
            self.show(widget)
        self.assertFalse(widget._initialized)
        self.assertTrue(widget.errorLabel.isVisible())
        self.assertIn("texture format", widget.errorLabel.text())

    def test_reparent_recreates_context_resources(self):
        first, second = QWidget(), QWidget()
        self.addCleanup(lambda: sip.delete(first))
        self.addCleanup(lambda: sip.delete(second))
        first.resize(180, 180)
        second.resize(180, 180)
        widget = self.make_widget()
        widget.setParent(first)
        first.show()
        self.show(widget)
        self.assert_red(widget)
        program = widget.program
        widget.setParent(second)
        second.show()
        self.show(widget)
        self.assert_red(widget)
        self.assertIsNot(widget.program, program)
        self.assertTrue(sip.isdeleted(program))


if __name__ == "__main__":
    unittest.main(verbosity=2)
