#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QQuickImageProvider>
#include <QStringList>
#include <QTimer>
#include <utility>

// Renders code to an image. The same function paints the preview and the
// exported file, so what the user sees is what gets saved.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString code READ code WRITE setCode NOTIFY changed)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    Q_PROPERTY(QStringList languages READ languages CONSTANT)
    Q_PROPERTY(QString frame READ frame WRITE setFrame NOTIFY changed)
    Q_PROPERTY(QStringList frames READ frames CONSTANT)
    // The Omarchy frame strokes the card in the desktop accent; the window
    // binds this to the theme so the export follows it live.
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    QString code() const { return m_code; }
    void setCode(const QString &code);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    QStringList languages() const;
    QString frame() const { return m_frame; }
    void setFrame(const QString &frame);
    QStringList frames() const;
    QString accent() const { return m_accent; }
    void setAccent(const QString &accent);
    int revision() const { return m_revision; }
    QString status() const { return m_status; }
    bool statusIsError() const { return m_error; }

    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void saveImage();
    Q_INVOKABLE void copyImage();

    // Renders on the calling (GUI) thread; the image provider reads the
    // cached result instead, because its requests run on another thread.
    QImage render(int scale = 2) const;
    QImage previewImage() const;
    static QString highlight(const QString &code, const QString &language);
    static QString guessLanguage(const QString &code, const QString &path = {});
    // Where saved images go; tests pass a temp directory.
    static QString saveDirectory();
    QString saveTo(const QString &directory) const;

signals:
    void changed();
    void statusChanged();

private:
    void setStatus(const QString &text, bool error = false);

    void refreshPreview();

    // Every property that reaches the render follows the same rule: ignore
    // no-ops, otherwise store and notify at once. The revision bump and
    // re-render follow via the preview timer, coalesced so that typing in
    // a large file does not re-render on every keystroke; saving and
    // copying always render fresh, so exports never lag behind.
    // (A plain member template, not a slot, so moc is fine.)
    template <typename T>
    void setField(T &field, T value) {
        if (field == value)
            return;
        field = std::move(value);
        m_previewTimer.start();
        emit changed();
    }

    QString m_code;
    QString m_language = "Auto";
    QString m_frame = "Omarchy";
    QString m_accent = "#FFD60A";
    QString m_title = "codeshot";
    int m_revision = 0;
    QTimer m_previewTimer;
    QImage m_cachedPreview;
    mutable QMutex m_cacheMutex;
    QString m_status;
    bool m_error = false;
};

class PreviewProvider : public QQuickImageProvider {
public:
    explicit PreviewProvider(Backend *backend) : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}
    QImage requestImage(const QString &, QSize *size, const QSize &) override;

private:
    // Non-owning and guarded: goes null if the backend ever dies first.
    QPointer<Backend> m_backend;
};
