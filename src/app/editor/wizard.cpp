#include "app/editor/wizard.h"

#include "core/archive.h"
#include "core/convert.h"
#include "core/platform.h"

#include <QBuffer>
#include <QCheckBox>
#include <QCoreApplication>
#include <QCollator>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>

using lunii::ActionNode;
using lunii::StageNode;
using lunii::StudioPack;

static const QStringList AUDIO_EXT = {"mp3", "ogg", "oga", "wav", "m4a", "flac", "opus", "aac"};

// Piper : installation Linux de cette machine (venv), ou piper(.exe) à côté du programme / dans le PATH
static QString piperDir()
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/piper";
#else
    return QDir::homePath() + "/.local/share/piper";
#endif
}

static QString piperBinary()
{
    return lunii::findTool("piper", {piperDir() + "/venv/bin", piperDir()});
}

static QStringList piperVoices()
{
    QStringList out;
    QDirIterator it(piperDir() + "/voices", {"*.onnx"}, QDir::Files);
    while (it.hasNext())
        out << it.next();
    out.sort();
    return out;
}

// Titre lisible : sans numéro de piste ni « Pt 1 » ; tri naturel des noms
static QString cleanTitle(QString name)
{
    name.remove(QRegularExpression(R"(^\s*\d+[\s._-]+)"));
    name.replace('_', ' ');
    return name.trimmed();
}

static void naturalSort(QStringList &l)
{
    QCollator c;
    c.setNumericMode(true);
    std::sort(l.begin(), l.end(), [&](const QString &a, const QString &b) {
        return c.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    });
}

PackWizard::PackWizard(const QString &spillDir, QWidget *parent) : QDialog(parent), m_spillDir(spillDir)
{
    setWindowTitle(tr("Assistant — nouveau pack"));
    resize(820, 600);
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Chaque ligne devient une histoire. Un zip ou un dossier = une histoire en plusieurs parties, "
                                "réunies dans l'ordre des noms. Le pack créé : couverture → choix (thèmes si besoin) → "
                                "titre de l'histoire → histoire, avec retour au menu à la fin."),
                             this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto *form = new QFormLayout;
    m_title = new QLineEdit(tr("Mes histoires"), this);
    form->addRow(tr("Titre du pack :"), m_title);
    lay->addLayout(form);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({tr("Titre (modifiable)"), tr("Parties"), tr("Image")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(m_table, 1);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (it->column() == 0 && it->row() < m_stories.size())
            m_stories[it->row()].title = it->text();
    });

    auto *btns = new QHBoxLayout;
    auto *addF = new QPushButton(QIcon::fromTheme("list-add"), tr("Fichiers audio…"), this);
    auto *addZ = new QPushButton(QIcon::fromTheme("archive-insert"), tr("Zips ou dossiers…"), this);
    auto *img = new QPushButton(QIcon::fromTheme("insert-image"), tr("Image…"), this);
    auto *up = new QPushButton(QIcon::fromTheme("go-up"), QString(), this);
    auto *down = new QPushButton(QIcon::fromTheme("go-down"), QString(), this);
    auto *rem = new QPushButton(QIcon::fromTheme("list-remove"), tr("Retirer"), this);
    for (auto *b : {addF, addZ, img, up, down, rem})
        btns->addWidget(b);
    btns->addStretch();
    lay->addLayout(btns);

    auto *opts = new QFormLayout;
    m_groupSize = new QSpinBox(this);
    m_groupSize->setRange(0, 20);
    m_groupSize->setValue(5);
    m_groupSize->setSpecialValueText(tr("un seul menu"));
    m_groupSize->setSuffix(tr(" histoires par thème"));
    opts->addRow(tr("Regroupement :"), m_groupSize);
    auto *voiceRow = new QHBoxLayout;
    m_tts = new QCheckBox(tr("Annoncer les titres (Piper)"), this);
    m_voice = new QComboBox(this);
    for (const QString &v : piperVoices())
        m_voice->addItem(QFileInfo(v).completeBaseName(), v);
    m_speaker = new QSpinBox(this);
    m_speaker->setPrefix(tr("voix n° "));
    m_speaker->setRange(0, 200);
    const QSettings s;
    m_voice->setCurrentIndex(qMax(0, m_voice->findText(s.value("editor/piperVoice", "fr_FR-upmc-medium").toString())));
    m_speaker->setValue(s.value("editor/piperSpeaker", 1).toInt());
    const bool piper = !piperBinary().isEmpty() && m_voice->count() > 0;
    m_tts->setChecked(piper);
    m_tts->setEnabled(piper);
    if (!piper)
        m_tts->setToolTip(tr("Piper introuvable (%1)").arg(piperDir()));
    voiceRow->addWidget(m_tts);
    voiceRow->addWidget(m_voice, 1);
    voiceRow->addWidget(m_speaker);
    opts->addRow(tr("Voix :"), voiceRow);
    lay->addLayout(opts);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Créer le pack"));
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &PackWizard::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(addF, &QPushButton::clicked, this, &PackWizard::addFiles);
    connect(addZ, &QPushButton::clicked, this, &PackWizard::addContainers);
    connect(rem, &QPushButton::clicked, this, &PackWizard::removeSelected);
    connect(up, &QPushButton::clicked, this, [this]() { moveSelected(-1); });
    connect(down, &QPushButton::clicked, this, [this]() { moveSelected(1); });
    connect(img, &QPushButton::clicked, this, [this]() {
        const int r = m_table->currentRow();
        if (r < 0)
            return;
        const QString f = QFileDialog::getOpenFileName(this, tr("Image de l'histoire"), {}, tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (!f.isEmpty()) {
            m_stories[r].image = f;
            refresh();
        }
    });
}

