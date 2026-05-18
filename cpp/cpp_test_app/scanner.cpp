// Document Scanner Test Suite — Qt6 QMainWindow application.
// Replaces all OpenCV HighGUI (imshow / namedWindow / createTrackbar / waitKey)
// with a proper Qt6 widget hierarchy.
//
// Layout:
//   ┌────────┬───────────────────────────────┬──────────────────────┐
//   │ Image  │  [Source][Edges][Result][⟺]   │  Algorithm Pipeline  │
//   │  List  │                               │  ─────────────────   │
//   │        │   ImageDisplayWidget          │  [ + Add Step ]      │
//   │        │   (zoom / pan)                │  ─────────────────   │
//   │        │                               │  ☑ Whitepaper ↑↓✕   │
//   │        │                               │  ─────────────────   │
//   │        │                               │  Parameters          │
//   │        │                               │  slider controls     │
//   └────────┴───────────────────────────────┴──────────────────────┘
//   Status bar

#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core/utility.hpp>

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QSplitter>
#include <QListWidget>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QTimer>
#include <QPixmap>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QFrame>
#include <QScrollBar>
#include <QMenu>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QSizePolicy>
#include <QCursor>
#include <QStyleOption>

#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include <DocumentDetector.h>
#include <ColorSimplificationTransform.h>
#include <WhitePaperTransform.h>
#include <WhitePaperTransform2.h>
#include <Utils.h>
#include <jsoncons/json.hpp>

using namespace cv;
using namespace std;

// ============================================================
//  Geometry helpers  (identical to original logic)
// ============================================================

static bool compareXCords(Point p1, Point p2) { return p1.x < p2.x; }
static bool compareYCords(Point p1, Point p2) { return p1.y < p2.y; }
static bool comparePairDist(pair<Point,Point> a, pair<Point,Point> b) {
    return norm(a.first - a.second) < norm(b.first - b.second);
}
static double ptDist(Point p1, Point p2) {
    double dx = p1.x - p2.x, dy = p1.y - p2.y;
    return sqrt(dx*dx + dy*dy);
}
static void orderPoints(vector<Point> inpts, vector<Point>& ordered) {
    sort(inpts.begin(), inpts.end(), compareXCords);
    vector<Point> lm(inpts.begin(), inpts.begin()+2);
    vector<Point> rm(inpts.end()-2, inpts.end());
    sort(lm.begin(), lm.end(), compareYCords);
    Point tl(lm[0]), bl(lm[1]);
    vector<pair<Point,Point>> tmp;
    for (auto& p : rm) tmp.push_back({tl, p});
    sort(tmp.begin(), tmp.end(), comparePairDist);
    Point tr(tmp[0].second), br(tmp[1].second);
    ordered = {tl, tr, br, bl};
}
static Mat cropAndWarp(Mat src, vector<cv::Point> pts) {
    int w = (int)ptDist(pts[0], pts[1]);
    int h = (int)ptDist(pts[1], pts[2]);
    if (w <= 0 || h <= 0) return {};
    Mat dst = Mat::zeros(h, w, src.type());
    vector<Point2f> sp = {
        {(float)pts[0].x,(float)pts[0].y},
        {(float)pts[1].x,(float)pts[1].y},
        {(float)pts[3].x,(float)pts[3].y},
        {(float)pts[2].x,(float)pts[2].y}
    };
    vector<Point2f> dp = {{0,0},{(float)w,0},{0,(float)h},{(float)w,(float)h}};
    Mat T = getPerspectiveTransform(sp, dp);
    warpPerspective(src, dst, T, dst.size());
    return dst;
}
static vector<string> loadImagesFromFolder(const string& dir) {
    vector<string> imgs;
    static const vector<string> kExts = {
        ".jpg",".jpeg",".png",".bmp",".tiff",".tif",".webp"
    };
    for (auto& e : filesystem::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        string ext = e.path().extension().string();
        transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (find(kExts.begin(), kExts.end(), ext) != kExts.end())
            imgs.push_back(e.path().string());
    }
    sort(imgs.begin(), imgs.end());
    return imgs;
}

// ============================================================
//  JSONCONS traits
// ============================================================

JSONCONS_N_MEMBER_TRAITS(WhitePaperTransformOptions, 0,
    csBlackPer, csWhitePer, gaussKSize, gaussSigma, gammaValue,
    cbBlackPer, cbWhitePer, dogKSize, dogSigma2);

// ============================================================
//  Helper: cv::Mat → QPixmap
// ============================================================

static QPixmap matToQPixmap(const Mat& mat) {
    if (mat.empty()) return {};
    Mat rgb;
    if (mat.channels() == 1)
        cvtColor(mat, rgb, COLOR_GRAY2RGB);
    else
        cvtColor(mat, rgb, COLOR_BGR2RGB);
    QImage img(rgb.data, rgb.cols, rgb.rows,
               (int)rgb.step, QImage::Format_RGB888);
    return QPixmap::fromImage(img.copy()); // copy so buffer outlives Mat
}

// ============================================================
//  Data structures: algorithm catalogue
// ============================================================

struct AlgoParam {
    QString id;
    QString label;
    double  minVal, maxVal, defaultVal, step;
};

struct AlgoDef {
    QString          id;
    QString          name;
    bool             implemented; // false = placeholder / todo
    QVector<AlgoParam> params;
};

struct PipelineStep {
    AlgoDef              def;
    QMap<QString,double> paramValues;
    bool                 enabled = true;
};

static PipelineStep makeStep(const AlgoDef& def) {
    PipelineStep s;
    s.def     = def;
    s.enabled = true;
    for (const auto& p : def.params)
        s.paramValues[p.id] = p.defaultVal;
    return s;
}

static QVector<AlgoDef> buildCatalog() {
    QVector<AlgoDef> c;

    QVector<AlgoParam> wpParams = {
        {"csBlackPer","Black Percentile",  0,  100, 2,    1  },
        {"csWhitePer","White Percentile",  0,  100, 99.5, 0.5},
        {"gaussKSize","Gauss KSize",        1,  99,  3,    2  },
        {"dogKSize",  "DoG KSize",          1,  99,  15,   2  },
        {"dogSigma1", "DoG Sigma 1",        0,  200, 100,  1  },
        {"dogSigma2", "DoG Sigma 2",        0,  100, 0,    1  },
    };

    c.push_back({"whitepaper",  "Whitepaper",           true, wpParams});
    c.push_back({"whitepaper2", "Whitepaper 2",         true, wpParams});
    c.push_back({"enhance",     "Enhance",              true, {}});
    c.push_back({"colors",      "Color Simplification", true, {
        {"resizeThreshold",     "Resize Threshold",  10, 500, 100, 1},
        {"filterDistThreshold", "Filter Dist Thresh", 1, 100, 20,  1},
        {"distThreshold",       "Distance Threshold", 1, 100, 40,  1},
        {"nbColors",            "Num Colors",         2, 20,  5,   1},
        {"colorSpace",          "Color Space",        0, 5,   0,   1},
        {"paletteColorSpace",   "Palette Space",      0, 5,   2,   1},
    }});

    // ---- Placeholders — not yet integrated ----
    c.push_back({"adaptive_sauvola","Adaptive Binarize: Sauvola (todo)",false,{
        {"windowSize","Window Size",5,101,25,2},
        {"k",         "K (×0.01)", 1,100,34,1},
        {"delta",     "Delta",     0,100, 0,1},
    }});
    c.push_back({"adaptive_wolf","Adaptive Binarize: Wolf (todo)",false,{
        {"windowSize","Window Size",5,101,25,2},
        {"k",         "K (×0.01)", 1,100,30,1},
    }});
    c.push_back({"adaptive_bradley","Adaptive Binarize: Bradley (todo)",false,{
        {"windowSize","Window Size",5,101,25,2},
        {"k",         "K (×0.01)", 1,100,15,1},
    }});
    c.push_back({"adaptive_edgediv","Adaptive Binarize: EdgeDiv (todo)",false,{
        {"windowSize","Window Size",5,101,25,2},
        {"kep",       "kep (×0.01)",0,100,50,1},
        {"kdb",       "kdb (×0.01)",0,100,50,1},
    }});
    c.push_back({"adaptive_grad","Adaptive Binarize: Grad (todo)",false,{
        {"windowSize","Window Size",5,101,25,2},
        {"k",         "K (×0.01)", 1,100,30,1},
    }});
    c.push_back({"skew_correct","Skew Correction (todo)",false,{
        {"maxAngle","Max Angle (deg)",1,45,10,1},
    }});
    c.push_back({"wiener_denoise","Wiener Denoise (todo)",false,{
        {"windowSize","Window Size",1,15,5,1},
        {"noiseSigma","Noise Sigma",1,100,10,1},
    }});
    c.push_back({"wiener_color","Wiener Denoise Color (todo)",false,{
        {"windowSize","Window Size",1,15,5,1},
        {"coef",      "Coef (×0.01)",1,100,10,1},
    }});
    c.push_back({"bg_normalize","Background Normalize (todo)",false,{
        {"polyDegree",    "Poly Degree",    1,8,4,1},
        {"marginFraction","Margin % (×0.01)",5,40,15,1},
    }});
    c.push_back({"despeckle_cautious",  "Despeckle Cautious (todo)",  false, {}});
    c.push_back({"despeckle_normal",    "Despeckle Normal (todo)",    false, {}});
    c.push_back({"despeckle_aggressive","Despeckle Aggressive (todo)", false, {}});

    return c;
}

// ============================================================
//  Pipeline Presets — example mode configurations
// ============================================================

