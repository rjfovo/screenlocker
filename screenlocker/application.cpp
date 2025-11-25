#include "application.h"

// Qt Core
#include <QAbstractNativeEventFilter>
#include <QScreen>
#include <QEvent>
#include <QFile>

// Qt Quick
#include <QQuickItem>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>

// this is usable to fake a "screensaver" installation for testing
// *must* be "0" for every public commit!
#define TEST_SCREENSAVER 0

class FocusOutEventFilter : public QAbstractNativeEventFilter
{
public:
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(const QByteArray &eventType, void *message, long int *result) override {
#else
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override {
#endif
        Q_UNUSED(result)
        
        // 在Qt6中，我们需要更谨慎地处理原生事件
        // 暂时禁用X11特定的事件处理
        return false;
    }
};

Application::Application(int &argc, char **argv)
    : QGuiApplication(argc, argv)
    , m_authenticator(new Authenticator(AuthenticationMode::Direct, this))
{
    // It's a queued connection to give the QML part time to eventually execute code connected to Authenticator::succeeded if any
    connect(m_authenticator, &Authenticator::succeeded, this, &Application::onSucceeded, Qt::QueuedConnection);

    installEventFilter(this);

    // Screens
    connect(this, &Application::screenAdded, this, &Application::onScreenAdded);
    connect(this, &Application::screenRemoved, this, &Application::desktopResized);

    // 在Qt6中简化平台检测
    if (QGuiApplication::platformName().contains("xcb")) {
        installNativeEventFilter(new FocusOutEventFilter);
    }
}

Application::~Application()
{
    // workaround QTBUG-55460
    // will be fixed when themes port to QQC2
    for (auto view : std::as_const(m_views)) {
        if (QQuickItem *focusItem = view->activeFocusItem()) {
            focusItem->setFocus(false);
        }
    }
    qDeleteAll(m_views);
}

void Application::initialViewSetup()
{
    for (QScreen *screen : screens()) {
        connect(screen, &QScreen::geometryChanged, this, [this, screen](const QRect &geo) {
            screenGeometryChanged(screen, geo);
        });
    }

    desktopResized();
}

void Application::desktopResized()
{
    const int nScreens = screens().count();
    // remove useless views and savers
    while (m_views.count() > nScreens) {
        m_views.takeLast()->deleteLater();
    }

    // extend views and savers to current demand
    for (int i = m_views.count(); i < nScreens; ++i) {
        // create the view
        auto *view = new QQuickView;
        view->create();

        // engine stuff
        QQmlContext *context = view->engine()->rootContext();
        context->setContextProperty(QStringLiteral("authenticator"), m_authenticator);

        view->setSource(QUrl("qrc:/qml/LockScreen.qml"));
        view->setResizeMode(QQuickView::SizeRootObjectToView);

        view->setColor(Qt::black);
        auto screen = QGuiApplication::screens()[i];
        view->setGeometry(screen->geometry());

        if (!m_testing) {
            // 统一使用FramelessWindowHint
            view->setFlags(Qt::FramelessWindowHint);
        }

        // overwrite the factory set by kdeclarative
        // auto oldFactory = view->engine()->networkAccessManagerFactory();
        // view->engine()->setNetworkAccessManagerFactory(nullptr);
        // delete oldFactory;
        // view->engine()->setNetworkAccessManagerFactory(new NoAccessNetworkAccessManagerFactory);

        view->setGeometry(screen->geometry());

        connect(view, &QQuickView::frameSwapped, this, [=] { markViewsAsVisible(view); }, Qt::QueuedConnection);

        m_views << view;
    }

    // update geometry of all views and savers
    for (int i = 0; i < nScreens; ++i) {
        auto *view = m_views.at(i);
        auto screen = QGuiApplication::screens()[i];
        view->setScreen(screen);

        // 简化窗口显示逻辑
        if (m_testing) {
            view->show();
        } else {
            view->showFullScreen();
        }

        view->raise();
    }
}

void Application::onScreenAdded(QScreen *screen)
{
    // Lambda connections can not have uniqueness constraints, ensure
    // geometry change signals are only connected once
    connect(screen, &QScreen::geometryChanged, this, [this, screen](const QRect &geo) {
        screenGeometryChanged(screen, geo);
    });

    desktopResized();
}

void Application::onSucceeded()
{
    QQuickView *mainView = nullptr;

    // 寻找主屏幕的 view
    for (int i = 0; i < m_views.size(); ++i) {
        if (m_views.at(i)->screen() == QGuiApplication::primaryScreen()) {
            mainView = m_views.at(i);
            break;
        }
    }

    if (mainView) {
        QVariantAnimation *ani = new QVariantAnimation;

        connect(ani, &QVariantAnimation::valueChanged, [mainView] (const QVariant &value) {
            mainView->setY(value.toInt());
        });

        connect(ani, &QVariantAnimation::finished, this, [=] {
            QCoreApplication::exit();
        });

        ani->setDuration(500);
        ani->setEasingCurve(QEasingCurve::OutSine);
        ani->setStartValue(mainView->geometry().y());
        ani->setEndValue(mainView->geometry().y() + -mainView->geometry().height());
        ani->start();
    } else {
        QCoreApplication::exit();
    }
}

void Application::getFocus()
{
    QWindow *activeScreen = getActiveScreen();

    if (!activeScreen) {
        return;
    }

    // this loop is required to make the qml/graphicsscene properly handle the shared keyboard input
    // ie. "type something into the box of every greeter"
    for (QQuickView *view : std::as_const(m_views)) {
        if (!m_testing) {
            view->setKeyboardGrabEnabled(true);
        }
    }

    // activate window and grab input to be sure it really ends up there.
    // focus setting is still required for proper internal QWidget state (and eg. visual reflection)
    if (!m_testing) {
        activeScreen->setKeyboardGrabEnabled(true);
    }

    activeScreen->requestActivate();
}

void Application::markViewsAsVisible(QQuickView *view)
{
    disconnect(view, &QQuickWindow::frameSwapped, this, nullptr);
    QQmlProperty showProperty(view->rootObject(), QStringLiteral("viewVisible"));
    showProperty.write(true);

    // random state update, actually rather required on init only
    QMetaObject::invokeMethod(this, "getFocus", Qt::QueuedConnection);
}

bool Application::eventFilter(QObject *obj, QEvent *event)
{
    if (obj != this && event->type() == QEvent::Show) {
        QQuickView *view = nullptr;
        for (QQuickView *v : std::as_const(m_views)) {
            if (v == obj) {
                view = v;
                break;
            }
        }
        
        // 简化X11特定逻辑，在Qt6中可能需要使用不同的方法
        // 暂时注释掉X11特定的代码
        /*
        if (view && view->winId() && QGuiApplication::platformName().contains("xcb")) {
            // 在Qt6中需要使用新的方法来处理原生窗口属性
            // 这里暂时禁用X11特定功能
        }
        */
        // no further processing
        return false;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        if (getActiveScreen()) {
            getActiveScreen()->requestActivate();
        }
        return false;
    }

    // 修复事件类型检查 - 使用QEvent枚举值而不是宏
    if (event->type() == QEvent::Type::KeyPress) { // react if saver is visible
        shareEvent(event, qobject_cast<QQuickView *>(obj));
        return false; // we don't care
    } else if (event->type() == QEvent::Type::KeyRelease) { // conditionally reshow the saver
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() != Qt::Key_Escape) {
            shareEvent(event, qobject_cast<QQuickView *>(obj));
            return false; // irrelevant
        }
        return true; // don't pass
    }

