// codeshot — turns code into a shareable image.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <memory>

#include "backend.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("codeshot");
    app.setApplicationVersion("0.1.0");

    // Matches the window to codeshot.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("codeshot");
    app.setWindowIcon(QIcon::fromTheme("codeshot"));

    QQuickStyle::setStyle("Material");

    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;
    if (argc > 1)
        backend.openFile(QString::fromLocal8Bit(argv[1]));

    QQmlApplicationEngine engine;
    // The engine takes ownership of the provider; release() makes the
    // transfer explicit instead of a bare new.
    engine.addImageProvider("preview", std::make_unique<PreviewProvider>(&backend).release());
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