/**
 * A named pipeline preset.  Each step stores an algorithm ID and optional
 * parameter overrides; any param NOT overridden uses the AlgoDef default.
 */
struct PipelinePreset {
    QString name;
    QString description;
    /// {algoId, paramOverrides}  — empty map = use AlgoDef defaults
    QVector<QPair<QString, QMap<QString,double>>> steps;
};

/**
 * Build the predefined preset list.  Every algo ID here must be present
 * in the catalogue produced by buildCatalog().
 *
 * The presets are intended as demonstrations / starting points; the user
 * can freely modify each step after loading.
 */
static QVector<PipelinePreset> buildPresets() {
    QVector<PipelinePreset> p;

    // ---- 📖 Book Scan ----
    // Gutter detection is automatic (built into DocumentDetector::detectGutterAndSplit).
    // This pipeline handles the per-page image quality.
    p.push_back({
        "📖 Book Scan",
        "Two-page book scan — gutter detection splits pages automatically.\n"
        "Normalizes illumination (page curl shadows), deskews each page,\n"
        "denoises, then binarizes with Sauvola (optimal for ink on paper\n"
        "with spine shadow gradients). Finish with light despeckle.",
        {
            {"bg_normalize",    {{"polyDegree", 4}, {"marginFraction", 15}}},
            {"skew_correct",    {{"maxAngle", 10}}},
            {"wiener_denoise",  {{"windowSize", 5}, {"noiseSigma", 8}}},
            {"adaptive_sauvola",{{"windowSize", 25}, {"k", 34}, {"delta", 0}}},
            {"despeckle_normal",{}},
        }
    });

    // ---- 📄 Whitepaper Document ----
    p.push_back({
        "📄 Whitepaper Document",
        "Flat whitepaper / whiteboard document: corrects colour cast and\n"
        "contrast with the Whitepaper transform, then applies Enhance for\n"
        "additional sharpening and a cautious despeckle.",
        {
            {"whitepaper",         {}},
            {"enhance",            {}},
            {"despeckle_cautious", {}},
        }
    });

    // ---- 📄 Whitepaper 2 (Alt) ----
    p.push_back({
        "📄 Whitepaper 2 (Alt)",
        "Alternative whitepaper pipeline using the secondary transform\n"
        "(different highlight recovery), followed by colour simplification\n"
        "to a small palette — great for diagrams and handwritten notes.",
        {
            {"whitepaper2",        {}},
            {"colors",             {{"nbColors", 4}, {"filterDistThreshold", 15}}},
            {"despeckle_cautious", {}},
        }
    });

    // ---- 🔤 OCR Preparation ----
    p.push_back({
        "🔤 OCR Preparation",
        "Optimised for optical character recognition:\n"
        "1) Background-normalize (removes uneven lighting).\n"
        "2) Skew-correct (horizontal text lines → max projection variance).\n"
        "3) Wiener denoise (preserve strokes, reduce scan noise).\n"
        "4) Wolf binarization (robust to varying local contrast).\n"
        "5) Normal despeckle (remove dots that confuse OCR engines).",
        {
            {"bg_normalize",    {{"polyDegree", 4}}},
            {"skew_correct",    {{"maxAngle", 10}}},
            {"wiener_denoise",  {{"windowSize", 7}, {"noiseSigma", 12}}},
            {"adaptive_wolf",   {{"windowSize", 25}, {"k", 30}}},
            {"despeckle_normal",{}},
        }
    });

    // ---- 🎨 Color Document ----
    p.push_back({
        "🎨 Color Document",
        "Preserves colour while reducing noise and normalizing illumination.\n"
        "Colour-preserving Wiener filter → background normalization →\n"
        "colour-palette simplification (keep 6 colours by default).\n"
        "Suitable for maps, charts, and colour-rich printed documents.",
        {
            {"wiener_color",  {{"windowSize", 5}, {"coef", 8}}},
            {"bg_normalize",  {{"polyDegree", 3}}},
            {"colors",        {{"nbColors", 6}, {"filterDistThreshold", 15}}},
        }
    });

    // ---- 🌑 Shadow Removal ----
    p.push_back({
        "🌑 Shadow Removal",
        "Removes cast shadows and uneven lighting using high-degree\n"
        "polynomial background estimation followed by EdgeDiv binarization,\n"
        "which blends edge-enhanced and blur-divided images — very robust\n"
        "against illumination gradients near the spine or under a lamp.\n"
        "Best combined with Book Scan for double-page spreads.",
        {
            {"bg_normalize",     {{"polyDegree", 5}, {"marginFraction", 20}}},
            {"wiener_denoise",   {{"windowSize", 3}, {"noiseSigma", 5}}},
            {"adaptive_edgediv", {{"windowSize", 31}, {"kep", 60}, {"kdb", 40}}},
            {"despeckle_cautious",{}},
        }
    });

    // ---- ✏️ Gradient Binarize (Grad) ----
    p.push_back({
        "✏️ Gradient Binarize",
        "Uses the scantailor-advanced Grad method (zvezdochiot 2024):\n"
        "classifies a pixel as foreground when it falls below\n"
        "  mean - k * (maxStd - localStd)\n"
        "Very crisp edges; works well on pencil and ink drawings.\n"
        "Preceded by background normalization and a light denoise.",
        {
            {"bg_normalize",    {{"polyDegree", 3}}},
            {"wiener_denoise",  {{"windowSize", 3}, {"noiseSigma", 6}}},
            {"adaptive_grad",   {{"windowSize", 25}, {"k", 30}}},
            {"despeckle_cautious",{}},
        }
    });

    return p;
}

// ============================================================
//  ImageDisplayWidget — shows a cv::Mat with zoom and pan
// ============================================================

class ImageDisplayWidget : public QWidget {
    Q_OBJECT
public:
    explicit ImageDisplayWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(300, 200);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
    }

    void setImage(const QPixmap& px) {
        pixmap_  = px;
        if (!px.isNull() && zoomFit_) fitToWindow();
        update();
    }

    void fitToWindow() {
        if (pixmap_.isNull()) return;
        double sx = (double)width()  / pixmap_.width();
        double sy = (double)height() / pixmap_.height();
        zoom_   = std::min(sx, sy);
        offset_ = QPointF(
            (width()  - pixmap_.width()  * zoom_) / 2.0,
            (height() - pixmap_.height() * zoom_) / 2.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(20,20,20));
        if (pixmap_.isNull()) {
            p.setPen(QColor(100,100,100));
            p.drawText(rect(), Qt::AlignCenter, "No image loaded");
            return;
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1.0);
        int dw = (int)(pixmap_.width()  * zoom_);
        int dh = (int)(pixmap_.height() * zoom_);
        p.drawPixmap(QRect((int)offset_.x(),(int)offset_.y(),dw,dh), pixmap_);
    }

    void resizeEvent(QResizeEvent*) override {
        if (!pixmap_.isNull() && zoomFit_) fitToWindow();
    }

    void wheelEvent(QWheelEvent* e) override {
        if (pixmap_.isNull()) return;
        double factor = (e->angleDelta().y() > 0) ? 1.15 : (1.0/1.15);
        QPointF mousePos = e->position();
        // Zoom around cursor
        offset_ = mousePos - (mousePos - offset_) * factor;
        zoom_  *= factor;
        zoom_   = std::clamp(zoom_, 0.05, 32.0);
        zoomFit_ = false;
        update();
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            dragging_    = true;
            dragStart_   = e->pos();
            offsetStart_ = offset_;
            setCursor(Qt::ClosedHandCursor);
        } else if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
            zoomFit_ = true;
            fitToWindow();
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_) {
            offset_ = offsetStart_ + QPointF(e->pos() - dragStart_);
            zoomFit_ = false;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            dragging_ = false;
            setCursor(Qt::CrossCursor);
        }
    }

    void mouseDoubleClickEvent(QMouseEvent*) override {
        zoomFit_ = true;
        fitToWindow();
    }

private:
    QPixmap  pixmap_;
    QPointF  offset_      = {0, 0};
    double   zoom_        = 1.0;
    bool     zoomFit_     = true;
    bool     dragging_    = false;
    QPoint   dragStart_;
    QPointF  offsetStart_;
};

// ============================================================
//  ParamFormWidget — dynamic form for one pipeline step's params
// ============================================================

class ParamFormWidget : public QWidget {
    Q_OBJECT
signals:
    void paramChanged(const QString& id, double value);

public:
    explicit ParamFormWidget(QWidget* parent = nullptr) : QWidget(parent) {
        layout_ = new QFormLayout(this);
        layout_->setContentsMargins(8,8,8,8);
        layout_->setSpacing(6);
        layout_->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }

    void setStep(PipelineStep* step) {
        step_ = step;
        rebuild();
    }

