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