void PackWizard::addFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Fichiers audio (une histoire chacun)"), {},
                                                      tr("Audio (*.mp3 *.ogg *.oga *.wav *.m4a *.flac *.opus *.aac)"));
    naturalSort(files);
    for (const QString &f : files)
        m_stories << StoryInput{cleanTitle(QFileInfo(f).completeBaseName()), {f}, {}};
    refresh();
}

void PackWizard::addContainers()
{
    QFileDialog dlg(this, tr("Zips ou dossiers (une histoire chacun)"));
    dlg.setFileMode(QFileDialog::ExistingFiles);
    dlg.setNameFilter(tr("Zips (*.zip);;Tous les fichiers (*)"));
    dlg.setOption(QFileDialog::DontUseNativeDialog, false);
    // un dossier peut aussi être choisi via le bouton dédié
    QMessageBox choice(QMessageBox::Question, tr("Ajouter"), tr("Que voulez-vous ajouter ?"), QMessageBox::NoButton, this);
    auto *zips = choice.addButton(tr("Des fichiers zip"), QMessageBox::AcceptRole);
    auto *dir = choice.addButton(tr("Un dossier"), QMessageBox::AcceptRole);
    choice.addButton(QMessageBox::Cancel);
    choice.exec();
    QStringList containers;
    if (choice.clickedButton() == zips) {
        if (dlg.exec())
            containers = dlg.selectedFiles();
    } else if (choice.clickedButton() == dir) {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Dossier d'une histoire"));
        if (!d.isEmpty())
            containers << d;
    }
    naturalSort(containers);
    for (const QString &c : containers)
        m_stories << StoryInput{cleanTitle(QFileInfo(c).completeBaseName()), {c}, {}};
    refresh();
}

void PackWizard::removeSelected()
{
    const int r = m_table->currentRow();
    if (r >= 0) {
        m_stories.removeAt(r);
        refresh();
    }
}

void PackWizard::moveSelected(int d)
{
    const int r = m_table->currentRow();
    if (r < 0 || r + d < 0 || r + d >= m_stories.size())
        return;
    m_stories.move(r, r + d);
    refresh();
    m_table->selectRow(r + d);
}

void PackWizard::refresh()
{
    m_table->blockSignals(true);
    m_table->setRowCount(int(m_stories.size()));
    for (int i = 0; i < m_stories.size(); ++i) {
        const StoryInput &s = m_stories[i];
        m_table->setItem(i, 0, new QTableWidgetItem(s.title));
        const QString src = s.parts.size() == 1 ? QFileInfo(s.parts.first()).fileName() : tr("%n fichiers", nullptr, int(s.parts.size()));
        auto *p = new QTableWidgetItem(src);
        p->setFlags(p->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 1, p);
        auto *im = new QTableWidgetItem(s.image.isEmpty() ? tr("jaquette intégrée ou titre") : QFileInfo(s.image).fileName());
        im->setFlags(im->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 2, im);
    }
    m_table->blockSignals(false);
}

// ------------------------------------------------------------------ génération

namespace {

struct Gen {
    QString piper, voice, speaker, spill;
    bool tts = false;
    QTemporaryDir tmp;
    StudioPack pack;
    QString error;

