#pragma once

#include "../core/ApplicationConfig.h"
#include "../core/NewProjectSettings.h"
#include "../core/Settings.h"
#include "DockModel.h"
#include "PanelManager.h"
#include "WorkspaceState.h"

#include <QObject>
#include <QQmlApplicationEngine>
#include <memory>

class QQuickWindow;

namespace komapaint {

// Owns the QML engine and the singletons that drive the desktop shell.
// Constructing KomaPaintApp boots configuration, registers QML types and
// loads the main QML scene. The entry point in apps/desktop/main.cpp is a
// thin wrapper around this class.

class KomaPaintApp : public QObject
{
    Q_OBJECT

public:
    // fileToOpen: optional path (or file: URL) to open once the main window
    // exists — used when the user double-clicks a .koma file. A failed open
    // surfaces through the ordinary error dialog; the empty workspace remains
    // untouched.
    explicit KomaPaintApp(const QString& fileToOpen = QString(), QObject* parent = nullptr);
    ~KomaPaintApp() override;

    int run();

    // Shared by the desktop shell and tests that load the same QML scenes.
    static void registerTypes();

private:
    void loadStyles();
    void loadUi();
    void runPackagedVerification();

    ApplicationConfig     m_config;
    Settings              m_settings;
    NewProjectSettings    m_newProject;
    DockModel             m_dock;
    PanelManager          m_panels;
    WorkspaceState        m_workspace;
    QQuickWindow*         m_window = nullptr;
    QString               m_fileToOpen;
    QStringList           m_qmlWarnings;
    // Destroy the QML windows before the C++ objects and warning sink they use.
    QQmlApplicationEngine m_engine;
};

}  // namespace komapaint
