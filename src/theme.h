#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    // Fixed neutral palette; only the accent and radius follow the desktop.
    Q_PROPERTY(QString window READ window CONSTANT)
    Q_PROPERTY(QString surface READ surface CONSTANT)
    Q_PROPERTY(QString border READ border CONSTANT)
    Q_PROPERTY(QString mutedText READ mutedText CONSTANT)
    Q_PROPERTY(QString error READ error CONSTANT)
    Q_PROPERTY(QString scrim READ scrim CONSTANT)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    QString window() const { return "#101012"; }
    QString surface() const { return "#17171a"; }
    QString border() const { return "#2a2a2e"; }
    QString mutedText() const { return "#9a9aa3"; }
    QString error() const { return "#ff6b6b"; }
    QString scrim() const { return "#b3000000"; }

    static QString readAccent(const QString &path);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
