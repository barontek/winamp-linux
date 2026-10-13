// media_library.h — Winamp Media Library
#pragma once

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QMenu>
#include <QTreeView>
#include <QFileSystemModel>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QKeyEvent>
#include "constants.h"
#include "compat.h"
#include "translator.h"

// The "gen" window frame is composed from gen.bmp. Region layout follows the
// classic skin spec: two 20px titlebar rows (active then inactive), each split
// into six 25px parts, with sidewalls, bottom corners and a bottom fill tile.
// Coordinates verified against the shipped assets/gen.bmp.
namespace gen {

// Titlebar parts, six 25x20 cells. `fill` parts repeat with the window width.
struct TitlebarPart {
    int x;        // source x in gen.bmp
    bool repeats; // true for the horizontally scalable cells
};
static const TitlebarPart kTitlebarParts[] = {
    {0, false},    // top left corner
    {26, false},   // fixed left-end graphic
    {52, true},    // main title container (text is drawn over this)
    {78, false},   // fixed right-end graphic
    {104, true},   // horizontal scale fill
    {130, false},  // top right corner, carries the close button
};
static const int kTitlebarPartW = 25;
static const int kTitlebarH = 20;
static const int kTitlebarActiveY = 0;
static const int kTitlebarInactiveY = 21;

// Bottom edge and sidewalls.
static const int kBottomH = 14;
static const int kBottomLeftX = 0,   kBottomLeftY = 42;
static const int kBottomRightX = 0,  kBottomRightY = 57;
static const int kBottomLeftW = 125;
static const int kBottomFillX = 127, kBottomFillY = 72, kBottomFillW = 25;

static const int kMiddleLeftX = 127, kMiddleLeftY = 42, kMiddleLeftW = 11, kMiddleLeftH = 29;
static const int kMiddleRightX = 139, kMiddleRightY = 42, kMiddleRightW = 8, kMiddleRightH = 29;

static const int kCloseSpriteX = 148, kCloseSpriteY = 42, kCloseSpriteW = 9, kCloseSpriteH = 9;

// Titlebar font. Highlighted row sits above the normal row; both share widths.
// Offsets come from the delimiter columns in gen.bmp. The W/X source cell has no
// full delimiter between the two glyphs, so that cell is split explicitly.
static const int kFontGlyphX[26] = {1, 8, 16, 24, 31, 38, 45, 53, 60, 65,  72,  80,  86,
                                    95, 102, 109, 116, 124, 132, 139, 145, 152, 159, 168, 176, 183};
static const int kFontGlyphW[26] = {6, 7, 7, 6, 6, 6, 7, 6, 4, 6, 7, 5, 8,
                                    6, 6,  6, 7,  7,  6,  5,  6,  6,  8,  7,  6,  5};
static const int kFontHighlightY = 88;
static const int kFontNormalY = 96;
static const int kFontH = 7;
static const int kSpaceAdvance = 3;

// genex.bmp y=0 palette row (see the skin spec for gen windows).
enum PaletteColor {
    ItemBackground = 48,
    ItemForeground = 50,
    WindowBackground = 52,
    ButtonText = 54,
    WindowText = 56,
    Divider = 58,
    Selection = 60,
    ListHeaderBackground = 62,
    ListHeaderText = 64,
    Scrollbar1 = 74,
    Scrollbar2 = 76,
    ScrollbarPressed1 = 78,
    ScrollbarPressed2 = 80,
    ScrollbarDead = 82,
};

} // namespace gen

class MediaLibraryWindow : public QWidget {
    Q_OBJECT

public:
    MediaLibraryWindow(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowTitle(TR("win.library.title", "Winamp Library"));
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_DeleteOnClose, false);
        setMinimumSize(kMinW, kMinH);
        resize(275, 300);

        loadSkin();

        treeView = new QTreeView(this);
        fileModel = new QFileSystemModel(this);

        QString musicPath = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (musicPath.isEmpty() || !QDir(musicPath).exists()) {
            musicPath = QDir::homePath();
        }
        fileModel->setRootPath(musicPath);

        QStringList filters;
        filters << "*.mp3" << "*.flac" << "*.ogg" << "*.wav" << "*.m4a" << "*.aac"
                << "*.wma" << "*.opus" << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.webm";
        fileModel->setNameFilters(filters);
        fileModel->setNameFilterDisables(false);

        treeView->setModel(fileModel);
        treeView->setRootIndex(fileModel->index(musicPath));
        // The classic media library is a plain name tree.
        for (int c = 1; c < fileModel->columnCount(); ++c) treeView->hideColumn(c);
        treeView->setHeaderHidden(true); // gen windows have no column headers
        treeView->setAnimated(false);
        treeView->setIndentation(12);
        treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        treeView->setFrameShape(QFrame::NoFrame);
        applyTreeStyle();