    void clearStep() { step_ = nullptr; rebuild(); }

private:
    void rebuild() {
        // Remove all existing rows
        while (layout_->rowCount() > 0)
            layout_->removeRow(0);

        if (!step_ || step_->def.params.isEmpty()) {
            auto* lbl = new QLabel("(no parameters)", this);
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color: #808080; font-style: italic;");
            layout_->addRow(lbl);
            return;
        }

        for (const auto& p : step_->def.params) {
            auto* row = new QWidget(this);
            auto* hl  = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0); hl->setSpacing(4);

            double curVal = step_->paramValues.value(p.id, p.defaultVal);
            bool   isInt  = (p.step >= 1.0 &&
                             std::fmod(p.minVal, 1.0) == 0.0 &&
                             std::fmod(p.maxVal, 1.0) == 0.0);

            auto* sl = new QSlider(Qt::Horizontal, row);
            sl->setRange((int)(p.minVal / p.step), (int)(p.maxVal / p.step));
            sl->setValue((int)(curVal / p.step));

            if (isInt) {
                auto* spn = new QSpinBox(row);
                spn->setRange((int)p.minVal, (int)p.maxVal);
                spn->setValue((int)curVal);
                spn->setFixedWidth(64);

                QString pid = p.id;
                PipelineStep* s = step_;
                connect(sl, &QSlider::valueChanged, this, [this,s,pid,spn,p](int v) {
                    double val = v * p.step;
                    s->paramValues[pid] = val;
                    spn->blockSignals(true); spn->setValue((int)val); spn->blockSignals(false);
                    emit paramChanged(pid, val);
                });
                connect(spn, QOverload<int>::of(&QSpinBox::valueChanged), this,
                        [this,s,pid,sl,p](int v) {
                    double val = v;
                    s->paramValues[pid] = val;
                    sl->blockSignals(true); sl->setValue((int)(val/p.step)); sl->blockSignals(false);
                    emit paramChanged(pid, val);
                });
                hl->addWidget(sl,1); hl->addWidget(spn);
            } else {
                int dec = (p.step < 0.01) ? 3 : (p.step < 0.1) ? 2 : 1;
                auto* spn = new QDoubleSpinBox(row);
                spn->setRange(p.minVal, p.maxVal);
                spn->setSingleStep(p.step);
                spn->setDecimals(dec);
                spn->setValue(curVal);
                spn->setFixedWidth(72);

                QString pid = p.id;
                PipelineStep* s = step_;
                connect(sl, &QSlider::valueChanged, this, [this,s,pid,spn,p](int v) {
                    double val = v * p.step;
                    s->paramValues[pid] = val;
                    spn->blockSignals(true); spn->setValue(val); spn->blockSignals(false);
                    emit paramChanged(pid, val);
                });
                connect(spn, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                        [this,s,pid,sl,p](double v) {
                    s->paramValues[pid] = v;
                    sl->blockSignals(true); sl->setValue((int)(v/p.step)); sl->blockSignals(false);
                    emit paramChanged(pid, v);
                });
                hl->addWidget(sl,1); hl->addWidget(spn);
            }
            layout_->addRow(p.label, row);
        }
    }

    QFormLayout*  layout_;
    PipelineStep* step_ = nullptr;
};

// ============================================================
//  AlgorithmPipelineWidget — stack list + param panel
// ============================================================

class AlgorithmPipelineWidget : public QWidget {
    Q_OBJECT
signals:
    void pipelineChanged();

public:
    explicit AlgorithmPipelineWidget(const QVector<AlgoDef>& catalog,
                                     const QVector<PipelinePreset>& presets,
                                     QWidget* parent = nullptr)
        : QWidget(parent), catalog_(catalog), presets_(presets)
    {
        auto* mainVl = new QVBoxLayout(this);
        mainVl->setContentsMargins(0,0,0,0);
        mainVl->setSpacing(0);

        // ---- Top: pipeline list ----
        auto* listGb = new QGroupBox("Algorithm Pipeline", this);
        auto* listVl = new QVBoxLayout(listGb);
        listVl->setContentsMargins(4,6,4,4);
        listVl->setSpacing(6);

        // ── Preset selector row ──
        auto* presetRow = new QWidget(listGb);
        auto* presetHl  = new QHBoxLayout(presetRow);
        presetHl->setContentsMargins(0,0,0,0); presetHl->setSpacing(4);
        auto* modeLbl = new QLabel("Mode:", presetRow);
        modeLbl->setFixedWidth(40);
        modeLbl->setStyleSheet("color: #A0A0A0; font-size: 11px;");
        presetCombo_ = new QComboBox(presetRow);
        presetCombo_->addItem("(custom — no preset)");
        for (const auto& pr : presets_)
            presetCombo_->addItem(pr.name);
        presetCombo_->setToolTip("Select a mode preset to load a predefined pipeline");
        auto* loadBtn = new QPushButton("▶ Load", presetRow);
        loadBtn->setFixedWidth(64);
        loadBtn->setToolTip("Load selected preset — replaces the current pipeline");
        loadBtn->setStyleSheet("QPushButton { background:#1A5A1A; border-color:#2A8A2A; }"
                               "QPushButton:hover { background:#216121; }");
        connect(loadBtn, &QPushButton::clicked, this, [this]{
            onLoadPreset(presetCombo_->currentIndex());
        });
        presetHl->addWidget(modeLbl);
        presetHl->addWidget(presetCombo_, 1);
        presetHl->addWidget(loadBtn);
        listVl->addWidget(presetRow);

        // ── Preset description label ──
        descLabel_ = new QLabel(listGb);
        descLabel_->setWordWrap(true);
        descLabel_->setStyleSheet(
            "color: #909090; font-size: 10px; font-style: italic;"
            "background: #1E2A1E; border: 1px solid #2A4A2A;"
            "border-radius: 3px; padding: 4px 6px;");
        descLabel_->setMinimumHeight(54);
        descLabel_->setMaximumHeight(80);
        descLabel_->setText("Select a mode above and press ▶ Load,\n"
                            "or build a custom pipeline with ＋ Add Step.");
        listVl->addWidget(descLabel_);

        // Update description when combo changes
        connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) {
            if (idx <= 0 || idx > (int)presets_.size()) {
                descLabel_->setText("Select a mode above and press ▶ Load,\n"
                                    "or build a custom pipeline with ＋ Add Step.");
            } else {
                descLabel_->setText(presets_[idx-1].description);
            }
        });

        // Separator line
        auto* sep = new QFrame(listGb);
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color: #444444;");
        listVl->addWidget(sep);

        // ── Add Step + Clear buttons ──
        auto* addClearRow = new QWidget(listGb);
        auto* addClearHl  = new QHBoxLayout(addClearRow);
        addClearHl->setContentsMargins(0,0,0,0); addClearHl->setSpacing(4);
        auto* addBtn = new QPushButton("＋ Add Step", addClearRow);
        addBtn->setToolTip("Add a processing step to the pipeline");
        connect(addBtn, &QPushButton::clicked, this, &AlgorithmPipelineWidget::onAddStep);
        auto* clearBtn = new QPushButton("🗑 Clear", addClearRow);
        clearBtn->setToolTip("Remove all pipeline steps");
        clearBtn->setFixedWidth(72);
        clearBtn->setStyleSheet("QPushButton { background:#3A1A1A; border-color:#6A2A2A; }"
                                "QPushButton:hover { background:#4A2020; }");
        connect(clearBtn, &QPushButton::clicked, this, [this]{
            pipeline_.clear();
            rebuildList();
            paramForm_->clearStep();
            presetCombo_->setCurrentIndex(0);
            emit pipelineChanged();
        });
        addClearHl->addWidget(addBtn, 1);
        addClearHl->addWidget(clearBtn);
        listVl->addWidget(addClearRow);

        listWidget_ = new QListWidget(listGb);
        listWidget_->setDragDropMode(QAbstractItemView::InternalMove);
        listWidget_->setSelectionMode(QAbstractItemView::SingleSelection);
        listWidget_->setMinimumHeight(80);
        connect(listWidget_, &QListWidget::currentRowChanged,
                this, &AlgorithmPipelineWidget::onSelectionChanged);
        // Reorder via drag-and-drop
        connect(listWidget_->model(), &QAbstractItemModel::rowsMoved,
                this, [this](auto,int,int,auto,int){
                    syncPipelineFromList();
                    emit pipelineChanged();
                });
        listVl->addWidget(listWidget_, 1);

        // Up / Down / Remove row
        auto* ctrlRow = new QWidget(listGb);
        auto* ctrlHl  = new QHBoxLayout(ctrlRow);
        ctrlHl->setContentsMargins(0,0,0,0); ctrlHl->setSpacing(4);
        auto* upBtn  = new QPushButton("▲", ctrlRow);
        auto* dnBtn  = new QPushButton("▼", ctrlRow);
        auto* rmBtn  = new QPushButton("✕ Remove", ctrlRow);
        upBtn->setFixedWidth(30); dnBtn->setFixedWidth(30);
        upBtn->setToolTip("Move step up");
        dnBtn->setToolTip("Move step down");
        rmBtn->setToolTip("Remove selected step");
        connect(upBtn, &QPushButton::clicked, this, &AlgorithmPipelineWidget::onMoveUp);
        connect(dnBtn, &QPushButton::clicked, this, &AlgorithmPipelineWidget::onMoveDown);
        connect(rmBtn, &QPushButton::clicked, this, &AlgorithmPipelineWidget::onRemove);
        ctrlHl->addWidget(upBtn);
        ctrlHl->addWidget(dnBtn);
        ctrlHl->addStretch();
        ctrlHl->addWidget(rmBtn);
        listVl->addWidget(ctrlRow);

        mainVl->addWidget(listGb, 1);

        // ---- Bottom: param editor ----
        auto* paramGb = new QGroupBox("Parameters", this);
        auto* paramVl = new QVBoxLayout(paramGb);
        paramVl->setContentsMargins(0,0,0,0);

        auto* scroll = new QScrollArea(paramGb);
        scroll->setWidgetResizable(true);
        scroll->setFrameStyle(QFrame::NoFrame);
        paramForm_ = new ParamFormWidget(scroll);
        scroll->setWidget(paramForm_);
        paramVl->addWidget(scroll);
        connect(paramForm_, &ParamFormWidget::paramChanged,
                this, [this](const QString&, double){ emit pipelineChanged(); });

        mainVl->addWidget(paramGb, 1);
    }

    const QVector<PipelineStep>& pipeline() const { return pipeline_; }

    /** Load a preset by its 0-based index in presets_. */
    void loadPresetByIndex(int idx) {
        if (idx < 0 || idx >= (int)presets_.size()) return;
        const PipelinePreset& pr = presets_[idx];

        pipeline_.clear();
        for (const auto& [algoId, overrides] : pr.steps) {
            // Find AlgoDef in catalog
            for (const auto& def : catalog_) {
                if (def.id == algoId) {
                    PipelineStep step = makeStep(def);
                    // Apply overrides
                    for (auto it = overrides.begin(); it != overrides.end(); ++it)
                        step.paramValues[it.key()] = it.value();
                    pipeline_.push_back(step);
                    break;
                }
            }
        }

        rebuildList();
        if (!pipeline_.empty())
            listWidget_->setCurrentRow(0);

        // Update combo + description (block signals to avoid re-entry)
        presetCombo_->blockSignals(true);
        presetCombo_->setCurrentIndex(idx + 1); // +1 because index 0 = "(custom)"
        presetCombo_->blockSignals(false);
        descLabel_->setText(pr.description);

        emit pipelineChanged();
    }

    /** Clear all pipeline steps (e.g. called from a menu action). */
    void clearPipeline() {
        pipeline_.clear();
        rebuildList();
        if (paramForm_) paramForm_->clearStep();
        presetCombo_->setCurrentIndex(0);
        emit pipelineChanged();
    }

