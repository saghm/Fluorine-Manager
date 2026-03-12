#include "pythonrunner.h"

#ifdef _WIN32
#pragma warning(disable : 4100)
#pragma warning(disable : 4996)

#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <optional>

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include "pybind11_qt/pybind11_qt.h"
#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/stl/filesystem.h>

#include <uibase/log.h>
#include <uibase/utility.h>

#include "error.h"
#include "pythonutils.h"

using namespace MOBase;
namespace py = pybind11;

namespace mo2::python {

    /**
     *
     */
    class PythonRunner : public IPythonRunner {

    public:
        PythonRunner()  = default;
        ~PythonRunner() = default;

        QList<QObject*> load(const QString& identifier) override;
        void unload(const QString& identifier) override;

        bool initialize(std::vector<std::filesystem::path> const& pythonPaths) override;
        void addDllSearchPath(std::filesystem::path const& dllPath) override;
        bool isInitialized() const override;

    private:
        /**
         * @brief Ensure that the given folder is in sys.path.
         */
        void ensureFolderInPath(QString folder);

    private:
        // for each "identifier" (python file or python module folder), contains the
        // list of python objects - this does not keep the objects alive, it simply used
        // to unload plugins
        std::unordered_map<QString, std::vector<py::handle>> m_PythonObjects;
    };

    std::unique_ptr<IPythonRunner> createPythonRunner()
    {
        return std::make_unique<PythonRunner>();
    }

