#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "theme.h"
#include "translator.h"

class CodeshotTests : public QObject {
    Q_OBJECT

private slots:
    void guessesLanguage()
    {
        QCOMPARE(Backend::guessLanguage("fn main() {}"), QString("Rust"));
        QCOMPARE(Backend::guessLanguage("#include <stdio.h>"), QString("C/C++"));
        QCOMPARE(Backend::guessLanguage("x", "a.py"), QString("Python"));
    }

    void keywordsInsideStringsStayStrings()
    {
        const QString html = Backend::highlight("let s = \"if else\";", "Rust");
        QVERIFY(html.contains("#cba6f7\">let"));
        QVERIFY(!html.contains("#cba6f7\">if"));
        QVERIFY(html.contains("#a6e3a1"));
    }

    void highlightsAssembler()
    {
        QCOMPARE(Backend::guessLanguage("move.l d0,a1"), QString("Assembler 68k"));
        QCOMPARE(Backend::guessLanguage("lda #$01\nsta $d020"), QString("Assembler 6510"));
        QCOMPARE(Backend::guessLanguage("rep #$30\nxba"), QString("Assembler 65816"));
        QCOMPARE(Backend::guessLanguage("mov rax, rbx"), QString("Assembler x64"));
        QCOMPARE(Backend::guessLanguage("mov eax, ebx"), QString("Assembler x86"));
        const QString html = Backend::highlight("loop: LDA #$01 ; hi", "Assembler 6510");
        QVERIFY(html.contains("#cba6f7\">LDA"));
        QVERIFY(html.contains("#fab387\">#$01") || html.contains("#fab387\">$01"));
        QVERIFY(html.contains("#6c7086\">; hi"));
    }

    void escapesHtml()
    {
        QVERIFY(Backend::highlight("a < b && c", "Plain").contains("&lt;"));
    }

    void rendersAndSaves()
    {
        Backend backend;
        backend.setCode("int main() { return 0; }");
        QVERIFY(backend.render(2).width() > 200);
        QTemporaryDir dir;
        const QString path = backend.saveTo(dir.path());
        QVERIFY(!path.isEmpty());
        QVERIFY(QFile::exists(path));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.window(), QString("#101012"));
        QCOMPARE(theme.surface(), QString("#17171a"));
        QCOMPARE(theme.border(), QString("#2a2a2e"));
        QCOMPARE(theme.mutedText(), QString("#9a9aa3"));
        QCOMPARE(theme.error(), QString("#ff6b6b"));
        QCOMPARE(theme.scrim(), QString("#b3000000"));
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
    }
};

QTEST_MAIN(CodeshotTests)
#include "codeshot_tests.moc"
