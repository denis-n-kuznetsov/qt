/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the Qt scene graph research project.
**
** $QT_BEGIN_LICENSE:LGPL$
** No Commercial Usage
** This file contains pre-release code and may not be distributed.
** You may use this file in accordance with the terms and conditions
** contained in the Technology Preview License Agreement accompanying
** this package.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights.  These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QtCore>
#include <QtGui>

#include <private/distancefieldfontatlas_p.h>

static void usage()
{
    qWarning("Usage: distfieldgen [options] <font_filename>");
    qWarning(" ");
    qWarning("Distfieldgen generates distance-field renderings of the provided font file,");
    qWarning("one for each font family/style it contains.");
    qWarning("Unless the QT_QML_DISTFIELDDIR environment variable is set, the renderings are");
    qWarning("saved in the fonts/distancefields directory where the Qt libraries are located.");
    qWarning("You can also override the output directory with the -d option.");
    qWarning(" ");
    qWarning(" options:");
    qWarning("  -d <directory>................................ output directory");
    qWarning("  -no-multithread............................... don't use multiple threads to render distance-fields");

    qWarning(" ");
    exit(1);
}

void printProgress(int p)
{
    printf("\r  [");
    for (int i = 0; i < 50; ++i)
        printf(i < p / 2 ? "=" : " ");
    printf("]");
    printf(" %d%%", p);
    fflush(stdout);
}

class DistFieldGenTask : public QRunnable
{
public:
    DistFieldGenTask(DistanceFieldFontAtlas *atlas, int c, int nbGlyph, QMap<int, QImage> *outList)
        : QRunnable()
        , m_atlas(atlas)
        , m_char(c)
        , m_nbGlyph(nbGlyph)
        , m_outList(outList)
    { }

    void run()
    {
        QImage df = m_atlas->renderDistanceFieldGlyph(m_char);
        QMutexLocker lock(&m_mutex);
        m_outList->insert(m_char, df);
        printProgress(float(m_outList->count()) / m_nbGlyph * 100);
    }

    static QMutex m_mutex;
    DistanceFieldFontAtlas *m_atlas;
    int m_char;
    int m_nbGlyph;
    QMap<int, QImage> *m_outList;
};

QMutex DistFieldGenTask::m_mutex;

static void generateDistanceFieldForFont(const QFont &font, const QString &destinationDir, bool multithread)
{
    QFontDatabase db;
    QString fontString = font.family() + QLatin1String(" ") + db.styleString(font);
    qWarning("> Generating distance-field for font '%s'", fontString.toLatin1().constData());

    DistanceFieldFontAtlas atlas(font);

    QMap<int, QImage> distfields;
    for (int i = 0; i < 0xFF; ++i) {
        if (multithread) {
            DistFieldGenTask *task = new DistFieldGenTask(&atlas, i, 0xFF, &distfields);
            QThreadPool::globalInstance()->start(task);
        } else {
            QImage df = atlas.renderDistanceFieldGlyph(i);
            distfields.insert(i, df);
            printProgress(float(distfields.count()) / 0xFF * 100);
        }
    }

    if (multithread)
        QThreadPool::globalInstance()->waitForDone();

    // Combine dist fields in one image
    QImage output(atlas.atlasSize(), QImage::Format_ARGB32_Premultiplied);
    output.fill(Qt::transparent);
    int i = 0;
    QPainter p(&output);
    foreach (const QImage &df, distfields.values()) {
        DistanceFieldFontAtlas::TexCoord c = atlas.glyphTexCoord(i);
        p.drawImage(qRound(c.x * atlas.atlasSize().width()), qRound(c.y * atlas.atlasSize().height()), df);
        ++i;
    }
    p.end();
    printProgress(100);
    printf("\n");

    // Save output
    QString destDir = destinationDir;
    if (destDir.isEmpty()) {
        destDir = atlas.distanceFieldDir();
        QDir dir;
        dir.mkpath(destDir);
    } else {
        QFileInfo dfi(destDir);
        if (!dfi.isDir()) {
            qWarning("Error: '%s' is not a directory.", destDir.toLatin1().constData());
            qWarning(" ");
            exit(1);
        }
        destDir = dfi.canonicalFilePath();
    }

    QString out = QString(QLatin1String("%1/%2")).arg(destDir).arg(atlas.distanceFieldFileName());
    output.save(out);
    qWarning("  Distance-field saved to '%s'\n", out.toLatin1().constData());
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QStringList args = QApplication::arguments();

    if (argc < 2
            || args.contains(QLatin1String("--help"))
            || args.contains(QLatin1String("-help"))
            || args.contains(QLatin1String("--h"))
            || args.contains(QLatin1String("-h")))
        usage();

    bool noMultithread = args.contains(QLatin1String("-no-multithread"));

    QString fontFile;
    QString destDir;
    for (int i = 0; i < args.count(); ++i) {
        QString a = args.at(i);
        if (!a.startsWith('-') && QFileInfo(a).exists())
            fontFile = a;
        if (a == QLatin1String("-d"))
            destDir = args.at(++i);
    }

    // Load the font
    int fontID = QFontDatabase::addApplicationFont(fontFile);
    if (fontID == -1) {
        qWarning("Error: Invalid font file.");
        qWarning(" ");
        exit(1);
    }

    // Generate distance-fields for all families and all styles provided by the font file
    QFontDatabase fontDatabase;
    QStringList families = QFontDatabase::applicationFontFamilies(fontID);
    int famCount = families.count();
    for (int i = 0; i < famCount; ++i) {
        QStringList styles = fontDatabase.styles(families.at(i));
        int styleCount = styles.count();
        for (int j = 0; j < styleCount; ++j) {
            QFont font = fontDatabase.font(families.at(i), styles.at(j), 10); // point size is ignored
            generateDistanceFieldForFont(font, destDir, !noMultithread);
        }
    }

    return 0;
}