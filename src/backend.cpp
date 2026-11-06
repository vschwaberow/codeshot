#include "backend.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTextDocument>
#include <QtMath>
#include <algorithm>
#include <array>
#include <iterator>
#include <memory>

namespace {

struct Lang {
    const char *name;
    const char *keywords;
    const char *comment; // regex for comments
    bool assembler = false;
};

constexpr const char *kSlash = R"(//[^\n]*|/\*.*?\*/)";
constexpr const char *kHash = R"(#[^\n]*)";
constexpr const char *kSemi = R"(;[^\n]*)";
constexpr const char *kSemiStar = R"(;[^\n]*|(?m:^\*[^\n]*))"; // 68k also comments whole lines with '*'

// Mnemonics, registers, and directives per CPU, matched case-insensitively.
constexpr const char *k68k = "abcd add adda addi addq addx and andi asl asr bcc bchg bclr bcs beq bge bgt bhi ble bls blt bmi bne bpl bra bset bsr btst bvc bvs chk clr cmp cmpa cmpi cmpm dbf dbra dbeq dbne divs divu eor eori exg ext illegal jmp jsr lea link lsl lsr move movea movem movep moveq muls mulu nbcd neg negx nop not or ori pea reset rol ror roxl roxr rte rtr rts sbcd scc seq sne st sf sub suba subi subq subx swap tas trap trapv tst unlk d0 d1 d2 d3 d4 d5 d6 d7 a0 a1 a2 a3 a4 a5 a6 a7 sp sr ccr usp pc section org dc ds equ rs set include incbin even cnop macro endm xdef xref";
constexpr const char *kX86 = "mov push pop lea add sub mul imul div idiv inc dec neg not and or xor shl shr sar sal rol ror cmp test jmp je jne jz jnz jg jge jl jle ja jae jb jbe js jns call ret leave int nop hlt cli sti cld std movzx movsx movs stos lods cmps scas rep repe repne loop xchg cdq cwde cbw sete setne setg setl eax ebx ecx edx esi edi esp ebp ax bx cx dx si di sp bp al ah bl bh cl ch dl dh cs ds es ss fs gs section segment global extern db dw dd dq resb resw resd resq equ times byte word dword qword ptr offset bits org";
constexpr const char *kX64 = "mov push pop lea add sub mul imul div idiv inc dec neg not and or xor shl shr sar sal rol ror cmp test jmp je jne jz jnz jg jge jl jle ja jae jb jbe js jns call ret leave syscall int nop hlt cmov cmove cmovne movzx movsx movsxd movabs movs stos lods rep xchg cqo cdqe sete setne setg setl rax rbx rcx rdx rsi rdi rsp rbp rip r8 r9 r10 r11 r12 r13 r14 r15 r8d r9d r10d r11d r12d r13d r14d r15d eax ebx ecx edx esi edi esp ebp ax bx cx dx al bl cl dl sil dil xmm0 xmm1 xmm2 xmm3 xmm4 xmm5 xmm6 xmm7 section segment global extern default rel db dw dd dq resb resw resd resq equ byte word dword qword ptr";
constexpr const char *k6510 = "adc and asl bcc bcs beq bit bmi bne bpl brk bvc bvs clc cld cli clv cmp cpx cpy dec dex dey eor inc inx iny jmp jsr lda ldx ldy lsr nop ora pha php pla plp rol ror rti rts sbc sec sed sei sta stx sty tax tay tsx txa txs tya lax sax dcp isc slo sre rla rra anc alr arr axs sbx x y a org byte word text fill incbin include macro endm";
constexpr const char *k65816 = "adc and asl bcc bcs beq bit bmi bne bpl bra brk brl bvc bvs clc cld cli clv cmp cop cpx cpy dec dex dey eor inc inx iny jml jmp jsl jsr lda ldx ldy lsr mvn mvp nop ora pea pei per pha phb phd phk php phx phy pla plb pld plp plx ply rep rol ror rti rtl rts sbc sec sed sei sep sta stp stx sty stz tax tay tcd tcs tdc tsc tsx txa txs txy tya tyx wai wdm xba xce a x y s org base byte word long dl dw db fill incbin include macro endm";

constexpr std::array<Lang, 13> kLangs{{
    {"C/C++", "auto break case char class const constexpr continue default delete do double else enum explicit extern false float for goto if inline int long namespace new nullptr operator private protected public return short signed sizeof static struct switch template this throw true try typedef typename union unsigned using virtual void volatile while include define", kSlash},
    {"Rust", "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while", kSlash},
    {"Go", "break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var true false nil", kSlash},
    {"JavaScript", "async await break case catch class const continue default delete do else export extends false finally for from function if import in instanceof let new null of return static super switch this throw true try typeof undefined var void while yield interface type enum", kSlash},
    {"Python", "and as assert async await break class continue def del elif else except False finally for from global if import in is lambda None nonlocal not or pass raise return True try while with yield self", kHash},
    {"Shell", "if then else elif fi for while do done case esac function in return local export echo cd exit set unset source", kHash},
    {"QML", "import property signal function id on as readonly required true false null undefined var let const if else for while return", kSlash},
    {"Assembler 68k", k68k, kSemiStar, true},
    {"Assembler x86", kX86, kSemi, true},
    {"Assembler x64", kX64, kSemi, true},
    {"Assembler 6510", k6510, kSemi, true},
    {"Assembler 65816", k65816, kSemi, true},
    {"Plain", "", "(?!)"},
}};

// Finds the entry whose name matches, or nullptr; each table picks its own
// fallback (Plain for languages, Omarchy for frames).
template <typename T, std::size_t N>
const T *findByName(const std::array<T, N> &table, const QString &name) {
    const auto it = std::find_if(table.begin(), table.end(),
                                 [&name](const T &entry) { return name == QLatin1String(entry.name); });
    return it == table.end() ? nullptr : &*it;
}

const Lang *findLang(const QString &name) {
    const Lang *found = findByName(kLangs, name);
    return found ? found : &kLangs.back();
}

QString escape(const QString &s) { return s.toHtmlEscaped(); }

// One row per window frame: title-bar height, corner radius, and the
// painter that draws its chrome (null for None). The retro frames have
// square corners; Omarchy and None keep the plain rounded card.
using FramePainter = void (*)(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &accent);

struct FrameSpec {
    const char *name;
    double chrome;
    double radius;
    FramePainter paint;
};

void paintOmarchy(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &accent);
void paintWindows(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &accent);
void paintMacOS(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &accent);
void paintAtari(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &accent);
void paintAmiga13(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &accent);
void paintAmiga3(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &accent);

constexpr std::array<FrameSpec, 7> kFrames{{
    {"Omarchy", 0, 12, paintOmarchy},
    {"Windows", 36, 8, paintWindows},
    {"macOS", 34, 12, paintMacOS},
    {"Atari TOS", 24, 0, paintAtari},
    {"Amiga OS 1.3", 24, 0, paintAmiga13},
    {"Amiga OS 3.x", 24, 0, paintAmiga3},
    {"None", 0, 12, nullptr},
}};

const FrameSpec *findFrame(const QString &name) {
    const FrameSpec *found = findByName(kFrames, name);
    return found ? found : &kFrames.front();
}

QFont titleFont(int pixelSize) {
    QFont font("sans-serif");
    font.setPixelSize(pixelSize);
    return font;
}

void drawTitle(QPainter &p, const QRectF &rect, const QString &title, const QColor &color, int alignment, int pixelSize) {
    p.setFont(titleFont(pixelSize));
    p.setPen(color);
    const QString text = QFontMetrics(p.font()).elidedText(title, Qt::ElideRight, int(rect.width()));
    p.drawText(rect, alignment | Qt::AlignVCenter, text);
}

// The paired black/white line that separates Amiga title-bar gadgets.
void gadgetSeparator(QPainter &p, double x, const QRectF &band) {
    p.setPen(QPen(Qt::black, 1));
    p.drawLine(QPointF(x, band.top()), QPointF(x, band.bottom()));
    p.setPen(QPen(Qt::white, 1));
    p.drawLine(QPointF(x + 1.5, band.top()), QPointF(x + 1.5, band.bottom()));
}

void paintOmarchy(QPainter &p, const QRectF &card, const QRectF &, const QString &, const QColor &accent) {
    // Hyprland draws no title bar, just the accent border.
    p.setPen(QPen(accent, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(card.adjusted(1, 1, -1, -1), 11, 11);
}

void paintMacOS(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &) {
    drawTitle(p, band, title, "#8e8ea3", Qt::AlignCenter, 12);
    constexpr std::array<const char *, 3> dots{{"#ff5f57", "#febc2e", "#28c840"}};
    p.setPen(Qt::NoPen);
    for (std::size_t i = 0; i < dots.size(); ++i) {
        p.setBrush(QColor(dots.at(i)));
        p.drawEllipse(QPointF(card.x() + 22 + int(i) * 20, card.y() + 20), 6, 6);
    }
}

void paintWindows(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2c2c30"));
    p.drawRect(band);
    const double buttonW = 46, buttonsX = card.right() - buttonW * 3;
    p.setBrush(QColor("#e81123"));
    p.drawRect(QRectF(card.right() - buttonW, card.y(), buttonW, band.height()));
    drawTitle(p, QRectF(card.x() + 14, card.y(), buttonsX - card.x() - 22, band.height()), title, "#eaeaea", Qt::AlignLeft, 12);
    p.setPen(QPen(QColor("#eaeaea"), 1.3));
    const double cy = card.y() + band.height() / 2;
    const double minCx = buttonsX + buttonW / 2, maxCx = minCx + buttonW, closeCx = maxCx + buttonW;
    p.drawLine(QPointF(minCx - 5, cy), QPointF(minCx + 5, cy));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(maxCx - 4.5, cy - 4.5, 9, 9));
    p.drawLine(QPointF(closeCx - 5, cy - 5), QPointF(closeCx + 5, cy + 5));
    p.drawLine(QPointF(closeCx - 5, cy + 5), QPointF(closeCx + 5, cy - 5));
}

void paintAtari(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &) {
    // GEM: white drag bar, striped fill, a box at each end.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#ffffff"));
    p.drawRect(band);
    const QRectF closeBox(card.x() + 7, card.y() + 6, 12, 12);
    const QRectF fullBox(card.right() - 19, card.y() + 6, 12, 12);
    p.setFont(titleFont(12));
    const int titleW = QFontMetrics(p.font()).horizontalAdvance(title);
    const double titleLeft = card.center().x() - titleW / 2.0, titleRight = titleLeft + titleW;
    p.setPen(QPen(Qt::black, 1));
    for (double y = card.y() + 4; y < band.bottom() - 3; y += 2) {
        p.drawLine(QPointF(closeBox.right() + 6, y), QPointF(titleLeft - 8, y));
        p.drawLine(QPointF(titleRight + 8, y), QPointF(fullBox.left() - 6, y));
    }
    p.setBrush(QColor("#ffffff"));
    p.drawRect(closeBox);
    p.drawRect(fullBox);
    p.drawRect(QRectF(fullBox.x() + 2, fullBox.y() + 2, 5, 5));
    p.drawLine(QPointF(card.x(), band.bottom()), QPointF(card.right(), band.bottom()));
    drawTitle(p, band, title, Qt::black, Qt::AlignCenter, 12);
}

void paintAmiga13(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &) {
    // Workbench 1.3: white drag bar, blue title, white window border
    // with a black line inside it. The close gadget is a blue-outlined
    // box with a black square inside; the depth gadget is a black and a
    // white square overlapping.
    const QColor blue("#0055aa");
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRect(band);
    const QRectF closeBox(card.x() + 6, card.y() + 6, 12, 12);
    const QRectF back(card.right() - 29, card.y() + 5, 11, 10), front(card.right() - 21, card.y() + 8, 11, 10);
    p.setPen(QPen(blue, 1));
    p.drawLine(QPointF(closeBox.right() + 5, card.y()), QPointF(closeBox.right() + 5, band.bottom()));
    p.drawLine(QPointF(back.left() - 6, card.y()), QPointF(back.left() - 6, band.bottom()));
    drawTitle(p, QRectF(closeBox.right() + 11, card.y(), back.left() - closeBox.right() - 22, band.height()), title, blue, Qt::AlignLeft, 12);
    p.setPen(QPen(blue, 1.5));
    p.setBrush(Qt::white);
    p.drawRect(closeBox);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawRect(QRectF(closeBox.center().x() - 2, closeBox.center().y() - 2, 4, 4));
    p.setPen(QPen(blue, 1.2));
    p.setBrush(Qt::black);
    p.drawRect(back);
    p.setBrush(Qt::white);
    p.drawRect(front);
    p.setPen(QPen(Qt::black, 1));
    p.drawLine(QPointF(card.x(), band.bottom() - 0.5), QPointF(card.right(), band.bottom() - 0.5));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 2));
    p.drawRect(card.adjusted(1, 1, -1, -1));
    p.setPen(QPen(Qt::black, 1));
    p.drawRect(card.adjusted(3.5, 3.5, -3.5, -3.5));
}