private slots:
    void onLoadPreset(int comboIdx) {
        // comboIdx 0 = "(custom)", 1..N = preset idx 0..N-1
        if (comboIdx <= 0) return;
        loadPresetByIndex(comboIdx - 1);
    }

    void onAddStep() {
        QMenu menu(this);
        for (const auto& def : catalog_) {
            QAction* act = menu.addAction(def.name);
            act->setData(def.id);
        }
        QAction* chosen = menu.exec(QCursor::pos());
        if (!chosen) return;
        QString id = chosen->data().toString();
        for (const auto& def : catalog_) {
            if (def.id == id) {
                pipeline_.push_back(makeStep(def));
                addListRow(pipeline_.back());
                presetCombo_->setCurrentIndex(0); // mark as custom
                emit pipelineChanged();
                listWidget_->setCurrentRow(listWidget_->count() - 1);
                break;
            }
        }
    }

    void onSelectionChanged(int row) {
        if (row < 0 || row >= (int)pipeline_.size()) {
            paramForm_->clearStep();
            return;
        }
        paramForm_->setStep(&pipeline_[row]);
    }

    void onMoveUp() {
        int row = listWidget_->currentRow();
        if (row <= 0) return;
        std::swap(pipeline_[row], pipeline_[row-1]);
        rebuildList();
        listWidget_->setCurrentRow(row - 1);
        emit pipelineChanged();
    }

    void onMoveDown() {
        int row = listWidget_->currentRow();
        if (row < 0 || row >= (int)pipeline_.size()-1) return;
        std::swap(pipeline_[row], pipeline_[row+1]);
        rebuildList();
        listWidget_->setCurrentRow(row + 1);
        emit pipelineChanged();
    }

    void onRemove() {
        int row = listWidget_->currentRow();
        if (row < 0 || row >= (int)pipeline_.size()) return;
        pipeline_.remove(row);
        rebuildList();
        emit pipelineChanged();
    }

private:
    void addListRow(const PipelineStep& step) {
        auto* item = new QListWidgetItem(listWidget_);
        updateItemText(item, step);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(step.enabled ? Qt::Checked : Qt::Unchecked);
        connect(listWidget_, &QListWidget::itemChanged, this,
                &AlgorithmPipelineWidget::onItemChanged);
    }

    void updateItemText(QListWidgetItem* item, const PipelineStep& step) {
        QString prefix = step.def.implemented ? "→ " : "⊘ ";
        item->setText(prefix + step.def.name);
        if (!step.def.implemented)
            item->setForeground(QColor(150,150,80));
        else
            item->setForeground(QColor(220,220,220));
    }

    void rebuildList() {
        listWidget_->blockSignals(true);
        listWidget_->clear();
        for (auto& s : pipeline_)
            addListRow(s);
        listWidget_->blockSignals(false);
    }

    void syncPipelineFromList() {
        QVector<PipelineStep> newPipeline;
        for (int i = 0; i < listWidget_->count(); ++i) {
            QString text = listWidget_->item(i)->text();
            for (auto& s : pipeline_) {
                QString t2 = (s.def.implemented ? "→ " : "⊘ ") + s.def.name;
                if (t2 == text) {
                    newPipeline.push_back(s);
                    break;
                }
            }
        }
        if (newPipeline.size() == pipeline_.size())
            pipeline_ = newPipeline;
    }

    void onItemChanged(QListWidgetItem* item) {
        int row = listWidget_->row(item);
        if (row < 0 || row >= (int)pipeline_.size()) return;
        pipeline_[row].enabled = (item->checkState() == Qt::Checked);
        emit pipelineChanged();
    }

    QVector<AlgoDef>      catalog_;
    QVector<PipelinePreset> presets_;
    QVector<PipelineStep>  pipeline_;
    QListWidget*           listWidget_   = nullptr;
    ParamFormWidget*       paramForm_    = nullptr;
    QComboBox*             presetCombo_  = nullptr;
    QLabel*                descLabel_    = nullptr;
};

// ============================================================
//  DetectionSettingsWidget — DocumentDetector options form
// ============================================================

class DetectionSettingsWidget : public QWidget {
    Q_OBJECT
signals:
    void settingsChanged();

public:
    struct DetSettings {
        double cannyFactor                = 2.0;
        int    morphologyAnchorSize       = 4;
        int    dilateAnchorSize           = 3;
        int    thresh                     = 160;
        int    threshMax                  = 256;
        int    bilateralFilterValue       = 18;
        int    medianBlurValue            = 9;
        double contoursApproxEpsilonFactor = 0.02;
        int    houghLinesThreshold        = 0;
        int    houghLinesMinLineLength    = 55;
        int    houghLinesMaxLineGap       = 0;
        int    useChannel                 = 0; // 0=auto (-1 in detector), 1-3 = ch 0-2
        // Book mode gutter detection
        double gutterSensitivity          = 0.15; // significance threshold (0.05=very sensitive, 0.40=strict)
    };

    DetSettings settings;

    explicit DetectionSettingsWidget(QWidget* parent = nullptr) : QWidget(parent) {
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameStyle(QFrame::NoFrame);
        auto* outerVl = new QVBoxLayout(this);
        outerVl->setContentsMargins(0,0,0,0);
        outerVl->addWidget(scroll);

        auto* w  = new QWidget;
        auto* fl = new QFormLayout(w);
        fl->setContentsMargins(8,8,8,8);
        fl->setSpacing(6);
        fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        scroll->setWidget(w);

        auto addInt = [&](const QString& lbl, int lo, int hi, int* val) {
            auto* row = new QWidget(w);
            auto* hl  = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0); hl->setSpacing(4);
            auto* sl  = new QSlider(Qt::Horizontal, row);
            auto* spn = new QSpinBox(row);
            sl->setRange(lo, hi); sl->setValue(*val);
            spn->setRange(lo, hi); spn->setValue(*val);
            spn->setFixedWidth(60);
            connect(sl,  &QSlider::valueChanged, this, [this,val,spn](int v){
                *val = v;
                spn->blockSignals(true); spn->setValue(v); spn->blockSignals(false);
                emit settingsChanged();
            });
            connect(spn, QOverload<int>::of(&QSpinBox::valueChanged), this, [this,val,sl](int v){
                *val = v;
                sl->blockSignals(true); sl->setValue(v); sl->blockSignals(false);
                emit settingsChanged();
            });
            hl->addWidget(sl,1); hl->addWidget(spn);
            fl->addRow(lbl, row);
        };

