#include "backend.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextDocument>
#include <QtMath>

namespace {

struct Lang {
    const char *name;
    const char *keywords;
    const char *comment; // regex for comments
};

const char *kSlash = R"(//[^\n]*|/\*.*?\*/)";
const char *kHash = R"(#[^\n]*)";

const Lang kLangs[] = {
    {"C/C++", "auto break case char class const constexpr continue default delete do double else enum explicit extern false float for goto if inline int long namespace new nullptr operator private protected public return short signed sizeof static struct switch template this throw true try typedef typename union unsigned using virtual void volatile while include define", kSlash},
    {"Rust", "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while", kSlash},
    {"Go", "break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var true false nil", kSlash},
    {"JavaScript", "async await break case catch class const continue default delete do else export extends false finally for from function if import in instanceof let new null of return static super switch this throw true try typeof undefined var void while yield interface type enum", kSlash},
    {"Python", "and as assert async await break class continue def del elif else except False finally for from global if import in is lambda None nonlocal not or pass raise return True try while with yield self", kHash},
    {"Shell", "if then else elif fi for while do done case esac function in return local export echo cd exit set unset source", kHash},
    {"QML", "import property signal function id on as readonly required true false null undefined var let const if else for while return", kSlash},
    {"Plain", "", "(?!)"},
};

const Lang *findLang(const QString &name) {
    for (const auto &l : kLangs)
        if (name == QLatin1String(l.name))
            return &l;
    return &kLangs[std::size(kLangs) - 1];
}

QString escape(const QString &s) { return s.toHtmlEscaped(); }

} // namespace

QStringList Backend::languages() const {
    QStringList list{"Auto"};
    for (const auto &l : kLangs)
        list << l.name;
    return list;
}

void Backend::setCode(const QString &code) {
    if (code == m_code)
        return;
    m_code = code;
    ++m_revision;
    emit changed();
}

void Backend::setLanguage(const QString &language) {
    if (language == m_language)
        return;
    m_language = language;
    ++m_revision;
    emit changed();
}

void Backend::setWindowChrome(bool on) {
    if (on == m_chrome)
        return;
    m_chrome = on;
    ++m_revision;
    emit changed();
}

void Backend::setStatus(const QString &text, bool error) {
    m_status = text;
    m_error = error;
    emit statusChanged();
}

QString Backend::guessLanguage(const QString &code, const QString &path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QHash<QString, QString> byExt{
        {"c", "C/C++"}, {"h", "C/C++"}, {"cpp", "C/C++"}, {"hpp", "C/C++"}, {"cc", "C/C++"},
        {"rs", "Rust"}, {"go", "Go"}, {"js", "JavaScript"}, {"ts", "JavaScript"}, {"mjs", "JavaScript"},
        {"py", "Python"}, {"sh", "Shell"}, {"bash", "Shell"}, {"qml", "QML"}};
    if (byExt.contains(ext))
        return byExt.value(ext);
    auto has = [&](const char *re) { return QRegularExpression(re, QRegularExpression::MultilineOption).match(code).hasMatch(); };
    if (has(R"(^#!.*\b(ba)?sh\b)")) return "Shell";
    if (has(R"(^\s*(def |import \w+$|from \w+ import)|:\s*$)") && !has(R"([;{]\s*$)")) return "Python";
    if (has(R"(\bfn \w+|\blet mut\b|\bimpl\b)")) return "Rust";
    if (has(R"(^package \w+|\bfunc \w+)")) return "Go";
    if (has(R"(#include|std::|\bnullptr\b)")) return "C/C++";
    if (has(R"(\bconst \w+ =|=>|\bfunction\b)")) return "JavaScript";
    if (has(R"(^\s*\w+ \{\s*$|\bproperty \w+)")) return "QML";
    return "Plain";
}