        connect(treeView, &QTreeView::doubleClicked, this, &MediaLibraryWindow::onItemDoubleClicked);

        updateLayout();
    }

    void setPlaylistWindow(QObject *pl) { playlistWindow = pl; }

signals:
    void addToPlaylist(const QString &filePath);
    void addToPlaylistRecursive(const QString &dirPath);
    // Emitted instead of addToPlaylist for a double-clicked file, so the
    // receiving window can add and start playback in one step.
    void playFile(const QString &filePath);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const int w = width();
        const int h = height();

        if (genBitmap.isNull()) {
            p.fillRect(rect(), bgColor);
            p.setPen(fgColor);
            p.drawText(rect(), Qt::AlignCenter, tr("Winamp Library"));
            return;
        }

        // Body fill behind everything.
        p.fillRect(0, 0, w, h, bgColor);

        drawTitlebar(p, w, isActiveWindow());
        drawSidewalls(p, w, h);
        drawBottom(p, w, h);
        drawTitle(p, w, isActiveWindow());

        if (closePressed && !closeRect().isEmpty()) {
            p.drawPixmap(closeRect().x(), closeRect().y(), genBitmap,
                         gen::kCloseSpriteX, gen::kCloseSpriteY,
                         gen::kCloseSpriteW, gen::kCloseSpriteH);
        }
    }

    void resizeEvent(QResizeEvent*) override { updateLayout(); }

    void changeEvent(QEvent *e) override {
        QWidget::changeEvent(e);
        // Active/inactive titlebars are different bitmaps.
        if (e->type() == QEvent::ActivationChange) update();
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) return;
        const QPoint pos = event->pos();

        if (closeRect().contains(pos)) {
            closePressed = true;
            update();
            return;
        }

        ResizeEdge edge = hitTestResize(pos);
        if (edge != NoEdge) {
            isResizing = true;
            resizeEdge = edge;
            resizeStartPos = waMouseGlobalPos(event);
            resizeStartGeometry = geometry();
            return;
        }

        // Titlebar drag. Wayland ignores client-side move(), so let the
        // compositor own the drag when it can and fall back otherwise.
        if (pos.y() < gen::kTitlebarH) {
            if (!waStartSystemDrag(this)) {
                isDragging = true;
                dragStartPos = waMouseGlobalPos(event) - frameGeometry().topLeft();
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (isResizing) {
            QPoint delta = waMouseGlobalPos(event) - resizeStartPos;
            QRect newGeom = resizeStartGeometry;

            if (resizeEdge & LeftEdge) {
                int newX = resizeStartGeometry.x() + delta.x();
                int newWidth = resizeStartGeometry.width() - delta.x();
                if (newWidth >= minimumWidth()) {
                    newGeom.setX(newX);
                    newGeom.setWidth(newWidth);
                }
            }
            if (resizeEdge & RightEdge)
                newGeom.setWidth(qMax(minimumWidth(), resizeStartGeometry.width() + delta.x()));
            if (resizeEdge & TopEdge) {
                int newY = resizeStartGeometry.y() + delta.y();
                int newHeight = resizeStartGeometry.height() - delta.y();
                if (newHeight >= minimumHeight()) {
                    newGeom.setY(newY);
                    newGeom.setHeight(newHeight);
                }
            }
            if (resizeEdge & BottomEdge)
                newGeom.setHeight(qMax(minimumHeight(), resizeStartGeometry.height() + delta.y()));

            setGeometry(newGeom);
            return;
        }

        if (isDragging) {
            move(waMouseGlobalPos(event) - dragStartPos);
            return;
        }

        updateCursorForEdge(hitTestResize(event->pos()));
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) return;
        if (closePressed) {
            closePressed = false;
            update();
            if (closeRect().contains(event->pos())) close();
            return;
        }
        isDragging = false;
        isResizing = false;
        resizeEdge = NoEdge;
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override {
        Q_UNUSED(event);
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
        } else {
            QWidget::keyPressEvent(event);
        }
    }

private slots:
    void onItemDoubleClicked(const QModelIndex &index) {
        QString path = fileModel->filePath(index);
        QFileInfo info(path);

        if (info.isFile()) {
            emit playFile(path);
        } else if (info.isDir()) {
            emit addToPlaylistRecursive(path);
        }
    }