    QString asset(const QByteArray &data, const QString &ext)
    {
        const QString name = QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex() + "." + ext;
        pack.putAsset(name, data, spill);
        return name;
    }

    static QByteArray run(const QString &prog, const QStringList &args, const QByteArray &in = {}, QString *err = nullptr)
    {
        QProcess p;
        p.start(prog, args);
        if (!p.waitForStarted())
            return {};
        if (!in.isEmpty())
            p.write(in);
        p.closeWriteChannel();
        p.waitForFinished(-1);
        if (p.exitCode() != 0) {
            if (err)
                *err = QString::fromUtf8(p.readAllStandardError()).trimmed();
            return {};
        }
        return p.readAllStandardOutput();
    }

    // parties d'une histoire : fichiers audio d'un zip ou d'un dossier, triés naturellement
    QStringList expand(const QString &src, int n)
    {
        const QFileInfo fi(src);
        QStringList parts;
        if (fi.isDir()) {
            QDirIterator it(src, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                if (AUDIO_EXT.contains(QFileInfo(f).suffix().toLower()))
                    parts << f;
            }
        } else if (fi.suffix().toLower() == "zip") {
            const QString dir = tmp.path() + QString("/src%1").arg(n);
            QDir().mkpath(dir);
            lunii::ArchiveReader ar(src);
            ar.forEach([&](const lunii::ArchiveEntry &e, const QByteArray &d) {
                if (!AUDIO_EXT.contains(QFileInfo(e.name).suffix().toLower()))
                    return true;
                const QString out = dir + "/" + QFileInfo(e.name).fileName();
                QFile f(out);
                if (f.open(QIODevice::WriteOnly))
                    f.write(d);
                parts << out;
                return true;
            });
        } else {
            parts << src;
        }
        naturalSort(parts);
        return parts;
    }

    QByteArray mergeAudio(const QStringList &parts)
    {
        const QString list = tmp.path() + "/concat.txt";
        QFile f(list);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {};
        for (QString p : parts)
            f.write("file '" + p.replace("'", "'\\''").toUtf8() + "'\n");
        f.close();
        return run(lunii::ffmpegPath(), {"-v", "error", "-f", "concat", "-safe", "0", "-i", list, "-map", "0:a",
                                        "-ac", "1", "-ar", "44100", "-b:a", "128k", "-map_metadata", "-1",
                                        "-id3v2_version", "0", "-write_xing", "0", "-f", "mp3", "pipe:1"},
                   {}, &error);
    }

    QImage embeddedCover(const QString &audio)
    {
        QImage img;
        img.loadFromData(run(lunii::ffmpegPath(), {"-v", "error", "-i", audio, "-an", "-frames:v", "1", "-f", "image2pipe",
                                                  "-vcodec", "png", "pipe:1"}));
        return img;
    }

    QImage titleCard(const QString &title, int side = 640)
    {
        QImage img(side, side, QImage::Format_RGB32);
        img.fill(QColor(20, 40, 70));
        QPainter p(&img);
        QFont f;
        f.setPixelSize(side / 14);
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(img.rect().adjusted(side / 16, side / 10, -side / 16, -side / 3), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, title);
        return img;
    }

    // carré -> haut de l'image en 4:3 (titre des jaquettes) -> 320x240
    QString screen(const QImage &src)
    {
        QImage img = src;
        if (img.width() * 3 > img.height() * 4)
            img = img.copy((img.width() - img.height() * 4 / 3) / 2, 0, img.height() * 4 / 3, img.height());
        else
            img = img.copy(0, 0, img.width(), img.width() * 3 / 4);
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.scaled(320, 240, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).save(&b, "PNG");
        return asset(png, "png");
    }