        auto addDbl = [&](const QString& lbl, double lo, double hi, double st, double* val) {
            auto* row = new QWidget(w);
            auto* hl  = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0); hl->setSpacing(4);
            int slo=(int)(lo/st), shi=(int)(hi/st), sv=(int)(*val/st);
            auto* sl  = new QSlider(Qt::Horizontal, row);
            auto* spn = new QDoubleSpinBox(row);
            sl->setRange(slo, shi); sl->setValue(sv);
            spn->setRange(lo, hi); spn->setSingleStep(st);
            int dec = (st < 0.01) ? 3 : (st < 0.1) ? 2 : 1;
            spn->setDecimals(dec); spn->setValue(*val);
            spn->setFixedWidth(72);
            connect(sl,  &QSlider::valueChanged, this, [this,val,st,spn](int v){
                *val = v*st;
                spn->blockSignals(true); spn->setValue(*val); spn->blockSignals(false);
                emit settingsChanged();
            });
            connect(spn, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [this,val,st,sl](double v){
                *val = v;
                sl->blockSignals(true); sl->setValue((int)(v/st)); sl->blockSignals(false);
                emit settingsChanged();
            });
            hl->addWidget(sl,1); hl->addWidget(spn);
            fl->addRow(lbl, row);
        };

        addInt("Use Channel (0=auto)", 0, 3,   &settings.useChannel);
        addDbl("Canny Factor",         0, 10,  0.01, &settings.cannyFactor);
        addInt("Morphology Size",      0, 20,  &settings.morphologyAnchorSize);
        addInt("Dilate Size",          0, 20,  &settings.dilateAnchorSize);
        addInt("Threshold",            0, 300, &settings.thresh);
        addInt("Threshold Max",        0, 300, &settings.threshMax);
        addInt("Bilateral Filter",     0, 200, &settings.bilateralFilterValue);
        addInt("Median Blur",          0, 200, &settings.medianBlurValue);
        addDbl("Contours Epsilon",     0, 0.2, 0.001, &settings.contoursApproxEpsilonFactor);
        addInt("Hough Threshold",      0, 500, &settings.houghLinesThreshold);
        addInt("Hough Min Length",     0, 500, &settings.houghLinesMinLineLength);
        addInt("Hough Max Gap",        0, 500, &settings.houghLinesMaxLineGap);

        // ── Book-mode gutter section ──────────────────────────────────────
        auto* gutterSep = new QFrame(w);
        gutterSep->setFrameShape(QFrame::HLine);
        gutterSep->setStyleSheet("color: #555555;");
        fl->addRow(gutterSep);
        auto* gutterLbl = new QLabel("<b>Book Mode / Gutter</b>", w);
        gutterLbl->setStyleSheet("color: #80C0FF; font-size: 11px;");
        fl->addRow(gutterLbl);
        addDbl("Gutter Sensitivity",  0.01, 0.50, 0.01, &settings.gutterSensitivity);
        auto* gutterHelp = new QLabel(
            "Higher = only split when gutter is very obvious.\n"
            "Lower  = split more aggressively.", w);
        gutterHelp->setStyleSheet("color: #909090; font-size: 10px; font-style: italic;");
        gutterHelp->setWordWrap(true);
        fl->addRow(gutterHelp);
    }

    void applyToDetector(detector::DocumentDetector& det) const {
        det.options.cannyFactor                 = settings.cannyFactor;
        det.options.morphologyAnchorSize        = settings.morphologyAnchorSize;
        det.options.dilateAnchorSize            = settings.dilateAnchorSize;
        det.options.thresh                      = settings.thresh;
        det.options.threshMax                   = settings.threshMax;
        det.options.bilateralFilterValue        = settings.bilateralFilterValue;
        int mb = settings.medianBlurValue;
        det.options.medianBlurValue             = (mb > 0 && mb % 2 == 0) ? mb+1 : mb;
        det.options.contoursApproxEpsilonFactor = settings.contoursApproxEpsilonFactor;
        det.options.houghLinesThreshold         = settings.houghLinesThreshold;
        det.options.houghLinesMinLineLength     = settings.houghLinesMinLineLength;
        det.options.houghLinesMaxLineGap        = settings.houghLinesMaxLineGap;
        det.options.useChannel                  = settings.useChannel - 1; // 0→-1 (auto)
    }
};

// ============================================================
//  ScannerWindow — main QMainWindow
// ============================================================

class ScannerWindow : public QMainWindow {
    Q_OBJECT

    enum ViewMode { SOURCE=0, EDGES=1, RESULT=2, COMPARE=3 };

public:
    explicit ScannerWindow(const QVector<AlgoDef>& catalog,
                           const QVector<PipelinePreset>& presets,
                           QWidget* parent = nullptr)
        : QMainWindow(parent)
        , catalog_(catalog)
        , presets_(presets)
        , docDetector_(300, 0)
    {
        setWindowTitle("Document Scanner");
        resize(1600, 900);
        buildUI();
        setupMenuBar();
        setupToolBar();
        statusBar()->showMessage("Ready — File → Open Folder to begin");

        // Debounce timer so rapid param changes don't re-run every keystroke
        debounceTimer_ = new QTimer(this);
        debounceTimer_->setSingleShot(true);
        debounceTimer_->setInterval(120);
        connect(debounceTimer_, &QTimer::timeout, this, &ScannerWindow::runPipeline);
    }

    void loadFolder(const QString& path) {
        images_ = loadImagesFromFolder(path.toStdString());
        fileList_->clear();
        for (const auto& imgPath : images_) {
            auto* item = new QListWidgetItem;
            item->setText(QString::fromStdString(
                filesystem::path(imgPath).filename().string()));
            item->setToolTip(QString::fromStdString(imgPath));
            QImageReader reader(QString::fromStdString(imgPath));
            reader.setScaledSize(QSize(88,66));
            QImage thumb = reader.read();
            if (!thumb.isNull())
                item->setIcon(QIcon(QPixmap::fromImage(thumb)));
            fileList_->addItem(item);
        }
        if (!images_.empty())
            fileList_->setCurrentRow(0);
    }

    void loadImage(int idx) {
        if (idx < 0 || idx >= (int)images_.size()) return;
        currentIdx_   = idx;
        currentImage_ = cv::imread(images_[idx]);
        if (currentImage_.empty()) {
            statusBar()->showMessage(
                "Failed to load: " + QString::fromStdString(images_[idx]));
            return;
        }
        debounceTimer_->start();
    }

private slots:
    void runPipeline() {
        if (currentImage_.empty()) return;

        QElapsedTimer timer;
        timer.start();

        detSettings_->applyToDetector(docDetector_);

        docDetector_.image = currentImage_;
        resizedImage_      = docDetector_.resizeImageMax();

        // Reset book-mode state
        gutterFound_      = false;
        gutterXResized_   = -1;
        leftPageWarped_   = Mat(); rightPageWarped_  = Mat();
        leftPageResult_   = Mat(); rightPageResult_  = Mat();
        leftDetectedPts_.clear();  rightDetectedPts_.clear();

        // ── Gutter detection (always run; result used only in book mode) ──
        auto split = docDetector_.detectGutterAndSplit(resizedImage_, 0.30f, 5,
            (float)detSettings_->settings.gutterSensitivity);

        if (bookMode_ && split.foundGutter && split.hasLeft && split.hasRight) {
            gutterFound_    = true;
            gutterXResized_ = split.gutterX;

            // Helper: scan one sub-image, warp, run pipeline, return result
            auto processPage = [&](const Rect& roi) -> pair<Mat, vector<cv::Point>> {
                if (roi.width <= 10 || roi.height <= 10)
                    return {Mat(), {}};

                Mat subImage = resizedImage_(roi).clone();
                Mat subEdged;
                auto pts = docDetector_.scanPoint(subEdged, subImage, /*drawContours=*/false);

                // If no contour found, use full sub-image rectangle
                if (pts.empty()) {
                    pts.push_back({
                        cv::Point(0, 0),
                        cv::Point(subImage.cols, 0),
                        cv::Point(subImage.cols, subImage.rows),
                        cv::Point(0, subImage.rows)
                    });
                }

                // Scale detected points back to currentImage_ coordinates
                double sf = docDetector_.resizeScale * docDetector_.scale;
                vector<cv::Point> scaledPts;
                for (auto& p : pts[0])
                    scaledPts.push_back(cv::Point(
                        (int)((p.x + roi.x) / sf),
                        (int)((p.y + roi.y) / sf)));

                // Warp from the original full-resolution image
                Rect fullRoi(
                    (int)(roi.x / sf), (int)(roi.y / sf),
                    std::min((int)(roi.width  / sf), currentImage_.cols - (int)(roi.x / sf)),
                    std::min((int)(roi.height / sf), currentImage_.rows - (int)(roi.y / sf)));
                fullRoi.x      = std::max(fullRoi.x, 0);
                fullRoi.y      = std::max(fullRoi.y, 0);
                fullRoi.width  = std::min(fullRoi.width,  currentImage_.cols - fullRoi.x);
                fullRoi.height = std::min(fullRoi.height, currentImage_.rows - fullRoi.y);

                Mat warpedPage;
                if (fullRoi.width > 10 && fullRoi.height > 10) {
                    // Re-scale pts[0] relative to the sub-image for cropAndWarp
                    vector<cv::Point> warpPts;
                    for (auto& p : pts[0])
                        warpPts.push_back(cv::Point(
                            std::clamp((int)(p.x / sf), 0, fullRoi.width  - 1),
                            std::clamp((int)(p.y / sf), 0, fullRoi.height - 1)));

                    Mat pageOrig = currentImage_(fullRoi).clone();
                    warpedPage   = cropAndWarp(pageOrig, warpPts);
                    if (warpedPage.empty())
                        warpedPage = pageOrig;
                }

                return {warpedPage, scaledPts};
            };

            auto [lWarped, lPts] = processPage(split.leftPage);
            auto [rWarped, rPts] = processPage(split.rightPage);

            leftPageWarped_  = lWarped;
            rightPageWarped_ = rWarped;
            leftDetectedPts_ = lPts;
            rightDetectedPts_= rPts;

            // Run the algorithm pipeline on each page independently
            leftPageResult_  = leftPageWarped_.empty()  ? Mat() : leftPageWarped_.clone();
            rightPageResult_ = rightPageWarped_.empty() ? Mat() : rightPageWarped_.clone();
            for (auto& step : pipelineWidget_->pipeline()) {
                if (!step.enabled) continue;
                if (!leftPageResult_.empty())  applyStep(step, leftPageResult_);
                if (!rightPageResult_.empty()) applyStep(step, rightPageResult_);
            }

            // For the RESULT view we stitch both pages side by side
            resultImage_ = stitchPages(leftPageResult_, rightPageResult_);

            // Also produce a combined edge map and detected points for other views
            edged_ = Mat();
            detectedPoints_.clear();
        } else {
            // ── Standard single-page pipeline ────────────────────────────
            vector<vector<cv::Point>> pointsList;
            pointsList = docDetector_.scanPoint(edged_, resizedImage_, true);

            if (pointsList.empty()) {
                pointsList.push_back({
                    cv::Point(0,0),
                    cv::Point(currentImage_.cols,0),
                    cv::Point(currentImage_.cols,currentImage_.rows),
                    cv::Point(0,currentImage_.rows)
                });
            }

            if (!pointsList.empty()) {
                detectedPoints_ = pointsList[0];
                warped_ = cropAndWarp(currentImage_, pointsList[0]);
            } else {
                detectedPoints_.clear();
                warped_ = Mat();
            }

            resultImage_ = warped_.empty() ? Mat() : warped_.clone();
            for (auto& step : pipelineWidget_->pipeline()) {
                if (!step.enabled || resultImage_.empty()) continue;
                applyStep(step, resultImage_);
            }
        }

        long long ms = timer.elapsed();
        QString modeStr = bookMode_ ?
            (gutterFound_ ? "📖 Book (gutter found)" : "📖 Book (no gutter — single page)") :
            "Single Page";
        bool detected = bookMode_ ? gutterFound_ :
            (!detectedPoints_.empty() &&
             !(detectedPoints_.size() == 4 &&
               detectedPoints_[0] == cv::Point(0,0) &&
               detectedPoints_[2] == cv::Point(currentImage_.cols,currentImage_.rows)));
        statusBar()->showMessage(
            QString("Image %1/%2  |  %3  |  Pipeline: %4ms  |  Detection: %5")
            .arg(currentIdx_+1).arg((int)images_.size())
            .arg(modeStr)
            .arg(ms)
            .arg(detected ? "found" : "not found / fallback"));

        updateDisplay();
    }