private:
    enum ResizeEdge {
        NoEdge = 0,
        LeftEdge = 1,
        RightEdge = 2,
        TopEdge = 4,
        BottomEdge = 8
    };

    // A 25px tileable cell: the fixed cells are blitted once, the fill cells
    // repeat. Both titlebar rows share the same part layout.
    //
    // Four of the six parts are fixed width. Of the two fill parts, the centre
    // fill is the title container the text is drawn over, so it absorbs the
    // slack; the right-hand fill keeps a single tile.
    void drawTitlebar(QPainter &p, int w, bool active) {
        const int srcY = active ? gen::kTitlebarActiveY : gen::kTitlebarInactiveY;
        const int P = gen::kTitlebarPartW;
        const int fixed = P * 4; // four non-repeating parts
        const int flexible = qMax(2 * P, w - fixed);

        int centerTiles = qMax(1, (flexible - P) / P);
        int rightTiles = qMax(1, (flexible - centerTiles * P) / P);

        int x = 0;
        for (int i = 0; i < 6; ++i) {
            const gen::TitlebarPart &part = gen::kTitlebarParts[i];
            const int n = part.repeats ? (i == 2 ? centerTiles : rightTiles) : 1;

            for (int t = 0; t < n && x < w; ++t) {
                p.drawPixmap(x, 0, genBitmap, part.x, srcY, P, gen::kTitlebarH);
                x += P;
            }
        }
    }

    void drawSidewalls(QPainter &p, int w, int h) {
        const int top = gen::kTitlebarH;
        const int bottom = h - gen::kBottomH;
        for (int y = top; y < bottom; y += gen::kMiddleLeftH) {
            int tileH = qMin(gen::kMiddleLeftH, bottom - y);
            p.drawPixmap(0, y, genBitmap, gen::kMiddleLeftX, gen::kMiddleLeftY,
                         gen::kMiddleLeftW, tileH);
            p.drawPixmap(w - gen::kMiddleRightW, y, genBitmap, gen::kMiddleRightX, gen::kMiddleRightY,
                         gen::kMiddleRightW, tileH);
        }
    }

    void drawBottom(QPainter &p, int w, int h) {
        const int y = h - gen::kBottomH;
        p.drawPixmap(0, y, genBitmap, gen::kBottomLeftX, gen::kBottomLeftY,
                     gen::kBottomLeftW, gen::kBottomH);
        p.drawPixmap(w - gen::kBottomLeftW, y, genBitmap, gen::kBottomRightX, gen::kBottomRightY,
                     gen::kBottomLeftW, gen::kBottomH);

        // Tile the middle; clip the last tile if it overruns the right corner.
        int x = gen::kBottomLeftW;
        const int right = w - gen::kBottomLeftW;
        while (x < right) {
            int tileW = qMin(gen::kBottomFillW, right - x);
            p.drawPixmap(x, y, genBitmap, gen::kBottomFillX, gen::kBottomFillY,
                         tileW, gen::kBottomH);
            x += gen::kBottomFillW;
        }
    }

    void drawTitle(QPainter &p, int w, bool active) {
        const QString title = TR("win.library.title", "Winamp Library");
        const int srcY = active ? gen::kFontHighlightY : gen::kFontNormalY;
        const int P = gen::kTitlebarPartW;
        const int y = (gen::kTitlebarH - gen::kFontH) / 2;

        // The text belongs over the centre fill, which starts after the two
        // fixed left-hand parts and ends before the right-hand fill.
        const int flexible = qMax(2 * P, w - P * 4);
        const int centerTiles = qMax(1, (flexible - P) / P);
        const int left = P * 2;
        const int avail = qMax(0, centerTiles * P - P); // leave a tile of padding

        int x = left + 4;
        const int limit = left + avail;
        for (const QChar &ch : title) {
            const int idx = ch.toUpper().unicode() - 'A';
            const int gw = (idx >= 0 && idx < 26) ? gen::kFontGlyphW[idx] : gen::kSpaceAdvance;
            if (x + gw > limit) break; // never spill out of the title container
            if (idx >= 0 && idx < 26) {
                p.drawPixmap(x, y, genBitmap, gen::kFontGlyphX[idx], srcY, gw, gen::kFontH);
            }
            x += gw;
        }
    }

    ResizeEdge hitTestResize(QPoint pos) {
        const int edgeSize = 4;
        const int w = width();
        const int h = height();
        ResizeEdge edge = NoEdge;

        if (pos.x() < edgeSize) edge = (ResizeEdge)(edge | LeftEdge);
        else if (pos.x() > w - edgeSize) edge = (ResizeEdge)(edge | RightEdge);

        if (pos.y() < edgeSize) edge = (ResizeEdge)(edge | TopEdge);
        else if (pos.y() > h - edgeSize) edge = (ResizeEdge)(edge | BottomEdge);

        return edge;
    }

    void updateCursorForEdge(ResizeEdge edge) {
        if (edge == NoEdge) {
            setCursor(Qt::ArrowCursor);
        } else if (edge == (LeftEdge | TopEdge) || edge == (RightEdge | BottomEdge)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (edge == (RightEdge | TopEdge) || edge == (LeftEdge | BottomEdge)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if (edge & (LeftEdge | RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else if (edge & (TopEdge | BottomEdge)) {
            setCursor(Qt::SizeVerCursor);
        }
    }

    void loadSkin() {
        QString appDir = QCoreApplication::applicationDirPath();
        QStringList skinPaths = {"skins/default", "assets"};
        for (const QString &root : winampDataRoots(appDir)) {
            skinPaths << (root + "/skins/default") << (root + "/resource");
        }

        for (const QString &basePath : skinPaths) {
            const QString genPath = basePath + "/gen.bmp";
            const QString genexPath = basePath + "/genex.bmp";
            if (QFile::exists(genPath) && genBitmap.isNull()) genBitmap = QPixmap(genPath);
            if (QFile::exists(genexPath) && genexBitmap.isNull()) genexBitmap = QPixmap(genexPath);
        }

        if (!genexBitmap.isNull()) {
            QImage img = genexBitmap.toImage();
            auto read = [&](int x) {
                return (img.width() > x && img.height() > 0) ? img.pixelColor(x, 0) : QColor();
            };
            bgColor = read(gen::WindowBackground);
            fgColor = read(gen::WindowText);
            itemBg = read(gen::ItemBackground);
            itemFg = read(gen::ItemForeground);
            selectionColor = read(gen::Selection);
            sbGroove = read(gen::Scrollbar1);
            sbHandle = read(gen::Scrollbar2);
            sbHandleActive = read(gen::ScrollbarPressed1);
        }

        if (!bgColor.isValid()) bgColor = QColor(36, 36, 60);
        if (!fgColor.isValid()) fgColor = QColor(255, 255, 255);
        if (!itemBg.isValid()) itemBg = bgColor;
        if (!itemFg.isValid()) itemFg = fgColor;
        if (!selectionColor.isValid()) selectionColor = QColor(0, 0, 198);
        if (!sbGroove.isValid()) sbGroove = bgColor;
        if (!sbHandle.isValid()) sbHandle = fgColor;
        if (!sbHandleActive.isValid()) sbHandleActive = selectionColor;
    }

    void applyTreeStyle() {
        treeView->setStyleSheet(QString(
            "QTreeView { background-color: %1; color: %2; border: none; }"
            "QTreeView::item { background-color: %1; color: %2; }"
            "QTreeView::item:selected { background-color: %3; color: %2; }"
            "QTreeView::item:hover { background-color: %3; }"
            "QScrollBar:vertical { background: %4; width: 12px; border: none; }"
            "QScrollBar:horizontal { background: %4; height: 12px; border: none; }"
            "QScrollBar::handle:vertical { background: %5; min-height: 20px; border: none; }"
            "QScrollBar::handle:horizontal { background: %5; min-width: 20px; border: none; }"
            "QScrollBar::handle:hover { background: %6; }"
            "QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: %6; }"
            "QScrollBar::add-line, QScrollBar::sub-line { height: 0px; width: 0px; border: none; }"
            "QScrollBar::add-page, QScrollBar::sub-page { background: none; }")
            .arg(itemBg.name(), itemFg.name(), selectionColor.name(),
                 sbGroove.name(), sbHandle.name(), sbHandleActive.name()));
    }

    void updateLayout() {
        if (!treeView) return;
        const QRect area = contentRect();
        treeView->setGeometry(area);
    }

    QRect contentRect() const {
        return QRect(gen::kMiddleLeftW,
                     gen::kTitlebarH,
                     qMax(0, width() - gen::kMiddleLeftW - gen::kMiddleRightW),
                     qMax(0, height() - gen::kTitlebarH - gen::kBottomH));
    }

    QRect closeRect() const {
        // The close button lives inside the top-right corner piece.
        return QRect(width() - gen::kTitlebarPartW + 13, 4,
                     gen::kCloseSpriteW + 2, gen::kCloseSpriteH + 2);
    }

    static const int kMinW = 250;
    static const int kMinH = 120;

    QTreeView *treeView = nullptr;
    QFileSystemModel *fileModel = nullptr;
    QPixmap genBitmap;
    QPixmap genexBitmap;
    QColor bgColor;
    QColor fgColor;
    QColor itemBg;
    QColor itemFg;
    QColor selectionColor;
    QColor sbGroove;
    QColor sbHandle;
    QColor sbHandleActive;

    bool isDragging = false;
    bool isResizing = false;
    bool closePressed = false;
    ResizeEdge resizeEdge = NoEdge;
    QPoint dragStartPos;
    QPoint resizeStartPos;
    QRect resizeStartGeometry;

    QObject *playlistWindow = nullptr;
};