void paintAmiga3(QPainter &p, const QRectF &card, const QRectF &band, const QString &title, const QColor &) {
    // The active window: orange fill bar and borders, black title, and
    // flat gadget imagery in separated cells — close on the left, then
    // zoom and depth (rightmost) on the right.
    const QColor orange("#ff8800"), grey("#aaaaaa");
    p.setPen(Qt::NoPen);
    p.setBrush(orange);
    p.drawRect(band);
    const double cell = 24, gy = card.y() + 6;
    const double closeX = card.x(), zoomX = card.right() - cell * 2, depthX = card.right() - cell;
    gadgetSeparator(p, closeX + cell, band);
    gadgetSeparator(p, zoomX, band);
    gadgetSeparator(p, depthX, band);
    // Close: a hollow box — thick black outline, white middle.
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawRect(QRectF(closeX + 7.5, gy, 9, 12));
    p.setBrush(Qt::white);
    p.drawRect(QRectF(closeX + 9.5, gy + 2, 5, 8));
    // Zoom: an outlined square with a small white square top-left.
    p.setPen(QPen(Qt::black, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(zoomX + 6, gy + 1, 12, 11));
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::white);
    p.drawRect(QRectF(zoomX + 6, gy + 1, 5, 5));
    // Depth: a grey square behind a white one.
    p.setPen(QPen(Qt::black, 1.2));
    p.setBrush(grey);
    p.drawRect(QRectF(depthX + 3, gy, 10, 9));
    p.setBrush(Qt::white);
    p.drawRect(QRectF(depthX + 9, gy + 4, 10, 9));
    p.setPen(QPen(Qt::white, 1));
    p.drawLine(QPointF(card.x(), card.y() + 0.5), QPointF(card.right(), card.y() + 0.5));
    p.setPen(QPen(Qt::black, 1));
    p.drawLine(QPointF(card.x(), band.bottom() - 0.5), QPointF(card.right(), band.bottom() - 0.5));
    drawTitle(p, QRectF(closeX + cell + 8, card.y(), zoomX - closeX - cell - 12, band.height()), title, Qt::black, Qt::AlignLeft, 12);
    // Orange border inside a black outline, lit from the top-left.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(orange, 3));
    p.drawRect(card.adjusted(2.5, 2.5, -2.5, -2.5));
    p.setPen(QPen(Qt::black, 1));
    p.drawRect(card.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setPen(QPen(Qt::white, 1));
    p.drawLine(QPointF(card.x() + 4.5, band.bottom()), QPointF(card.x() + 4.5, card.bottom() - 4));
    p.setPen(QPen(Qt::black, 1));
    p.drawLine(QPointF(card.right() - 4.5, band.bottom()), QPointF(card.right() - 4.5, card.bottom() - 4));
    p.drawLine(QPointF(card.x() + 4, card.bottom() - 4.5), QPointF(card.right() - 4, card.bottom() - 4.5));
}

} // namespace

