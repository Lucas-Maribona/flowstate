#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMetaObject>
#include <QQuickView>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <cstdio>

class KeyObserver final : public QObject {
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress)
            std::fprintf(stderr, "Qt window received key=%d\n",
                         static_cast<QKeyEvent *>(event)->key());
        return false;
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("Flowstate");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(QCommandLineOption("probe-vulkan", "Exit after a Vulkan frame is swapped."));
    parser.addOption(QCommandLineOption("probe-visual", "Verify actual QML pixels after a Vulkan frame and exit."));
    parser.addOption(QCommandLineOption("visual-report", "Verify QML pixels and save a PNG, then keep the window open.", "path"));
    parser.process(app);
    const bool probeVulkan = parser.isSet("probe-vulkan");
    const bool probeVisual = parser.isSet("probe-visual");
    const QString visualReport = parser.value("visual-report");
    const bool probe = probeVulkan || probeVisual || !visualReport.isEmpty();

    // This must happen before the first Qt Quick window is constructed.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    std::atomic_bool vulkanReady = false;
    std::atomic_bool frameChecked = false;
    QTimer probeTimer;
    probeTimer.setSingleShot(true);
    QObject::connect(&probeTimer, &QTimer::timeout, &app, [] {
        qFatal("No verified Vulkan frame was rendered within 10 seconds");
    });
    KeyObserver keyObserver;
    QQuickView view;
    if (probeVisual || !visualReport.isEmpty())
        view.installEventFilter(&keyObserver);

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
                     [&app, &view, &probeTimer, &vulkanReady, &frameChecked, probeVulkan,
                      probeVisual, visualReport] {
                         if (!vulkanReady.load(std::memory_order_acquire))
                             qFatal("A frame swapped before Vulkan initialization was verified");
                         if (probeVulkan)
                             QMetaObject::invokeMethod(&app, "quit", Qt::QueuedConnection);
                         if ((probeVisual || !visualReport.isEmpty()) &&
                             !frameChecked.exchange(true, std::memory_order_acq_rel)) {
                             QMetaObject::invokeMethod(&view, [&app, &view, &probeTimer,
                                                               probeVisual, visualReport] {
                                 // Readback is only used by tests, never the running compositor.
                                 const QImage image = view.grabWindow();
                                 const bool pixelsMatch = image.width() > 200 && image.height() > 100 &&
                                     image.pixelColor(30, 30) == QColor("#36c7a7") &&
                                     image.pixelColor(200, 100) == QColor("#1d2430");
                                 const bool saved = visualReport.isEmpty() || image.save(visualReport);
                                 if (!pixelsMatch || !saved)
                                     std::fprintf(stderr, "QML Vulkan pixel check failed: image=%dx%d saved=%d\n",
                                                  image.width(), image.height(), saved);
                                 else
                                     std::fputs("QML Vulkan pixels verified\n", stderr);
                                 probeTimer.stop();
                                 if (probeVisual || !pixelsMatch || !saved)
                                     app.exit(pixelsMatch && saved ? 0 : 1);
                             }, Qt::QueuedConnection);
                         }
                     }, Qt::DirectConnection);

    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl("qrc:/Main.qml"));
    if (view.status() == QQuickView::Error)
        qFatal("Could not load Flowstate's QML scene");
    view.resize(640, 360);
    view.show();

    if (probe)
        probeTimer.start(10000);
    return app.exec();
}