// One combined regular expression walks the text once, so a keyword inside a
// string or comment is never colored as a keyword.
QString Backend::highlight(const QString &code, const QString &language) {
    const Lang *lang = findLang(language == "Auto" ? guessLanguage(code) : language);
    QStringList words = QString::fromLatin1(lang->keywords).split(' ', Qt::SkipEmptyParts);
    const QString keywordPattern = words.isEmpty() ? "(?!)" : "\\b(?:" + words.join('|') + ")\\b";
    const QRegularExpression re(
        QString("(?<comment>%1)|(?<string>\"(?:\\\\.|[^\"\\\\\\n])*\"|'(?:\\\\.|[^'\\\\\\n])*'|`[^`]*`)"
                "|(?<number>\\b(?:0x[0-9a-fA-F]+|\\d+(?:\\.\\d+)?)\\b)|(?<keyword>%2)"
                "|(?<call>\\b[A-Za-z_]\\w*(?=\\())").arg(lang->comment, keywordPattern),
        QRegularExpression::DotMatchesEverythingOption);

    static const QHash<QString, QString> colors{{"comment", "#6c7086"}, {"string", "#a6e3a1"}, {"number", "#fab387"},
                                                {"keyword", "#cba6f7"}, {"call", "#89b4fa"}};
    QString html;
    qsizetype pos = 0;
    auto it = re.globalMatch(code);
    while (it.hasNext()) {
        const auto m = it.next();
        html += escape(code.mid(pos, m.capturedStart() - pos));
        for (auto i = colors.begin(); i != colors.end(); ++i) {
            if (m.hasCaptured(i.key())) {
                html += QString("<span style=\"color:%1\">%2</span>").arg(i.value(), escape(m.captured(i.key())));
                break;
            }
        }
        pos = m.capturedEnd();
    }
    html += escape(code.mid(pos));
    return html;
}

QImage Backend::render(int scale) const {
    const QString source = m_code.isEmpty() ? QString("// ") + tr("Type or paste code") : m_code;
    QTextDocument doc;
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(15);
    doc.setDefaultFont(font);
    doc.setDocumentMargin(0);
    // The <pre> keeps indentation; tabs become four columns.
    QString text = source;
    text.replace('\t', "    ");
    doc.setHtml("<pre style=\"color:#cdd6f4; margin:0\">" + highlight(text, m_language) + "</pre>");
    doc.setTextWidth(-1);

    const double pad = 40, inner = 24, chrome = m_chrome ? 34 : 0;
    const QSizeF textSize(qMax(doc.idealWidth(), 240.0), doc.size().height());
    const QSizeF card(textSize.width() + inner * 2, textSize.height() + inner * 2 + chrome);
    const QSize full(qCeil(card.width() + pad * 2), qCeil(card.height() + pad * 2));

    QImage image(full * scale, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&image);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    p.scale(scale, scale);

    QLinearGradient bg(0, 0, full.width(), full.height());
    bg.setColorAt(0, "#7c5cff");
    bg.setColorAt(1, "#ff6ba0");
    p.fillRect(QRect(QPoint(), full), bg);

    const QRectF cardRect(pad, pad, card.width(), card.height());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70)); // soft drop shadow
    p.drawRoundedRect(cardRect.translated(0, 8), 12, 12);
    p.setBrush(QColor("#1e1e2e"));
    p.drawRoundedRect(cardRect, 12, 12);

    if (m_chrome) {
        const char *dots[] = {"#ff5f57", "#febc2e", "#28c840"};
        for (int i = 0; i < 3; ++i) {
            p.setBrush(QColor(dots[i]));
            p.drawEllipse(QPointF(pad + 22 + i * 20, pad + 20), 6, 6);
        }
    }
    p.translate(pad + inner, pad + inner + chrome);
    doc.drawContents(&p);
    return image;
}

QImage PreviewProvider::requestImage(const QString &, QSize *size, const QSize &) {
    const QImage image = m_backend->render(2);
    if (size)
        *size = image.size();
    return image;
}

QString Backend::saveDirectory() {
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return pictures.isEmpty() ? QDir::homePath() : pictures;
}

// Writes beside the target and renames, so a failed save never leaves half a file.
QString Backend::saveTo(const QString &directory) const {
    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath("codeshot-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".png");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !render(2).save(&file, "PNG") || !file.commit())
        return {};
    return path;
}

void Backend::saveImage() {
    const QString path = saveTo(saveDirectory());
    if (path.isEmpty())
        setStatus(tr("Could not save the image"), true);
    else
        setStatus(tr("Saved %1").arg(path));
}

void Backend::copyImage() {
    // wl-copy keeps the image available after the window closes, which
    // QClipboard on Wayland does not.
    const QString tool = QStandardPaths::findExecutable("wl-copy");
    if (tool.isEmpty()) {
        setStatus(tr("wl-copy is missing, install wl-clipboard to copy images"), true);
        return;
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    render(2).save(&buffer, "PNG");
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    process->start(tool, {"--type", "image/png"});
    if (!process->waitForStarted(2000)) {
        setStatus(tr("Could not copy the image"), true);
        process->deleteLater();
        return;
    }
    process->write(png);
    process->closeWriteChannel();
    setStatus(tr("Copied to the clipboard"));
}

void Backend::openFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(tr("Could not open %1").arg(path), true);
        return;
    }
    const QString text = QString::fromUtf8(file.readAll());
    m_language = "Auto";
    setCode(text);
    emit changed();
}