    QImage mosaic(const QList<QImage> &covers, const QString &title)
    {
        QImage cv(320, 240, QImage::Format_RGB32);
        cv.fill(QColor(20, 40, 70));
        QPainter p(&cv);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QFont f;
        f.setBold(true);
        int size = 22;
        f.setPixelSize(size);
        while (QFontMetrics(f).horizontalAdvance(title) > 304 && size > 10)
            f.setPixelSize(--size);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRect(8, 10, 304, 30), Qt::AlignCenter, title);
        const int n = int(covers.size());
        if (n == 0)
            return cv;
        const int gap = 4, AW = 312, AH = 186, AY = 48;
        int best = 1, side = 0;
        for (int c = 1; c <= n; ++c) {
            const int r = (n + c - 1) / c;
            const int s = qMin((AW - gap * (c - 1)) / c, (AH - gap * (r - 1)) / r);
            if (s > side)
                side = s, best = c;
        }
        const int rows = (n + best - 1) / best;
        const int y0 = AY + (AH - (rows * side + gap * (rows - 1))) / 2;
        for (int r = 0; r < rows; ++r) {
            const int inRow = qMin(best, n - r * best);
            const int x0 = (320 - (inRow * side + gap * (inRow - 1))) / 2;
            for (int k = 0; k < inRow; ++k)
                p.drawImage(QRect(x0 + k * (side + gap), y0 + r * (side + gap), side, side), covers[r * best + k]);
        }
        return cv;
    }

    QString speak(const QString &text)
    {
        if (!tts)
            return {};
        const QString wav = tmp.path() + "/tts.wav";
        QStringList args{"-m", voice, "-f", wav};
        if (!speaker.isEmpty())
            args << "--speaker" << speaker;
        run(piper, args, text.toUtf8());
        const QByteArray mp3 = run(lunii::ffmpegPath(), {"-v", "error", "-i", wav, "-af", "adelay=500,apad=pad_dur=0.5",
                                                         "-ac", "1", "-ar", "44100", "-b:a", "128k", "-map_metadata", "-1",
                                                         "-id3v2_version", "0", "-f", "mp3", "pipe:1"});
        return mp3.isEmpty() ? QString() : asset(mp3, "mp3");
    }
};

StageNode stage(const QString &name, const QPointF &pos, const QString &image, const QString &audio,
                bool wheel, bool ok, bool home, bool pause, bool autoplay)
{
    StageNode n;
    n.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    n.name = name;
    n.pos = pos;
    n.image = image;
    n.audio = audio;
    n.ctrl = {wheel, ok, home, pause, autoplay};
    return n;
}

} // namespace