QStringList Backend::languages() const {
    QStringList list{"Auto"};
    std::transform(kLangs.begin(), kLangs.end(), std::back_inserter(list),
                   [](const Lang &lang) { return QString::fromLatin1(lang.name); });
    return list;
}

void Backend::setCode(const QString &code) { setField(m_code, code); }

void Backend::setLanguage(const QString &language) { setField(m_language, language); }

QStringList Backend::frames() const {
    QStringList list;
    std::transform(kFrames.begin(), kFrames.end(), std::back_inserter(list),
                   [](const FrameSpec &frame) { return QString::fromLatin1(frame.name); });
    return list;
}

void Backend::setFrame(const QString &frame) { setField(m_frame, frame); }

void Backend::setAccent(const QString &accent) { setField(m_accent, accent); }

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
        {"i", "Assembler 68k"}, {"s", "Assembler 68k"}, {"asm", "Assembler 68k"}, {"a65", "Assembler 6510"},
        {"py", "Python"}, {"sh", "Shell"}, {"bash", "Shell"}, {"qml", "QML"}};
    if (byExt.contains(ext))
        return byExt.value(ext);
    auto has = [&](const char *re) { return QRegularExpression(re, QRegularExpression::MultilineOption).match(code).hasMatch(); };
    if (has(R"(\b(movem?|moveq|dbra|rts|lea)\.?[bwl]?\s+.*\b[da][0-7]\b)")) return "Assembler 68k";
    if (has(R"(\b(rep|sep)\s+#\$[0-9a-f]{2}|\b(xba|xce|jsl|rtl)\b)")) return "Assembler 65816";
    if (has(R"(\b(lda|sta|ldx|ldy|jsr|inx|dex)\b)")) return "Assembler 6510";
    if (has(R"(\br[a-d]x\b|\br(sp|bp|ip)\b|\bsyscall\b)")) return "Assembler x64";
    if (has(R"(\be[a-d]x\b|\b(esp|ebp)\b)")) return "Assembler x86";
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
    // Assembler adds $hex, %binary, and line-start labels, and ignores case.
    const QString number = lang->assembler
        ? "\\$[0-9a-fA-F]+|%[01]+|\\b(?:0x[0-9a-fA-F]+|[0-9][0-9a-fA-F]*h|\\d+)\\b"
        : "\\b(?:0x[0-9a-fA-F]+|\\d+(?:\\.\\d+)?)\\b";
    const QString call = lang->assembler ? "(?m:^[A-Za-z_.][\\w.]*(?=:))" : "\\b[A-Za-z_]\\w*(?=\\()";
    QRegularExpression::PatternOptions options = QRegularExpression::DotMatchesEverythingOption;
    if (lang->assembler)
        options |= QRegularExpression::CaseInsensitiveOption;
    const QRegularExpression re(
        QString("(?<comment>%1)|(?<string>\"(?:\\\\.|[^\"\\\\\\n])*\"|'(?:\\\\.|[^'\\\\\\n])*'|`[^`]*`)"
                "|(?<number>%3)|(?<keyword>%2)|(?<call>%4)").arg(lang->comment, keywordPattern, number, call),
        options);

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

    const FrameSpec *spec = findFrame(m_frame);
    const double pad = 40, inner = 24, chrome = spec->chrome, radius = spec->radius;
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
    p.drawRoundedRect(cardRect.translated(0, 8), radius, radius);
    p.setBrush(QColor("#1e1e2e"));
    p.drawRoundedRect(cardRect, radius, radius);

    // Title bars are square at the corners, so clip them to the card shape.
    // The scope guard restores the painter state at the end of the block.
    QPainterPath clip;
    clip.addRoundedRect(cardRect, radius, radius);
    {
        p.save();
        const auto restore = qScopeGuard([&p] { p.restore(); });
        p.setClipPath(clip);
        if (spec->paint)
            spec->paint(p, cardRect, QRectF(cardRect.x(), cardRect.y(), cardRect.width(), chrome), m_title, QColor(m_accent));
    }
    p.translate(pad + inner, pad + inner + chrome);
    doc.drawContents(&p);
    return image;
}

QImage PreviewProvider::requestImage(const QString &, QSize *size, const QSize &) {
    if (!m_backend)
        return {};
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
    // Owned by the unique_ptr until the process is running; then ownership
    // passes to the QObject tree, which deletes it when it finishes. If it
    // never starts, the unique_ptr cleans up on return.
    auto process = std::make_unique<QProcess>();
    process->start(tool, {"--type", "image/png"});
    if (!process->waitForStarted(2000)) {
        setStatus(tr("Could not copy the image"), true);
        return;
    }
    process->write(png);
    process->closeWriteChannel();
    auto *released = process.release();
    released->setParent(this);
    connect(released, &QProcess::finished, released, &QObject::deleteLater);
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
    m_title = QFileInfo(path).fileName().isEmpty() ? "codeshot" : QFileInfo(path).fileName();
    setCode(text);
    emit changed();
}