    bool PythonRunner::initialize(std::vector<std::filesystem::path> const& pythonPaths)
    {
        // we only initialize Python once for the whole lifetime of the program, even if
        // MO2 is restarted and the proxy or PythonRunner objects are deleted and
        // recreated, Python is not re-initialized
        //
        // in an ideal world, we would initialize Python here (or in the constructor)
        // and then finalize it in the destructor
        //
        // unfortunately, many library, including PyQt6, do not handle properly
        // re-initializing the Python interpreter, so we cannot do that and we keep the
        // interpreter alive
        //

        if (Py_IsInitialized()) {
            return true;
        }

        try {
            static const char* argv0 = "ModOrganizer.exe";

#ifndef _WIN32
            // Ensure libpython symbols are globally visible for extension modules
            // loaded later (_struct, PyQt6, etc.).
            //
            // We must promote the *already-loaded* libpython to RTLD_GLOBAL.
            // Using the compile-time filename (e.g. "libpython3.13.so.1.0") with
            // RTLD_NOLOAD can fail when the portable Python's SONAME differs
            // (e.g. "libpython3.13.so"), causing a second copy to be loaded and
            // making Py_IsInitialized() return false after Py_InitializeFromConfig().
            //
            // Instead, find the DSO that provides Py_IsInitialized via dladdr, then
            // re-dlopen that exact path with RTLD_GLOBAL.
            {
                Dl_info di;
                void* sym = dlsym(RTLD_DEFAULT, "Py_IsInitialized");
                if (sym && dladdr(sym, &di) && di.dli_fname) {
                    void* pyHandle =
                        dlopen(di.dli_fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
                    if (pyHandle) {
                        MOBase::log::debug(
                            "python: promoted '{}' to RTLD_GLOBAL via dladdr",
                            di.dli_fname);
                    } else {
                        // Fallback: load by full path (not NOLOAD).
                        pyHandle = dlopen(di.dli_fname, RTLD_NOW | RTLD_GLOBAL);
                        if (pyHandle) {
                            MOBase::log::debug(
                                "python: loaded '{}' with RTLD_GLOBAL (fresh)",
                                di.dli_fname);
                        } else {
                            MOBase::log::warn(
                                "python: failed to promote '{}' to RTLD_GLOBAL: {}",
                                di.dli_fname, dlerror());
                        }
                    }
                } else {
                    // Py_IsInitialized not yet in scope — libpython may not be loaded
                    // as a dependency yet.  Try the compile-time name.
#ifdef MO2_PYTHON_SHARED_LIBRARY
                    void* pyHandle =
                        dlopen(MO2_PYTHON_SHARED_LIBRARY, RTLD_NOW | RTLD_GLOBAL);
                    if (pyHandle) {
                        MOBase::log::debug(
                            "python: loaded '{}' with RTLD_GLOBAL (compile-time name)",
                            MO2_PYTHON_SHARED_LIBRARY);
                    } else {
                        MOBase::log::warn(
                            "python: failed to dlopen '{}': {}",
                            MO2_PYTHON_SHARED_LIBRARY, dlerror());
                    }
#else
                    MOBase::log::warn(
                        "python: Py_IsInitialized not found in global scope and "
                        "no compile-time library name available");
#endif
                }
            }
#endif

            // Determine Python home directory.
            // Priority: 1) venv at ~/.local/share/fluorine/python-venv/
            //           2) MO2_PYTHON_DIR env var (legacy bundled Python)
            //           3) <exe_dir>/python (legacy bundled Python)
            //           4) system Python (no PYTHONHOME needed)
            QString pythonHome;
            QString venvPath;
            bool usingVenv = false;

            // Check for user-created venv
            {
                QString dataDir = QDir::homePath() + "/.local/share/fluorine";
                QString venvDir = dataDir + "/python-venv";
                if (QDir(venvDir).exists() && QFile::exists(venvDir + "/bin/python3")) {
                    venvPath = venvDir;
                    // Read pyvenv.cfg to find the base Python prefix
                    QFile cfg(venvDir + "/pyvenv.cfg");
                    if (cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        while (!cfg.atEnd()) {
                            QString line = QString::fromUtf8(cfg.readLine()).trimmed();
                            if (line.startsWith("home")) {
                                // "home = /usr/bin" → base prefix is parent of bin dir
                                int eq = line.indexOf('=');
                                if (eq >= 0) {
                                    QString binDir = line.mid(eq + 1).trimmed();
                                    QDir parent(binDir);
                                    parent.cdUp();
                                    pythonHome = parent.absolutePath();
                                    usingVenv = true;
                                    MOBase::log::info("python: using venv at '{}', base prefix '{}'",
                                                      venvPath, pythonHome);
                                }
                                break;
                            }
                        }
                    }
                }
            }

            // Fallback: legacy bundled Python
            if (pythonHome.isEmpty()) {
                const char* envPy = std::getenv("MO2_PYTHON_DIR");
                if (envPy && envPy[0] != '\0') {
                    pythonHome = QString::fromUtf8(envPy);
                } else {
                    QString bundled = QCoreApplication::applicationDirPath() + "/python";
                    if (QDir(bundled).exists()) {
                        pythonHome = bundled;
                    }
                }
            }

            // If no bundled Python, use system Python (don't set PYTHONHOME)
            if (!pythonHome.isEmpty() && !QDir(pythonHome).exists()) {
                MOBase::log::warn("python: PYTHONHOME dir '{}' does not exist, "
                                  "falling back to system Python", pythonHome);
                pythonHome.clear();
            }

            std::optional<QByteArray> oldPythonHome;
            std::optional<QByteArray> oldPythonPath;
            auto restorePythonEnv = [&]() {
                if (oldPythonHome.has_value()) {
                    setenv("PYTHONHOME", oldPythonHome->constData(), 1);
                } else {
                    unsetenv("PYTHONHOME");
                }
                if (oldPythonPath.has_value()) {
                    setenv("PYTHONPATH", oldPythonPath->constData(), 1);
                } else {
                    unsetenv("PYTHONPATH");
                }
            };
            if (const char* v = std::getenv("PYTHONHOME"); v != nullptr) {
                oldPythonHome = QByteArray(v);
            }
            if (const char* v = std::getenv("PYTHONPATH"); v != nullptr) {
                oldPythonPath = QByteArray(v);
            }

            // Paths we want to prepend/append for MO2 plugin loading.
            auto paths = pythonPaths;

            // Build PYTHONPATH and optionally set PYTHONHOME.
            QStringList corePaths;

            if (!pythonHome.isEmpty()) {
                // Bundled or system Python with known prefix.
                const QDir libDir(pythonHome + "/lib");
                const auto pyDirs =
                    libDir.entryList({"python3.*"}, QDir::Dirs | QDir::NoDotAndDotDot);
                const QString pyverDir = pyDirs.isEmpty() ? QStringLiteral("python3.13")
                                                          : pyDirs.first();
                const QString stdlibDir = pythonHome + "/lib/" + pyverDir;
                const QString dynloadDir = stdlibDir + "/lib-dynload";
                const QString siteDir = stdlibDir + "/site-packages";

                corePaths = {stdlibDir, siteDir, dynloadDir};

                const QString stdlibZip = pythonHome + "/lib/python313.zip";
                if (QFile::exists(stdlibZip)) {
                    corePaths.prepend(stdlibZip);
                }
                const QString rootDynloadDir = pythonHome + "/lib-dynload";
                if (QDir(rootDynloadDir).exists()) {
                    corePaths.append(rootDynloadDir);
                }

                corePaths.append(pythonHome);
                setenv("PYTHONHOME", pythonHome.toUtf8().constData(), 1);
            }

            // If using a venv, prepend its site-packages so venv packages
            // (PyQt6, etc.) take priority over system site-packages.
            if (usingVenv && !venvPath.isEmpty()) {
                const QDir venvLibDir(venvPath + "/lib");
                const auto venvPyDirs =
                    venvLibDir.entryList({"python3.*"}, QDir::Dirs | QDir::NoDotAndDotDot);
                if (!venvPyDirs.isEmpty()) {
                    QString venvSite = venvPath + "/lib/" + venvPyDirs.first() + "/site-packages";
                    if (QDir(venvSite).exists()) {
                        corePaths.prepend(venvSite);
                    }
                }
            }

            if (!corePaths.isEmpty()) {
                setenv("PYTHONPATH", corePaths.join(":").toUtf8().constData(), 1);
            }

            MOBase::log::debug(
                "python: calling Py_InitializeFromConfig, PYTHONHOME='{}', "
                "venv='{}', Py_IsInitialized before={}",
                pythonHome.isEmpty() ? "(system)" : pythonHome,
                venvPath.isEmpty() ? "(none)" : venvPath,
                Py_IsInitialized());

            // Use Py_InitializeFromConfig (Python 3.8+) for explicit error reporting.
            {
                PyConfig config;
                PyConfig_InitPythonConfig(&config);
                if (!pythonHome.isEmpty()) {
                    // Set config.home directly (more reliable than env for embedded use).
                    std::wstring wHome = pythonHome.toStdWString();
                    PyStatus status = PyConfig_SetString(&config, &config.home, wHome.c_str());
                    if (PyStatus_Exception(status)) {
                        MOBase::log::error(
                            "python: PyConfig_SetString(home) failed: '{}'",
                            status.err_msg ? status.err_msg : "(no message)");
                        PyConfig_Clear(&config);
                        restorePythonEnv();
                        return false;
                    }
                }
                PyStatus status = Py_InitializeFromConfig(&config);
                PyConfig_Clear(&config);
                if (PyStatus_Exception(status)) {
                    MOBase::log::error(
                        "python: Py_InitializeFromConfig failed: '{}' [in '{}']",
                        status.err_msg ? status.err_msg : "(no message)",
                        status.func ? status.func : "(no func)");
                    restorePythonEnv();
                    return false;
                }
            }

            MOBase::log::debug("python: Py_IsInitialized after={}",
                               Py_IsInitialized());

            if (!Py_IsInitialized()) {
                MOBase::log::error(
                    "failed to init python: Py_IsInitialized() returned false.");
                restorePythonEnv();
                return false;
            }

            {
                for (auto const& path : paths) {
                    ensureFolderInPath(QString::fromStdString(absolute(path).string()));
                }

                py::module_ mainModule   = py::module_::import("__main__");
                py::object mainNamespace = mainModule.attr("__dict__");
                mainNamespace["sys"]     = py::module_::import("sys");
                mainNamespace["mobase"]  = py::module_::import("mobase");

                mo2::python::configure_python_stream();
                mo2::python::configure_python_logging(mainNamespace["mobase"]);
            }

            // we need to release the GIL here - which is what this does
            //
            // when Python is initialized, the GIl is acquired, and if it is not
            // release, trying to acquire it on a different thread will deadlock
            PyEval_SaveThread();
            restorePythonEnv();

            return true;
        }
        catch (const py::error_already_set& ex) {
            MOBase::log::error("failed to init python: {}", ex.what());
            return false;
        }
    }

    void PythonRunner::addDllSearchPath(std::filesystem::path const& dllPath)
    {
        py::gil_scoped_acquire lock;
#ifdef _WIN32
        py::module_::import("os").attr("add_dll_directory")(absolute(dllPath));
#else
        // On Linux, there is no add_dll_directory equivalent; prepend the folder to
        // sys.path so Python extension modules can be found.
        ensureFolderInPath(QString::fromStdString(absolute(dllPath).string()));
#endif
    }

    void PythonRunner::ensureFolderInPath(QString folder)
    {
        py::module_ sys  = py::module_::import("sys");
        py::list sysPath = sys.attr("path");

        // Converting to QStringList for Qt::CaseInsensitive and because .index()
        // raise an exception:
        const QStringList currentPath = sysPath.cast<QStringList>();
        if (!currentPath.contains(folder, Qt::CaseInsensitive)) {
            sysPath.insert(0, folder);
        }
    }

    QList<QObject*> PythonRunner::load(const QString& identifier)
    {
        py::gil_scoped_acquire lock;

        const QFileInfo idInfo(identifier);
        const QString baseName = idInfo.fileName();
        if (baseName == "winreg.py" || baseName == "lzokay.py") {
            log::debug("Skipping Python compatibility shim '{}'.", identifier);
            return {};
        }

        // `pluginName` can either be a python file (single-file plugin or a folder
        // (whole module).
        //
        // For whole module, we simply add the parent folder to path, then we load
        // the module with a simple py::import, and we retrieve the associated
        // __dict__ from which we extract either createPlugin or createPlugins.
        //
        // For single file, we need to use py::eval_file, and we will use the
        // context (global variables) from __main__ (already contains mobase, and
        // other required module). Since the context is shared between called of
        // `instantiate`, we need to make sure to remove createPlugin(s) from
        // previous call.
        try {

            // dictionary that will contain createPlugin() or createPlugins().
            py::dict moduleDict;

            if (identifier.endsWith(".py")) {
                py::object mainModule = py::module_::import("__main__");

                // make a copy, otherwise we might end up calling the createPlugin() or
                // createPlugins() function multiple time
                py::dict moduleNamespace = mainModule.attr("__dict__").attr("copy")();

                std::string temp = ToString(identifier);
                py::eval_file(temp, moduleNamespace).is_none();
                moduleDict = moduleNamespace;
            }
            else {
                // Retrieve the module name:
                QStringList parts      = identifier.split("/");
                std::string moduleName = ToString(parts.takeLast());
                ensureFolderInPath(parts.join("/"));

                // check if the module is already loaded
                py::dict modules = py::module_::import("sys").attr("modules");
                if (modules.contains(moduleName)) {
                    py::module_ prev = modules[py::str(moduleName)];
                    py::module_(prev).reload();
                    moduleDict = prev.attr("__dict__");
                }
                else {
                    moduleDict =
                        py::module_::import(moduleName.c_str()).attr("__dict__");
                }
            }

            if (py::len(moduleDict) == 0) {
                MOBase::log::error("No plugins found in {}.", identifier);
                return {};
            }

            // Create the plugins:
            std::vector<py::object> plugins;

            if (moduleDict.contains("createPlugin")) {
                plugins.push_back(moduleDict["createPlugin"]());
            }
            else if (moduleDict.contains("createPlugins")) {
                py::object pyPlugins = moduleDict["createPlugins"]();
                if (!py::isinstance<py::sequence>(pyPlugins)) {
                    MOBase::log::error(
                        "Plugin {}: createPlugins must return a sequence.", identifier);
                }
                else {
                    py::sequence pyList(pyPlugins);
                    size_t nPlugins = pyList.size();
                    for (size_t i = 0; i < nPlugins; ++i) {
                        plugins.push_back(pyList[i]);
                    }
                }
            }
            else {
                MOBase::log::error("Plugin {}: missing a createPlugin(s) function.",
                                   identifier);
            }

            // If we have no plugins, there was an issue, and we already logged the
            // problem:
            if (plugins.empty()) {
                return QList<QObject*>();
            }

            QList<QObject*> allInterfaceList;

            for (py::object pluginObj : plugins) {

                // save to be able to unload it
                m_PythonObjects[identifier].push_back(pluginObj);

                QList<QObject*> interfaceList = py::module_::import("mobase.private")
                                                    .attr("extract_plugins")(pluginObj)
                                                    .cast<QList<QObject*>>();

                if (interfaceList.isEmpty()) {
                    MOBase::log::error("Plugin {}: no plugin interface implemented.",
                                       identifier);
                }

                // Append the plugins to the main list:
                allInterfaceList.append(interfaceList);
            }

            return allInterfaceList;
        }
        catch (const py::error_already_set& ex) {
            MOBase::log::error("Failed to import plugin from {}: {}", identifier,
                               ex.what());
            throw pyexcept::PythonError(ex);
        }
    }

    void PythonRunner::unload(const QString& identifier)
    {
        auto it = m_PythonObjects.find(identifier);
        if (it != m_PythonObjects.end()) {

            py::gil_scoped_acquire lock;

            if (!identifier.endsWith(".py")) {

                // At this point, the identifier is the full path to the module.
                QDir folder(identifier);

                // We want to "unload" (remove from sys.modules) modules that come
                // from this plugin (whose __path__ points under this module,
                // including the module of the plugin itself).
                py::object sys   = py::module_::import("sys");
                py::dict modules = sys.attr("modules");
                py::list keys    = modules.attr("keys")();
                for (std::size_t i = 0; i < py::len(keys); ++i) {
                    py::object mod = modules[keys[i]];
                    if (PyObject_HasAttrString(mod.ptr(), "__path__")) {
                        QString mpath =
                            mod.attr("__path__")[py::int_(0)].cast<QString>();

                        if (!folder.relativeFilePath(mpath).startsWith("..")) {
                            // If the path is under identifier, we need to unload
                            // it.
                            log::debug("Unloading module {} from {} for {}.",
                                       keys[i].cast<std::string>(), mpath, identifier);

                            PyDict_DelItem(modules.ptr(), keys[i].ptr());
                        }
                    }
                }
            }

            // Boost.Python does not handle cyclic garbace collection, so we need to
            // release everything hold by the objects before deleting the objects
            // themselves (done when erasing from m_PythonObjects).
            for (auto& obj : it->second) {
                obj.attr("__dict__").attr("clear")();
            }

            log::debug("Deleting {} python objects for {}.", it->second.size(),
                       identifier);
            m_PythonObjects.erase(it);
        }
    }

    bool PythonRunner::isInitialized() const
    {
        return Py_IsInitialized() != 0;
    }

}  // namespace mo2::python