void PackWizard::accept()
{
    if (m_stories.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Ajoutez au moins une histoire."));
        return;
    }
    if (lunii::ffmpegPath().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("ffmpeg est nécessaire pour préparer les sons."));
        return;
    }
    QSettings s;
    s.setValue("editor/piperVoice", m_voice->currentText());
    s.setValue("editor/piperSpeaker", m_speaker->value());

    auto gen = std::make_shared<Gen>();
    gen->tts = m_tts->isChecked();
    gen->spill = m_spillDir;
    gen->piper = piperBinary();
    gen->voice = m_voice->currentData().toString();
    gen->speaker = QString::number(m_speaker->value());
    const QList<StoryInput> stories = m_stories;
    const QString title = m_title->text().trimmed().isEmpty() ? tr("Mes histoires") : m_title->text().trimmed();
    const int groupSize = m_groupSize->value();
    const QString coverTts = groupSize > 0 && stories.size() > groupSize
                                 ? tr("%1. Tourne la molette pour choisir un thème, puis appuie sur OK.").arg(title)
                                 : tr("%1. Tourne la molette pour choisir ton histoire, puis appuie sur OK.").arg(title);

    QProgressDialog progress(tr("Préparation des histoires…"), tr("Annuler"), 0, int(stories.size()) + 1, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    connect(&progress, &QProgressDialog::canceled, this, [cancelled]() { *cancelled = true; });

    QFutureWatcher<bool> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([gen, stories, title, groupSize, coverTts, cancelled, &progress]() {
        StudioPack &p = gen->pack;
        p.title = title;
        p.description = QStringList([&] {
            QStringList t;
            for (const auto &s : stories)
                t << s.title;
            return t;
        }()).join(" ; ");
        struct Built { QString title, image, tts, audio; QImage cover; };
        QList<Built> built;
        for (int i = 0; i < stories.size(); ++i) {
            if (*cancelled)
                return false;
            QMetaObject::invokeMethod(&progress, "setValue", Qt::QueuedConnection, Q_ARG(int, i));
            const StoryInput &in = stories[i];
            QStringList parts;
            for (const QString &src : in.parts)
                parts << gen->expand(src, i);
            if (parts.isEmpty()) {
                gen->error = QObject::tr("« %1 » : aucun fichier audio").arg(in.title);
                return false;
            }
            const QByteArray audio = gen->mergeAudio(parts);
            if (audio.isEmpty()) {
                gen->error = QObject::tr("« %1 » : %2").arg(in.title, gen->error);
                return false;
            }
            QImage cover = in.image.isEmpty() ? gen->embeddedCover(parts.first()) : QImage(in.image);
            if (cover.isNull())
                cover = gen->titleCard(in.title);
            built << Built{in.title, gen->screen(cover), gen->speak(in.title), gen->asset(audio, "mp3"),
                           cover.scaled(240, 240, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)};
        }

        // groupes (thèmes) de groupSize histoires
        QList<QList<int>> groups;
        if (groupSize > 0 && built.size() > groupSize) {
            for (int i = 0; i < built.size(); i += groupSize) {
                QList<int> g;
                for (int k = i; k < qMin(i + groupSize, int(built.size())); ++k)
                    g << k;
                groups << g;
            }
        } else {
            QList<int> g;
            for (int k = 0; k < built.size(); ++k)
                g << k;
            groups << g;
        }

        QList<QImage> allCovers;
        for (const Built &b : built)
            allCovers << b.cover;
        QByteArray thumb;
        {
            QBuffer tb(&thumb);
            tb.open(QIODevice::WriteOnly);
            gen->mosaic(allCovers, title).scaled(640, 480).save(&tb, "PNG");
        }
        p.thumbnail = thumb;
        QImage coverImg = gen->mosaic(allCovers, title);
        QByteArray coverPng;
        {
            QBuffer cb(&coverPng);
            cb.open(QIODevice::WriteOnly);
            coverImg.save(&cb, "PNG");
        }
        const bool themed = groups.size() > 1;
        ActionNode root;
        root.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        root.name = themed ? QObject::tr("Choix du thème") : QObject::tr("Choix de l'histoire");
        root.pos = {250, 0};
        StageNode cover = stage(title, {0, 0}, gen->asset(coverPng, "png"), gen->speak(coverTts), true, true, false, false, false);
        cover.type = "cover";
        cover.ok = {root.id, 0};
        p.stages << cover;
        p.actions << root;

        int y = 0;
        for (int gi = 0; gi < groups.size(); ++gi) {
            const QList<int> &g = groups[gi];
            ActionNode list = root;   // sans thème : la liste des histoires est le choix racine
            int rootIndex = 0;
            if (themed) {
                const QString name = QObject::tr("Histoires %1 à %2").arg(g.first() + 1).arg(g.last() + 1);
                QList<QImage> covers;
                for (int k : g)
                    covers << built[k].cover;
                QByteArray png;
                QBuffer b(&png);
                b.open(QIODevice::WriteOnly);
                gen->mosaic(covers, name).save(&b, "PNG");
                list = ActionNode{};
                list.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                list.name = name + QObject::tr(" (choix)");
                list.pos = {750, qreal(y)};
                StageNode th = stage(name, {500, qreal(y)}, gen->asset(png, "png"), gen->speak(name), true, true, true, false, false);
                th.ok = {list.id, 0};
                p.actions[0].options << th.uuid;
                rootIndex = p.actions[0].options.size() - 1;
                p.stages << th;
                p.actions << list;
            }
            const int listIndex = p.actionIndex(list.id);
            for (int si = 0; si < g.size(); ++si) {
                const Built &b = built[g[si]];
                ActionNode play;
                play.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                play.name = b.title + QObject::tr(" (lecture)");
                play.pos = {1250, qreal(y)};
                StageNode menu = stage(b.title, {1000, qreal(y)}, b.image, b.tts, true, true, true, false, false);
                menu.ok = {play.id, 0};
                if (themed)
                    menu.home = {root.id, rootIndex};
                StageNode story = stage(b.title + QObject::tr(" (histoire)"), {1500, qreal(y)}, {}, b.audio, false, false, true, true, true);
                story.ok = {list.id, si};
                story.home = {list.id, si};
                play.options << story.uuid;
                p.actions[listIndex].options << menu.uuid;
                p.stages << menu << story;
                p.actions << play;
                y += 150;
            }
            y += 150;
        }
        QMetaObject::invokeMethod(&progress, "setValue", Qt::QueuedConnection, Q_ARG(int, int(stories.size()) + 1));
        return true;
    }));
    loop.exec();
    progress.close();
    if (!watcher.result()) {
        if (!*cancelled)
            QMessageBox::warning(this, windowTitle(), tr("Création impossible :\n%1").arg(gen->error));
        return;
    }
    m_result = gen->pack;
    QDialog::accept();
}