    void onViewChanged(int id) {
        viewMode_ = (ViewMode)id;
        updateDisplay();
    }

    void onFileClicked(int row) {
        if (row < 0 || row >= (int)images_.size()) return;
        loadImage(row);
    }

    void onOpenFolder() {
        QString dir = QFileDialog::getExistingDirectory(
            this, "Open Image Folder", QString(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!dir.isEmpty()) loadFolder(dir);
    }

    void onSaveResult() {
        if (bookMode_ && gutterFound_) {
            // In book mode: offer to save both pages
            QString basePath = QFileDialog::getSaveFileName(
                this, "Save Pages (base path — _left/_right will be appended)",
                QString(), "Images (*.png *.jpg *.bmp)");
            if (basePath.isEmpty()) return;
            QString ext  = QFileInfo(basePath).suffix();
            QString base = QFileInfo(basePath).absolutePath() + "/"
                         + QFileInfo(basePath).completeBaseName();
            if (!leftPageResult_.empty())
                cv::imwrite((base + "_left."  + ext).toStdString(), leftPageResult_);
            if (!rightPageResult_.empty())
                cv::imwrite((base + "_right." + ext).toStdString(), rightPageResult_);
        } else {
            if (resultImage_.empty()) {
                QMessageBox::information(this, "Save", "No result image to save.");
                return;
            }
            QString path = QFileDialog::getSaveFileName(
                this, "Save Result", QString(),
                "Images (*.png *.jpg *.bmp)");
            if (!path.isEmpty())
                cv::imwrite(path.toStdString(), resultImage_);
        }
    }

    void onPrevImage() {
        if (images_.empty()) return;
        int row = (currentIdx_ - 1 + (int)images_.size()) % (int)images_.size();
        fileList_->setCurrentRow(row);
    }

    void onNextImage() {
        if (images_.empty()) return;
        int row = (currentIdx_ + 1) % (int)images_.size();
        fileList_->setCurrentRow(row);
    }

private:
    // ----- Pipeline execution -----

    /** Stitch two page images side by side with a thin divider line. */
    static Mat stitchPages(const Mat& left, const Mat& right) {
        if (left.empty() && right.empty())
            return Mat();
        if (left.empty())  return right.clone();
        if (right.empty()) return left.clone();

        // Normalise to the same height
        Mat r = right.clone();
        if (r.rows != left.rows && r.rows > 0) {
            double sc = (double)left.rows / r.rows;
            cv::resize(r, r, Size((int)(r.cols * sc), left.rows));
        }

        const int gap = 6;
        Mat out(left.rows, left.cols + gap + r.cols, CV_8UC3, Scalar(30, 30, 30));
        if (left.type() == CV_8UC3)
            left.copyTo(out(Rect(0, 0, left.cols, left.rows)));
        else { Mat tmp; cvtColor(left, tmp, COLOR_GRAY2BGR); tmp.copyTo(out(Rect(0,0,left.cols,left.rows))); }
        if (r.type() == CV_8UC3)
            r.copyTo(out(Rect(left.cols + gap, 0, r.cols, out.rows)));
        else { Mat tmp; cvtColor(r, tmp, COLOR_GRAY2BGR); tmp.copyTo(out(Rect(left.cols+gap, 0, r.cols, out.rows))); }
        // Gutter divider line
        line(out, Point(left.cols + gap/2, 0), Point(left.cols + gap/2, out.rows),
             Scalar(80, 180, 255), 2, LINE_AA);
        return out;
    }

    void applyStep(const PipelineStep& step, Mat& img) {
        if (!step.def.implemented) return; // placeholder

        const auto& id = step.def.id;

        if (id == "whitepaper" || id == "whitepaper2") {
            WhitePaperTransformOptions opts;
            opts.csBlackPer = (int)step.paramValues.value("csBlackPer", 2);
            opts.csWhitePer = step.paramValues.value("csWhitePer", 99.5);
            opts.gaussKSize = (int)step.paramValues.value("gaussKSize", 3);
            opts.dogKSize   = (int)step.paramValues.value("dogKSize",   15);
            opts.dogSigma1  = (int)step.paramValues.value("dogSigma1",  100);
            opts.dogSigma2  = (int)step.paramValues.value("dogSigma2",  0);
            string s;
            jsoncons::encode_json(opts, s, jsoncons::indenting::no_indent);
            string key = (id == "whitepaper") ? "whitepaper_" + s : "whitepaper2_" + s;
            detector::DocumentDetector::applyTransforms(img, key);
        }
        else if (id == "enhance") {
            detector::DocumentDetector::applyTransforms(img, "enhance");
        }
        else if (id == "colors") {
            int resizeT  = (int)step.paramValues.value("resizeThreshold",     100);
            int filterD  = (int)step.paramValues.value("filterDistThreshold", 20);
            int distT    = (int)step.paramValues.value("distThreshold",       40);
            int nbCol    = (int)step.paramValues.value("nbColors",            5);
            int colSp    = (int)step.paramValues.value("colorSpace",          0);
            int palSp    = (int)step.paramValues.value("paletteColorSpace",   2);
            colorSimplificationTransform(
                img, img, false, resizeT, filterD, distT, nbCol,
                (ColorSpace)colSp, (ColorSpace)palSp);
        }
        // New algorithm placeholders — nothing yet
    }

    // ----- Display -----

    void updateDisplay() {
        if (currentImage_.empty()) return;

        Mat display;

        // ── Book mode with gutter found: specialised views ──────────────────
        if (bookMode_ && gutterFound_) {
            switch (viewMode_) {
                case SOURCE: {
                    display = currentImage_.clone();
                    double sf = docDetector_.resizeScale * docDetector_.scale;

                    // Draw gutter line
                    if (gutterXResized_ > 0 && sf > 0.0) {
                        int gx = (int)(gutterXResized_ / sf);
                        line(display, Point(gx, 0), Point(gx, display.rows),
                             Scalar(80, 180, 255), 3, LINE_AA);
                    }

                    // Draw left-page contour (cyan)
                    if (!leftDetectedPts_.empty()) {
                        vector<vector<cv::Point>> c = {leftDetectedPts_};
                        polylines(display, c, true, Scalar(0, 220, 255), 3, LINE_AA);
                        for (auto& p : leftDetectedPts_)
                            circle(display, p, 8, Scalar(0, 255, 100), -1, LINE_AA);
                    }
                    // Draw right-page contour (orange)
                    if (!rightDetectedPts_.empty()) {
                        vector<vector<cv::Point>> c = {rightDetectedPts_};
                        polylines(display, c, true, Scalar(0, 140, 255), 3, LINE_AA);
                        for (auto& p : rightDetectedPts_)
                            circle(display, p, 8, Scalar(0, 200, 255), -1, LINE_AA);
                    }
                    break;
                }
                case EDGES: {
                    // Show combined edge images side by side (edged_ not populated in book mode)
                    display = currentImage_.clone();
                    break;
                }
                case RESULT: {
                    // Both pages processed by pipeline, side by side
                    Mat l = leftPageResult_.empty()  ? Mat(400, 300, CV_8UC3, Scalar(30,30,30)) : leftPageResult_.clone();
                    Mat r = rightPageResult_.empty() ? Mat(400, 300, CV_8UC3, Scalar(30,30,30)) : rightPageResult_.clone();

                    // Add "Left" / "Right" labels
                    if (!leftPageResult_.empty())
                        putText(l, "Left Page",  Point(10, 28), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(80,200,255), 2, LINE_AA);
                    if (!rightPageResult_.empty())
                        putText(r, "Right Page", Point(10, 28), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(80,200,255), 2, LINE_AA);

                    display = stitchPages(l, r);
                    break;
                }
                case COMPARE: {
                    // Left: original resized; Right: both pages side by side
                    Mat orig = currentImage_.clone();
                    Mat processed = stitchPages(leftPageResult_, rightPageResult_);
                    if (processed.empty())
                        processed = Mat(orig.size(), CV_8UC3, Scalar(30,30,30));
                    // Normalise heights
                    if (processed.rows != orig.rows && processed.rows > 0) {
                        double sc = (double)orig.rows / processed.rows;
                        cv::resize(processed, processed, Size((int)(processed.cols*sc), orig.rows));
                    }
                    const int gap = 4;
                    display = Mat(orig.rows, orig.cols + gap + processed.cols, CV_8UC3, Scalar(50,50,50));
                    orig.copyTo(display(Rect(0, 0, orig.cols, orig.rows)));
                    processed.copyTo(display(Rect(orig.cols + gap, 0, processed.cols, orig.rows)));
                    line(display, Point(orig.cols+1,0), Point(orig.cols+1,display.rows),
                         Scalar(255,200,0), 2, LINE_AA);
                    break;
                }
            }
            imageDisplay_->setImage(matToQPixmap(display));
            return;
        }

        // ── Standard single-page views ────────────────────────────────────────
        switch (viewMode_) {
            case SOURCE: {
                display = currentImage_.clone();
                // Draw detected corners overlay
                if (!detectedPoints_.empty()) {
                    double scaleFactor = docDetector_.resizeScale * docDetector_.scale;
                    if (scaleFactor > 0.0) {
                        vector<cv::Point> scaled;
                        for (auto& p : detectedPoints_)
                            scaled.push_back(cv::Point(
                                (int)(p.x / scaleFactor),
                                (int)(p.y / scaleFactor)));
                        vector<vector<cv::Point>> contours = {scaled};
                        polylines(display, contours, true, Scalar(0,200,255), 3, LINE_AA);
                        for (auto& p : scaled)
                            circle(display, p, 8, Scalar(0,255,100), -1, LINE_AA);
                    }
                }
                // Draw gutter line even when not in book mode (informational)
                if (gutterXResized_ > 0) {
                    double sf = docDetector_.resizeScale * docDetector_.scale;
                    if (sf > 0.0) {
                        int gx = (int)(gutterXResized_ / sf);
                        line(display, Point(gx, 0), Point(gx, display.rows),
                             Scalar(80, 80, 255), 2, LINE_AA);
                    }
                }
                break;
            }
            case EDGES: {
                if (!edged_.empty()) {
                    if (edged_.channels() == 1)
                        cvtColor(edged_, display, COLOR_GRAY2BGR);
                    else
                        display = edged_.clone();
                    if (!resizedImage_.empty() && resizedImage_.rows > 0) {
                        double scaleBack = (double)currentImage_.rows / resizedImage_.rows;
                        cv::resize(display, display, Size(), scaleBack, scaleBack, INTER_LINEAR);
                    }
                } else {
                    display = currentImage_.clone();
                }
                break;
            }
            case RESULT: {
                if (!resultImage_.empty())
                    display = resultImage_.clone();
                else {
                    display = Mat(300, 400, CV_8UC3, Scalar(30,30,30));
                    putText(display, "No result", Point(80,160),
                            FONT_HERSHEY_SIMPLEX, 1.2, Scalar(120,120,120), 2);
                }
                break;
            }
            case COMPARE: {
                const int gap = 4;
                Mat left  = currentImage_.clone();
                Mat right = resultImage_.empty()
                           ? Mat(left.size(), CV_8UC3, Scalar(30,30,30))
                           : resultImage_.clone();
                if (right.rows != left.rows && right.rows > 0) {
                    double sc = (double)left.rows / right.rows;
                    cv::resize(right, right, Size((int)(right.cols*sc), left.rows));
                }
                display = Mat(left.rows, left.cols + right.cols + gap, CV_8UC3, Scalar(50,50,50));
                left.copyTo( display(Rect(0,                0, left.cols,  left.rows)));
                right.copyTo(display(Rect(left.cols + gap,  0, right.cols, left.rows)));
                line(display, Point(left.cols + gap/2, 0), Point(left.cols + gap/2, display.rows),
                     Scalar(255,200,0), 2, LINE_AA);
                break;
            }
        }

        imageDisplay_->setImage(matToQPixmap(display));
    }

    // ----- UI construction -----

    void buildUI() {
        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* mainHl = new QHBoxLayout(central);
        mainHl->setContentsMargins(4,4,4,4);
        mainHl->setSpacing(4);

        auto* mainSplit = new QSplitter(Qt::Horizontal, central);
        mainSplit->setHandleWidth(5);

        // ── Left: image file list ──
        auto* leftPanel = new QWidget;
        auto* leftVl    = new QVBoxLayout(leftPanel);
        leftVl->setContentsMargins(0,0,0,0); leftVl->setSpacing(2);
        auto* folderLbl = new QLabel("Images", leftPanel);
        folderLbl->setStyleSheet("font-weight: bold; padding: 4px;");
        fileList_ = new QListWidget(leftPanel);
        fileList_->setIconSize(QSize(88,66));
        fileList_->setSpacing(2);
        fileList_->setViewMode(QListView::ListMode);
        fileList_->setResizeMode(QListView::Adjust);
        connect(fileList_, &QListWidget::currentRowChanged,
                this, &ScannerWindow::onFileClicked);
        leftVl->addWidget(folderLbl);
        leftVl->addWidget(fileList_,1);
        leftPanel->setMinimumWidth(120);
        leftPanel->setMaximumWidth(240);

        // ── Center: image view ──
        auto* centerPanel = new QWidget;
        auto* centerVl    = new QVBoxLayout(centerPanel);
        centerVl->setContentsMargins(0,0,0,0); centerVl->setSpacing(2);

        // View mode buttons bar
        auto* viewBar = new QWidget(centerPanel);
        viewBar->setFixedHeight(36);
        auto* viewHl  = new QHBoxLayout(viewBar);
        viewHl->setContentsMargins(4,2,4,2); viewHl->setSpacing(4);
        auto* viewBtnGroup = new QButtonGroup(viewBar);
        viewBtnGroup_ = viewBtnGroup;
        viewBtnGroup->setExclusive(true);
        static const QString viewNames[] = {"Source","Edges","Result","⟺ Compare"};
        for (int i = 0; i < 4; ++i) {
            auto* btn = new QPushButton(viewNames[i], viewBar);
            btn->setCheckable(true);
            btn->setFixedHeight(28);
            if (i == 0) btn->setChecked(true);
            viewBtnGroup->addButton(btn, i);
            viewHl->addWidget(btn);
        }
        viewHl->addStretch();
        connect(viewBtnGroup, &QButtonGroup::idClicked,
                this, &ScannerWindow::onViewChanged);

        imageDisplay_ = new ImageDisplayWidget(centerPanel);
        centerVl->addWidget(viewBar);
        centerVl->addWidget(imageDisplay_, 1);

        // ── Right: pipeline + detection settings ──
        auto* rightSplit = new QSplitter(Qt::Vertical);
        rightSplit->setHandleWidth(4);

        pipelineWidget_ = new AlgorithmPipelineWidget(catalog_, presets_);
        connect(pipelineWidget_, &AlgorithmPipelineWidget::pipelineChanged,
                this, [this]{ debounceTimer_->start(); });

        detSettings_ = new DetectionSettingsWidget;
        connect(detSettings_, &DetectionSettingsWidget::settingsChanged,
                this, [this]{ debounceTimer_->start(); });

        auto* detGb = new QGroupBox("Detection Settings");
        auto* detVl = new QVBoxLayout(detGb);
        detVl->setContentsMargins(0,0,0,0);
        detVl->addWidget(detSettings_);

        rightSplit->addWidget(pipelineWidget_);
        rightSplit->addWidget(detGb);
        rightSplit->setStretchFactor(0, 2);
        rightSplit->setStretchFactor(1, 1);

        auto* rightPanel = new QWidget;
        auto* rightVl    = new QVBoxLayout(rightPanel);
        rightVl->setContentsMargins(0,0,0,0);
        rightVl->addWidget(rightSplit);
        rightPanel->setMinimumWidth(260);
        rightPanel->setMaximumWidth(420);

        mainSplit->addWidget(leftPanel);
        mainSplit->addWidget(centerPanel);
        mainSplit->addWidget(rightPanel);
        mainSplit->setStretchFactor(0, 0);
        mainSplit->setStretchFactor(1, 1);
        mainSplit->setStretchFactor(2, 0);
        mainSplit->setSizes({180, 1000, 320});

        mainHl->addWidget(mainSplit);
    }

    void setupMenuBar() {
        auto* fileMenu = menuBar()->addMenu("&File");
        fileMenu->addAction("&Open Folder…", this, &ScannerWindow::onOpenFolder,
                            QKeySequence::Open);
        fileMenu->addSeparator();
        fileMenu->addAction("&Save Result…", this, &ScannerWindow::onSaveResult,
                            QKeySequence::Save);
        fileMenu->addSeparator();
        fileMenu->addAction("E&xit", this, &QWidget::close, QKeySequence::Quit);

        // Presets menu
        auto* presetsMenu = menuBar()->addMenu("&Presets");
        presetsMenu->setToolTipsVisible(true);
        for (int i = 0; i < (int)presets_.size(); ++i) {
            const auto& pr = presets_[i];
            QAction* act = presetsMenu->addAction(pr.name, this, [this, i]{
                pipelineWidget_->loadPresetByIndex(i);
                viewMode_ = RESULT;
                if (viewBtnGroup_)
                    viewBtnGroup_->button(RESULT)->setChecked(true);
                // "Book Scan" preset (index 0) auto-enables book mode
                if (i == 0 && bookModeAction_) {
                    bookModeAction_->setChecked(true);
                }
                debounceTimer_->start();
            });
            act->setToolTip(pr.description);
            act->setStatusTip(pr.description.split('\n').first());
        }
        presetsMenu->addSeparator();
        presetsMenu->addAction("Clear Pipeline", this, [this]{
            pipelineWidget_->clearPipeline();
        });

        auto* viewMenu = menuBar()->addMenu("&View");
        viewMenu->addAction("Fit Image", this, [this]{
            if (imageDisplay_) imageDisplay_->fitToWindow();
        }, QKeySequence("F"));

        auto* navMenu = menuBar()->addMenu("&Navigate");
        navMenu->addAction("Previous Image", this, &ScannerWindow::onPrevImage,
                           QKeySequence(Qt::Key_Left));
        navMenu->addAction("Next Image",     this, &ScannerWindow::onNextImage,
                           QKeySequence(Qt::Key_Right));
    }

    void setupToolBar() {
        auto* tb = addToolBar("Main");
        tb->setMovable(false);
        tb->addAction("📂 Open", this, &ScannerWindow::onOpenFolder);
        tb->addSeparator();
        tb->addAction("◀ Prev", this, &ScannerWindow::onPrevImage);
        tb->addAction("▶ Next", this, &ScannerWindow::onNextImage);
        tb->addSeparator();

        // Book Mode toggle
        bookModeAction_ = tb->addAction("📖 Book Mode");
        bookModeAction_->setCheckable(true);
        bookModeAction_->setChecked(false);
        bookModeAction_->setToolTip(
            "Book Mode: detect gutter, split into left and right pages,\n"
            "process each independently and display side by side.\n"
            "Uses a scantailor-inspired darkness+gradient valley detector.");
        connect(bookModeAction_, &QAction::toggled, this, [this](bool on) {
            bookMode_ = on;
            debounceTimer_->start();
        });
        tb->addSeparator();

        // Quick-load presets from toolbar
        auto* presetTbBtn = new QPushButton("🎛 Presets ▾", tb);
        presetTbBtn->setFlat(true);
        presetTbBtn->setStyleSheet("padding: 3px 8px;");
        presetTbBtn->setToolTip("Quick-load a pipeline preset");
        connect(presetTbBtn, &QPushButton::clicked, this, [this, presetTbBtn]{
            QMenu m(this);
            for (int i = 0; i < (int)presets_.size(); ++i) {
                const auto& pr = presets_[i];
                m.addAction(pr.name, this, [this, i]{
                    pipelineWidget_->loadPresetByIndex(i);
                    debounceTimer_->start();
                });
            }
            m.exec(presetTbBtn->mapToGlobal(QPoint(0, presetTbBtn->height())));
        });
        tb->addWidget(presetTbBtn);

        tb->addSeparator();
        tb->addAction("💾 Save Result", this, &ScannerWindow::onSaveResult);
    }

    // ----- Members -----

    QVector<AlgoDef>          catalog_;
    QVector<PipelinePreset>   presets_;
    detector::DocumentDetector docDetector_;

    vector<string>          images_;
    int                     currentIdx_ = 0;
    Mat                     currentImage_, resizedImage_, edged_, warped_, resultImage_;
    vector<cv::Point>       detectedPoints_;
    ViewMode                viewMode_ = SOURCE;

    // Book Mode state
    bool                    bookMode_    = false; ///< user toggled book mode
    bool                    gutterFound_ = false; ///< gutter detected this frame
    int                     gutterXResized_ = -1; ///< gutter column in resizedImage coords
    Mat                     leftPageWarped_,  rightPageWarped_;
    Mat                     leftPageResult_,  rightPageResult_;
    vector<cv::Point>       leftDetectedPts_, rightDetectedPts_;

    // Widgets
    QListWidget*            fileList_       = nullptr;
    ImageDisplayWidget*     imageDisplay_   = nullptr;
    AlgorithmPipelineWidget* pipelineWidget_ = nullptr;
    DetectionSettingsWidget* detSettings_   = nullptr;
    QTimer*                 debounceTimer_  = nullptr;
    QButtonGroup*           viewBtnGroup_   = nullptr; ///< View mode toggle buttons
    QAction*                bookModeAction_ = nullptr; ///< Book Mode toolbar toggle
};

// ============================================================
//  Dark theme
// ============================================================

static void applyDarkTheme(QApplication& app) {
    app.setStyle("Fusion");
    QPalette p;
    p.setColor(QPalette::Window,          QColor(45,45,45));
    p.setColor(QPalette::WindowText,      QColor(240,240,240));
    p.setColor(QPalette::Base,            QColor(30,30,30));
    p.setColor(QPalette::AlternateBase,   QColor(50,50,50));
    p.setColor(QPalette::ToolTipBase,     QColor(60,60,60));
    p.setColor(QPalette::ToolTipText,     QColor(240,240,240));
    p.setColor(QPalette::Text,            QColor(240,240,240));
    p.setColor(QPalette::Button,          QColor(60,60,60));
    p.setColor(QPalette::ButtonText,      QColor(240,240,240));
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Link,            QColor(74,158,255));
    p.setColor(QPalette::Highlight,       QColor(74,158,255));
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::Mid,             QColor(90,90,90));
    p.setColor(QPalette::Shadow,          QColor(20,20,20));
    app.setPalette(p);

    app.setStyleSheet(R"(
QMainWindow { background-color: #2D2D2D; }
QSplitter::handle { background-color: #444444; }

QGroupBox {
    border: 1px solid #5A5A5A;
    border-radius: 5px;
    margin-top: 8px;
    font-weight: bold;
    color: #D0D0D0;
}
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }

QPushButton {
    background-color: #3C3C3C;
    color: #F0F0F0;
    border: 1px solid #5A5A5A;
    border-radius: 4px;
    padding: 4px 10px;
    min-height: 22px;
}
QPushButton:hover   { background-color: #4A4A4A; border-color: #7A7A7A; }
QPushButton:pressed { background-color: #282828; }
QPushButton:checked { background-color: #1A6ECC; border-color: #4A9EFF; color: white; }

QSlider::groove:horizontal {
    height: 4px;
    background: #555555;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    width: 14px; height: 14px;
    background: #4A9EFF;
    border-radius: 7px;
    margin: -5px 0;
}
QSlider::sub-page:horizontal { background: #4A9EFF; border-radius: 2px; }

QSpinBox, QDoubleSpinBox, QComboBox {
    background-color: #3C3C3C;
    color: #F0F0F0;
    border: 1px solid #5A5A5A;
    border-radius: 3px;
    padding: 1px 4px;
}
QComboBox::drop-down { border: none; }
QComboBox::down-arrow { image: none; width: 0; }
QComboBox QAbstractItemView {
    background-color: #3C3C3C;
    color: #F0F0F0;
    border: 1px solid #5A5A5A;
    selection-background-color: #1A6ECC;
}

QListWidget {
    background-color: #252525;
    color: #F0F0F0;
    border: 1px solid #5A5A5A;
    border-radius: 4px;
    outline: none;
}
QListWidget::item { padding: 4px; border-radius: 3px; }
QListWidget::item:selected { background-color: #1A6ECC; color: white; }
QListWidget::item:hover    { background-color: #3A3A3A; }

QScrollBar:vertical {
    background: #2D2D2D; width: 10px;
    border: none; border-radius: 5px;
}
QScrollBar::handle:vertical {
    background: #5A5A5A; border-radius: 5px; min-height: 20px;
}
QScrollBar::handle:vertical:hover { background: #7A7A7A; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal {
    background: #2D2D2D; height: 10px;
    border: none; border-radius: 5px;
}
QScrollBar::handle:horizontal {
    background: #5A5A5A; border-radius: 5px; min-width: 20px;
}

QMenuBar { background-color: #2D2D2D; color: #F0F0F0; border-bottom: 1px solid #444444; }
QMenuBar::item:selected { background-color: #1A6ECC; border-radius: 3px; }
QMenu {
    background-color: #3C3C3C; color: #F0F0F0;
    border: 1px solid #5A5A5A;
}
QMenu::item:selected { background-color: #1A6ECC; }
QMenu::separator     { height: 1px; background: #5A5A5A; margin: 3px 6px; }

QToolBar { background-color: #2D2D2D; border-bottom: 1px solid #444444; spacing: 3px; }
QToolBar QToolButton {
    background: transparent; color: #F0F0F0;
    border: 1px solid transparent; border-radius: 4px;
    padding: 3px 7px;
}
QToolBar QToolButton:hover   { background-color: #4A4A4A; border-color: #5A5A5A; }
QToolBar QToolButton:pressed { background-color: #282828; }

QStatusBar    { background-color: #252525; color: #A0A0A0; }
QScrollArea   { border: none; }
QLabel        { color: #F0F0F0; }
QCheckBox     { color: #F0F0F0; spacing: 5px; }
QCheckBox::indicator {
    width: 14px; height: 14px;
    border: 1px solid #5A5A5A; border-radius: 2px;
    background: #3C3C3C;
}
QCheckBox::indicator:checked { background: #4A9EFF; border-color: #4A9EFF; }
)");
}

// ============================================================
//  main
// ============================================================

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    applyDarkTheme(app);

    if (argc < 2) {
        QMessageBox::critical(nullptr, "Usage",
            "Usage:  scanner  <image_folder_path>  [start_image_name]");
        return 1;
    }

    const string dirPath   = argv[1];
    const string startName = (argc > 2) ? argv[2] : "";

    auto catalog = buildCatalog();
    auto presets = buildPresets();
    ScannerWindow win(catalog, presets);
    win.show();

    win.loadFolder(QString::fromStdString(dirPath));

    if (!startName.empty()) {
        auto imgs = loadImagesFromFolder(dirPath);
        for (int i = 0; i < (int)imgs.size(); ++i) {
            if (imgs[i].find(startName) != string::npos) {
                win.loadImage(i);
                break;
            }
        }
    }

    return app.exec();
}

// Required by CMAKE_AUTOMOC when Q_OBJECT is in a .cpp file
#include "scanner.moc"