    return false;
}

QWindow *Application::getActiveScreen()
{
    QWindow *activeScreen = nullptr;

    if (m_views.isEmpty()) {
        return activeScreen;
    }

    for (QQuickView *view : std::as_const(m_views)) {
        if (view->geometry().contains(QCursor::pos())) {
            activeScreen = view;
            break;
        }
    }
    if (!activeScreen) {
        activeScreen = m_views.first();
    }

    return activeScreen;
}

void Application::shareEvent(QEvent *e, QQuickView *from)
{
    // from can be NULL any time (because the parameter is passed as qobject_cast)
    // m_views.contains(from) is atm. supposed to be true but required if any further
    // QQuickView are added (which are not part of m_views)
    // this makes "from" an optimization (nullptr check aversion)
    if (from && m_views.contains(from)) {
        // NOTICE any recursion in the event sharing will prevent authentication on multiscreen setups!
        // Any change in regarded event processing shall be tested thoroughly!
        removeEventFilter(this); // prevent recursion!
        const bool accepted = e->isAccepted(); // store state
        for (QQuickView *view : std::as_const(m_views)) {
            if (view != from) {
                QCoreApplication::sendEvent(view, e);
                e->setAccepted(accepted);
            }
        }
        installEventFilter(this);
    }
}

void Application::screenGeometryChanged(QScreen *screen, const QRect &geo)
{
    // We map screens() to m_views by index and Qt is free to
    // reorder screens, so pointer to pointer connections
    // may not remain matched by index, perform index
    // mapping in the change event itself
    const int screenIndex = QGuiApplication::screens().indexOf(screen);
    if (screenIndex < 0) {
        qWarning() << "Screen not found, not updating geometry" << screen;
        return;
    }

    if (screenIndex >= m_views.size()) {
        qWarning() << "Screen index out of range, not updating geometry" << screenIndex;
        return;
    }

    QQuickView *view = m_views[screenIndex];
    view->setGeometry(geo);
}