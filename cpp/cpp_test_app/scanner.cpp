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
                                     QWidget* parent = nullptr)
        : QWidget(parent), catalog_(catalog)
    {
        auto* mainVl = new QVBoxLayout(this);
        mainVl->setContentsMargins(0,0,0,0);
        mainVl->setSpacing(0);

        // ---- Top: pipeline list ----
        auto* listGb = new QGroupBox("Algorithm Pipeline", this);
        auto* listVl = new QVBoxLayout(listGb);
        listVl->setContentsMargins(4,4,4,4);
        listVl->setSpacing(4);

        // Add Step button
        auto* addBtn = new QPushButton("＋ Add Step", listGb);
        addBtn->setToolTip("Add a processing step to the pipeline");
        connect(addBtn, &QPushButton::clicked, this, &AlgorithmPipelineWidget::onAddStep);
        listVl->addWidget(addBtn);

        listWidget_ = new QListWidget(listGb);
        listWidget_->setDragDropMode(QAbstractItemView::InternalMove);
        listWidget_->setSelectionMode(QAbstractItemView::SingleSelection);
        listWidget_->setMinimumHeight(100);
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

private slots:
    void onAddStep() {
        QMenu menu(this);
        for (const auto& def : catalog_) {
            QAction* act = menu.addAction(def.name);
            act->setData(def.id);
            if (!def.implemented)
                act->setEnabled(true); // shown but styled differently
        }
        QAction* chosen = menu.exec(QCursor::pos());
        if (!chosen) return;
        QString id = chosen->data().toString();
        for (const auto& def : catalog_) {
            if (def.id == id) {
                pipeline_.push_back(makeStep(def));
                addListRow(pipeline_.back());
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
        // After a drag-reorder the list order may differ from pipeline_
        // We can't easily reorder pipeline_ since items lost their index.
        // Simplest: rebuild from scratch is handled by drag internally,
        // but QListWidget drag changes only the display. We reflect it:
        // (For simplicity, reorder pipeline_ to match list order)
        QVector<PipelineStep> newPipeline;
        for (int i = 0; i < listWidget_->count(); ++i) {
            // Find matching step by name (good enough for a test app)
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

    QVector<AlgoDef>     catalog_;
    QVector<PipelineStep> pipeline_;
    QListWidget*          listWidget_ = nullptr;
    ParamFormWidget*      paramForm_  = nullptr;
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
                           QWidget* parent = nullptr)
        : QMainWindow(parent)
        , catalog_(catalog)
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

        auto split = docDetector_.detectGutterAndSplit(resizedImage_, 0.4f);

        vector<vector<cv::Point>> pointsList;
        if (split.foundGutter) {
            Mat combinedEdged = Mat::zeros(resizedImage_.size(), CV_8U);
            auto scanAndMerge = [&](const Rect& r) {
                if (r.width <= 0 || r.height <= 0) return;
                Mat sub = resizedImage_(r);
                Mat subEdged;
                auto subList = docDetector_.scanPoint(subEdged, sub, true);
                if (!subEdged.empty()) {
                    if (subEdged.type() != combinedEdged.type())
                        cvtColor(subEdged, subEdged, COLOR_BGR2GRAY);
                    subEdged.copyTo(combinedEdged(r));
                }
                double sf = docDetector_.resizeScale * docDetector_.scale;
                Point off((int)(r.x*sf),(int)(r.y*sf));
                for (auto& c : subList) {
                    for (auto& pt : c) pt += off;
                    pointsList.push_back(c);
                }
            };
            if (split.hasLeft)  scanAndMerge(split.leftPage);
            if (split.hasRight) scanAndMerge(split.rightPage);
            if (pointsList.empty())
                pointsList = docDetector_.scanPoint(edged_, resizedImage_, true);
            else
                edged_ = combinedEdged;
        } else {
            pointsList = docDetector_.scanPoint(edged_, resizedImage_, true);
        }

        if (pointsList.empty()) {
            // Fall back to full-image rectangle
            pointsList.push_back({
                cv::Point(0,0),
                cv::Point(currentImage_.cols,0),
                cv::Point(currentImage_.cols,currentImage_.rows),
                cv::Point(0,currentImage_.rows)
            });
        }

        // Warp
        if (!pointsList.empty()) {
            detectedPoints_ = pointsList[0];
            warped_ = cropAndWarp(currentImage_, pointsList[0]);
        } else {
            detectedPoints_.clear();
            warped_ = Mat();
        }

        // Apply pipeline
        resultImage_ = warped_.empty() ? Mat() : warped_.clone();
        for (auto& step : pipelineWidget_->pipeline()) {
            if (!step.enabled || resultImage_.empty()) continue;
            applyStep(step, resultImage_);
        }

        long long ms = timer.elapsed();
        bool detected = !detectedPoints_.empty() &&
                        !(detectedPoints_.size() == 4 &&
                          detectedPoints_[0] == cv::Point(0,0) &&
                          detectedPoints_[2] == cv::Point(currentImage_.cols,currentImage_.rows));
        statusBar()->showMessage(
            QString("Image %1/%2  |  Pipeline: %3ms  |  Detection: %4")
            .arg(currentIdx_+1).arg((int)images_.size())
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
        switch (viewMode_) {
            case SOURCE: {
                display = currentImage_.clone();
                // Draw detected corners overlay
                if (!detectedPoints_.empty()) {
                    // Scale points back to original image coordinates
                    double scaleFactor = docDetector_.resizeScale * docDetector_.scale;
                    if (scaleFactor > 0.0) {
                        vector<cv::Point> scaled;
                        for (auto& p : detectedPoints_)
                            scaled.push_back(cv::Point(
                                (int)(p.x / scaleFactor),
                                (int)(p.y / scaleFactor)));
                        // Draw filled polygon with transparency-like effect
                        vector<vector<cv::Point>> contours = {scaled};
                        polylines(display, contours, true, Scalar(0,200,255), 3, LINE_AA);
                        for (auto& p : scaled)
                            circle(display, p, 8, Scalar(0,255,100), -1, LINE_AA);
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
                    // Scale back to original image size
                    if (!resizedImage_.empty() && resizedImage_.rows > 0) {
                        double scaleBack = (double)currentImage_.rows / resizedImage_.rows;
                        resize(display, display, Size(), scaleBack, scaleBack, INTER_LINEAR);
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
                Mat left  = currentImage_.clone();
                Mat right = resultImage_.empty()
                           ? Mat(left.size(), CV_8UC3, Scalar(30,30,30))
                           : resultImage_.clone();
                // Normalize heights
                if (right.rows != left.rows && right.rows > 0) {
                    double sc = (double)left.rows / right.rows;
                    resize(right, right, Size((int)(right.cols*sc), left.rows));
                }
                display = Mat(left.rows, left.cols + right.cols + 4, CV_8UC3, Scalar(50,50,50));
                left.copyTo( display(Rect(0,                   0, left.cols,  left.rows)));
                right.copyTo(display(Rect(left.cols+4,         0, right.cols, left.rows)));
                line(display, Point(left.cols+1,0), Point(left.cols+1,display.rows),
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

        pipelineWidget_ = new AlgorithmPipelineWidget(catalog_);
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
        tb->addAction("💾 Save Result", this, &ScannerWindow::onSaveResult);
    }

    // ----- Members -----

    QVector<AlgoDef>        catalog_;
    detector::DocumentDetector docDetector_;

    vector<string>          images_;
    int                     currentIdx_ = 0;
    Mat                     currentImage_, resizedImage_, edged_, warped_, resultImage_;
    vector<cv::Point>       detectedPoints_;
    ViewMode                viewMode_ = SOURCE;

    // Widgets
    QListWidget*            fileList_       = nullptr;
    ImageDisplayWidget*     imageDisplay_   = nullptr;
    AlgorithmPipelineWidget* pipelineWidget_ = nullptr;
    DetectionSettingsWidget* detSettings_   = nullptr;
    QTimer*                 debounceTimer_  = nullptr;
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

QSpinBox, QDoubleSpinBox {
    background-color: #3C3C3C;
    color: #F0F0F0;
    border: 1px solid #5A5A5A;
    border-radius: 3px;
    padding: 1px 4px;
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
    ScannerWindow win(catalog);
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
