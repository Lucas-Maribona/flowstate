#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQuickView>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <cstdio>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("Flowstate");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(QCommandLineOption("probe-vulkan", "Exit after a Vulkan frame is swapped."));
    parser.process(app);
    const bool probe = parser.isSet("probe-vulkan");

    // This must happen before the first Qt Quick window is constructed.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    QQuickView view;
    std::atomic_bool vulkanReady = false;

    QObject::connect(&view, &QQuickWindow::sceneGraphError, &view,
                     [](QQuickWindow::SceneGraphError, const QString &message) {
                         std::fprintf(stderr, "Qt Quick scene graph failed: %s\n", qPrintable(message));
                         qFatal("Qt Quick scene graph failed: %s", qPrintable(message));
                     });
    QObject::connect(&view, &QQuickWindow::sceneGraphInitialized, &view,
                     [&view, &vulkanReady] {
                         if (view.rendererInterface()->graphicsApi() != QSGRendererInterface::Vulkan)
                             qFatal("Flowstate requires Qt Quick's Vulkan renderer");
                         vulkanReady.store(true, std::memory_order_release);
                         std::fputs("Qt Quick scene graph initialized with Vulkan\n", stderr);
                     }, Qt::DirectConnection);
    QObject::connect(&view, &QQuickWindow::frameSwapped, &view,
                     [&app, &vulkanReady, probe] {
                         if (!vulkanReady.load(std::memory_order_acquire))
                             qFatal("A frame swapped before Vulkan initialization was verified");
                         if (probe)
                             QMetaObject::invokeMethod(&app, "quit", Qt::QueuedConnection);
                     }, Qt::DirectConnection);

    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl("qrc:/Main.qml"));
    if (view.status() == QQuickView::Error)
        qFatal("Could not load Flowstate's QML scene");
    view.resize(640, 360);
    view.show();

    if (probe) {
        QTimer::singleShot(10000, &app, [] {
            std::fputs("No Vulkan frame was swapped within 10 seconds\n", stderr);
            qFatal("No Vulkan frame was swapped within 10 seconds");
        });
    }
    return app.exec();
}
