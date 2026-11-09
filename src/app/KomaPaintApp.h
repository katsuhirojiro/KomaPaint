#pragma once

#include "../core/ApplicationConfig.h"
#include "../core/Settings.h"
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
    // surfaces through the ordinary error dialog; the untitled document is
    // left in place and untouched.
    explicit KomaPaintApp(const QString& fileToOpen = QString(), QObject* parent = nullptr);
    ~KomaPaintApp() override;

    int run();

private:
    void registerTypes();
    void loadStyles();
    void loadUi();

    QQmlApplicationEngine m_engine;
    ApplicationConfig     m_config;
    Settings              m_settings;
    PanelManager          m_panels;
    WorkspaceState        m_workspace;
    QQuickWindow*         m_window = nullptr;
    QString               m_fileToOpen;
};

}  // namespace komapaint
