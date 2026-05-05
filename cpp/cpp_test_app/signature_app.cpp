/*
 * signature_app.cpp
 *
 * Pipeline overview:
 *   Camera ROI  →  detect (adaptive threshold + morphology + hole punch)
 *                       ↓
 *              [Upscale mask 4× via cubic]  ←── all rendering happens in hi-res space
 *                       ↓              ↓
 *               PNG path            SVG / vector overlay
 *   QPainter renders filled contour    skeleton → smooth Bézier centre-line
 *   paths with AA at hi-res, then      → per-point DT radius → offset polygon
 *   downscales for sharp crisp output  → Bézier-fitted outline → filled path
 */

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QGroupBox>
#include <QPainter>
#include <QPainterPath>
#include <QFile>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QComboBox>
#include <QTextStream>
#include <QFont>

#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>

#include <opencv2/opencv.hpp>
#ifdef HAVE_OPENCV_XIMGPROC
#include <opencv2/ximgproc.hpp>
#endif

#include <cmath>
#include <vector>
#include <array>
#include <deque>
#include <numeric>
#include <algorithm>

// ═════════════════════════════════════════════════════════════════════════════
//  Cubic Bézier fitting  (Schneider 1990, recursive least-squares)
// ═════════════════════════════════════════════════════════════════════════════
namespace BezFit {
using P2 = cv::Point2f;
inline float len (P2 a,P2 b){float dx=a.x-b.x,dy=a.y-b.y;return std::sqrt(dx*dx+dy*dy);}
inline P2   sub  (P2 a,P2 b){return{a.x-b.x,a.y-b.y};}
inline P2   add  (P2 a,P2 b){return{a.x+b.x,a.y+b.y};}
inline P2   mul  (P2 a,float s){return{a.x*s,a.y*s};}
inline float dot (P2 a,P2 b){return a.x*b.x+a.y*b.y;}
inline P2   nrm  (P2 a){float l=std::sqrt(dot(a,a));return l>1e-6f?mul(a,1/l):a;}
inline P2   lerp (P2 a,P2 b,float t){return add(mul(a,1-t),mul(b,t));}

struct Cubic{P2 p0,p1,p2,p3;};

static std::vector<float> chordParam(const std::vector<P2>&pts){
    int n=int(pts.size());std::vector<float>u(n,0);
    for(int i=1;i<n;i++)u[i]=u[i-1]+len(pts[i],pts[i-1]);
    float t=u.back();if(t>1e-6f)for(auto&v:u)v/=t;
    return u;
}
static P2 evalC(const Cubic&c,float t){
    float m=1-t;
    return add(add(mul(c.p0,m*m*m),mul(c.p1,3*m*m*t)),
               add(mul(c.p2,3*m*t*t),mul(c.p3,t*t*t)));
}
static Cubic fit1(const std::vector<P2>&pts,int f,int l,P2 t1,P2 t2,const std::vector<float>&u){
    int n=l-f+1;
    if(n==2){float d=len(pts[f],pts[l])/3.f;return{pts[f],add(pts[f],mul(t1,d)),add(pts[l],mul(t2,d)),pts[l]};}
    std::array<std::array<float,2>,2>C{};std::array<float,2>X{};
    for(int i=0;i<n;i++){
        float t=u[f+i],m=1-t;
        P2 A0=mul(t1,3*m*m*t),A1=mul(t2,3*m*t*t);
        C[0][0]+=dot(A0,A0);C[0][1]+=dot(A0,A1);C[1][0]=C[0][1];C[1][1]+=dot(A1,A1);
        P2 fix=add(mul(pts[f],m*m*m+3*m*m*t),mul(pts[l],3*m*t*t+t*t*t));
        P2 tmp=sub(pts[f+i],fix);
        X[0]+=dot(A0,tmp);X[1]+=dot(A1,tmp);
    }
    float det=C[0][0]*C[1][1]-C[0][1]*C[1][0];
    float a1=std::abs(det)<1e-12f?len(pts[f],pts[l])/3.f:(X[0]*C[1][1]-X[1]*C[0][1])/det;
    float a2=std::abs(det)<1e-12f?len(pts[f],pts[l])/3.f:(C[0][0]*X[1]-C[1][0]*X[0])/det;
    if(a1<1e-6f||a2<1e-6f){float d=len(pts[f],pts[l])/3.f;a1=a2=d;}
    return{pts[f],add(pts[f],mul(t1,a1)),add(pts[l],mul(t2,a2)),pts[l]};
}
static float errMax(const std::vector<P2>&pts,int f,int l,const Cubic&c,const std::vector<float>&u,int&sp){
    float mx=0;sp=(f+l)/2;
    for(int i=f+1;i<l;i++){float d=len(evalC(c,u[i]),pts[i]);if(d>mx){mx=d;sp=i;}}
    return mx;
}
static void fitRec(const std::vector<P2>&pts,int f,int l,P2 t1,P2 t2,float tol2,
                   std::vector<Cubic>&out,const std::vector<float>&u){
    if(l<=f)return;
    if(l-f==1){float d=len(pts[f],pts[l])/3.f;out.push_back({pts[f],add(pts[f],mul(t1,d)),add(pts[l],mul(t2,d)),pts[l]});return;}
    Cubic c=fit1(pts,f,l,t1,t2,u);
    int sp;float e=errMax(pts,f,l,c,u,sp);
    if(e<tol2){out.push_back(c);return;}
    P2 mt=nrm(sub(pts[std::min(sp+1,l)],pts[std::max(sp-1,f)]));
    fitRec(pts,f,sp,t1,mt,tol2,out,u);
    fitRec(pts,sp,l,{-mt.x,-mt.y},t2,tol2,out,u);
}
std::vector<Cubic> fit(const std::vector<P2>&pts,float tol=2.0f){
    std::vector<Cubic>out;
    if(pts.size()<2)return out;
    auto u=chordParam(pts);int n=int(pts.size())-1;
    fitRec(pts,0,n,nrm(sub(pts[1],pts[0])),nrm(sub(pts[n-1],pts[n])),tol*tol,out,u);
    return out;
}

// Sample a cubic chain at uniform arc-length intervals
std::vector<P2> resample(const std::vector<Cubic>&crv, float spacing){
    if(crv.empty())return{};
    std::vector<P2>out;
    out.push_back(crv.front().p0);
    float accum=0;
    const int STEPS=20;
    for(auto&c:crv){
        for(int s=1;s<=STEPS;s++){
            float t=float(s)/STEPS;
            P2 p=evalC(c,t);
            float d=len(out.back(),p);
            accum+=d;
            if(accum>=spacing){out.push_back(p);accum=0;}
        }
    }
    return out;
}
} // BezFit

// ═════════════════════════════════════════════════════════════════════════════
//  Smooth a vector of floats with Gaussian weights
// ═════════════════════════════════════════════════════════════════════════════
static std::vector<float> gaussSmooth(const std::vector<float>&v,float sigma){
    int n=int(v.size());
    int w=int(std::ceil(sigma*3));
    std::vector<float>k(2*w+1);
    float sum=0;
    for(int i=-w;i<=w;i++){k[i+w]=std::exp(-0.5f*i*i/(sigma*sigma));sum+=k[i+w];}
    for(auto&x:k)x/=sum;
    std::vector<float>out(n);
    for(int i=0;i<n;i++){
        float s=0;
        for(int j=-w;j<=w;j++){int idx=std::clamp(i+j,0,n-1);s+=v[idx]*k[j+w];}
        out[i]=s;
    }
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Skeleton extraction
// ═════════════════════════════════════════════════════════════════════════════
namespace Skel {

struct Stroke {
    std::vector<cv::Point2f> pts;
    std::vector<float>       rad;   // DT half-width at each point
};

static const int dx8[8]={1,1,0,-1,-1,-1,0,1};
static const int dy8[8]={0,1,1,1,0,-1,-1,-1};

static cv::Mat skeletonize(const cv::Mat&bin){
    cv::Mat s=bin.clone();
#ifdef HAVE_OPENCV_XIMGPROC
    cv::ximgproc::thinning(s,s,cv::ximgproc::THINNING_ZHANGSUEN);
#else
    // Iterative morphological thinning fallback
    cv::Mat el=cv::getStructuringElement(cv::MORPH_CROSS,{3,3});
    cv::Mat skel=cv::Mat::zeros(s.size(),CV_8U);
    while(true){
        cv::Mat eroded,opened,tmp;
        cv::erode(s,eroded,el);
        cv::dilate(eroded,opened,el);
        cv::subtract(s,opened,tmp);
        cv::bitwise_or(skel,tmp,skel);
        eroded.copyTo(s);
        if(cv::countNonZero(s)==0)break;
    }
    return skel;
#endif
    return s;
}

static int deg(const cv::Mat&sk,int y,int x){
    int d=0,rows=sk.rows,cols=sk.cols;
    for(int k=0;k<8;k++){int ny=y+dy8[k],nx=x+dx8[k];
        if(ny>=0&&ny<rows&&nx>=0&&nx<cols&&sk.at<uchar>(ny,nx))d++;}
    return d;
}

std::vector<Stroke> extract(const cv::Mat&mask,const cv::Mat&dt,float minLen=8.f){
    cv::Mat skel=skeletonize(mask);
    int rows=skel.rows,cols=skel.cols;
    cv::Mat vis=cv::Mat::zeros(skel.size(),CV_8U);
    std::vector<Stroke>strokes;

    // Collect all skeleton pixels, prioritise endpoints as seeds
    std::vector<cv::Point>seeds,rest;
    for(int y=0;y<rows;y++)
        for(int x=0;x<cols;x++)
            if(skel.at<uchar>(y,x)){
                int d=deg(skel,y,x);
                if(d<=1)seeds.push_back({x,y});
                else rest.push_back({x,y});
            }
    seeds.insert(seeds.end(),rest.begin(),rest.end());

    for(auto&seed:seeds){
        if(!skel.at<uchar>(seed.y,seed.x)||vis.at<uchar>(seed.y,seed.x))continue;
        Stroke st;
        cv::Point cur=seed;
        while(true){
            if(vis.at<uchar>(cur.y,cur.x))break;
            vis.at<uchar>(cur.y,cur.x)=255;
            st.pts.push_back({float(cur.x),float(cur.y)});
            st.rad.push_back(dt.at<float>(cur.y,cur.x));
            cv::Point best(-1,-1);int bestD=99;
            for(int k=0;k<8;k++){
                int ny=cur.y+dy8[k],nx=cur.x+dx8[k];
                if(ny<0||ny>=rows||nx<0||nx>=cols)continue;
                if(!skel.at<uchar>(ny,nx)||vis.at<uchar>(ny,nx))continue;
                int d=deg(skel,ny,nx);
                if(d<bestD){bestD=d;best={nx,ny};}
            }
            if(best.x<0)break;
            cur=best;
        }
        if(st.pts.size()<3)continue;
        float arcLen=0;
        for(int i=1;i<int(st.pts.size());i++)arcLen+=BezFit::len(st.pts[i],st.pts[i-1]);
        if(arcLen<minLen)continue;
        strokes.push_back(std::move(st));
    }
    return strokes;
}
} // Skel

// ═════════════════════════════════════════════════════════════════════════════
//  Pen-stroke → filled QPainterPath
//  Strategy: fit Bézier to centre-line, resample uniformly, compute offset
//  profiles from smoothed radius, fit Bézier to each offset side, close path.
// ═════════════════════════════════════════════════════════════════════════════
struct PenParams {
    float widthScale;  // multiplier on DT radius  (default 1.0)
    float minRadius;   // minimum half-width in source pixels
    float taper;       // 0=none 1=full sin-arch taper at ends
    float fitTol;      // Bézier fit tolerance
};

static QPainterPath makePenPath(const std::vector<cv::Point2f>&rawPts,
                                 const std::vector<float>&rawRad,
                                 const PenParams&pp)
{
    using P2=BezFit::P2;
    int n=int(rawPts.size());
    if(n<2)return{};

    // 1. Fit Bézier to raw centre-line, resample at ~2px spacing for smoothness
    auto centreCrv=BezFit::fit(rawPts,pp.fitTol);
    if(centreCrv.empty())return{};
    auto cPts=BezFit::resample(centreCrv,2.0f);
    int m=int(cPts.size());
    if(m<2)return{};

    // 2. Re-sample radii at the same arc positions via linear interpolation on originals
    //    Build cumulative arc of original points
    std::vector<float>origArc(n,0);
    for(int i=1;i<n;i++)origArc[i]=origArc[i-1]+BezFit::len(rawPts[i],rawPts[i-1]);
    float totalArc=origArc.back();

    // Build cumulative arc of resampled centre
    std::vector<float>resArc(m,0);
    for(int i=1;i<m;i++)resArc[i]=resArc[i-1]+BezFit::len(cPts[i],cPts[i-1]);
    float resTotal=resArc.back();

    // Smooth raw radii first
    auto sRad=gaussSmooth(rawRad,3.0f);

    std::vector<float>rad(m);
    for(int i=0;i<m;i++){
        float t=resTotal>0?resArc[i]/resTotal:0;
        float s=t*totalArc;
        // find bracket in origArc
        int lo=0;
        for(int j=0;j<n-1;j++){if(origArc[j]<=s&&origArc[j+1]>=s){lo=j;break;}}
        float span=origArc[lo+1]-origArc[lo];
        float alpha=span>0?(s-origArc[lo])/span:0;
        rad[i]=sRad[lo]+alpha*(sRad[std::min(lo+1,n-1)]-sRad[lo]);
    }

    // 3. Apply width scale + taper envelope
    float minR=pp.minRadius;
    std::vector<float>halfW(m);
    for(int i=0;i<m;i++){
        float t=m>1?float(i)/(m-1):0.5f;
        float env=(1.f-pp.taper)+pp.taper*std::sin(float(M_PI)*t);
        halfW[i]=std::max(minR,rad[i]*pp.widthScale)*env;
    }
    // Extra smooth after taper
    halfW=gaussSmooth(halfW,2.0f);

    // 4. Per-point tangents (central diff)
    std::vector<P2>tang(m);
    tang[0]=BezFit::nrm(BezFit::sub(cPts[1],cPts[0]));
    tang[m-1]=BezFit::nrm(BezFit::sub(cPts[m-1],cPts[m-2]));
    for(int i=1;i<m-1;i++)tang[i]=BezFit::nrm(BezFit::sub(cPts[i+1],cPts[i-1]));

    // 5. Build left / right offset profiles
    auto perp=[](P2 t)->P2{return{-t.y,t.x};};
    std::vector<P2>left(m),right(m);
    for(int i=0;i<m;i++){
        P2 no=BezFit::mul(perp(tang[i]),halfW[i]);
        left[i] ={cPts[i].x+no.x,cPts[i].y+no.y};
        right[i]={cPts[i].x-no.x,cPts[i].y-no.y};
    }

    // 6. Fit Bézier to each offset side
    auto lCrv=BezFit::fit(left, 1.2f);
    std::vector<P2>rightRev(right.rbegin(),right.rend());
    auto rCrv=BezFit::fit(rightRev,1.2f);

    // 7. Build closed QPainterPath
    QPainterPath path;
    if(lCrv.empty())return{};
    path.moveTo(lCrv[0].p0.x,lCrv[0].p0.y);
    for(auto&c:lCrv)path.cubicTo(c.p1.x,c.p1.y,c.p2.x,c.p2.y,c.p3.x,c.p3.y);
    // small round cap at end: arc through the right endpoint
    path.lineTo(right[m-1].x,right[m-1].y);
    for(auto&c:rCrv)path.cubicTo(c.p1.x,c.p1.y,c.p2.x,c.p2.y,c.p3.x,c.p3.y);
    path.closeSubpath();
    return path;
}

// SVG version of the same pen path
static QString makePenSVGPath(const std::vector<cv::Point2f>&rawPts,
                               const std::vector<float>&rawRad,
                               const PenParams&pp)
{
    using P2=BezFit::P2;
    int n=int(rawPts.size());
    if(n<2)return{};

    auto centreCrv=BezFit::fit(rawPts,pp.fitTol);
    if(centreCrv.empty())return{};
    auto cPts=BezFit::resample(centreCrv,2.0f);
    int m=int(cPts.size());if(m<2)return{};

    std::vector<float>origArc(n,0);
    for(int i=1;i<n;i++)origArc[i]=origArc[i-1]+BezFit::len(rawPts[i],rawPts[i-1]);
    float totalArc=origArc.back();
    std::vector<float>resArc(m,0);
    for(int i=1;i<m;i++)resArc[i]=resArc[i-1]+BezFit::len(cPts[i],cPts[i-1]);
    float resTotal=resArc.back();
    auto sRad=gaussSmooth(rawRad,3.0f);
    std::vector<float>rad(m);
    for(int i=0;i<m;i++){
        float t=resTotal>0?resArc[i]/resTotal:0;
        float s=t*totalArc;
        int lo=0;
        for(int j=0;j<n-1;j++){if(origArc[j]<=s&&origArc[j+1]>=s){lo=j;break;}}
        float span=origArc[lo+1]-origArc[lo];
        float alpha=span>0?(s-origArc[lo])/span:0;
        rad[i]=sRad[lo]+alpha*(sRad[std::min(lo+1,n-1)]-sRad[lo]);
    }
    std::vector<float>halfW(m);
    for(int i=0;i<m;i++){
        float t=m>1?float(i)/(m-1):0.5f;
        float env=(1.f-pp.taper)+pp.taper*std::sin(float(M_PI)*t);
        halfW[i]=std::max(pp.minRadius,rad[i]*pp.widthScale)*env;
    }
    halfW=gaussSmooth(halfW,2.0f);

    std::vector<P2>tang(m);
    tang[0]=BezFit::nrm(BezFit::sub(cPts[1],cPts[0]));
    tang[m-1]=BezFit::nrm(BezFit::sub(cPts[m-1],cPts[m-2]));
    for(int i=1;i<m-1;i++)tang[i]=BezFit::nrm(BezFit::sub(cPts[i+1],cPts[i-1]));
    auto perp=[](P2 t)->P2{return{-t.y,t.x};};
    std::vector<P2>left(m),right(m);
    for(int i=0;i<m;i++){
        P2 no=BezFit::mul(perp(tang[i]),halfW[i]);
        left[i]={cPts[i].x+no.x,cPts[i].y+no.y};
        right[i]={cPts[i].x-no.x,cPts[i].y-no.y};
    }
    auto lCrv=BezFit::fit(left,1.2f);
    std::vector<P2>rightRev(right.rbegin(),right.rend());
    auto rCrv=BezFit::fit(rightRev,1.2f);
    if(lCrv.empty())return{};

    QString d;QTextStream s(&d);
    s.setRealNumberPrecision(2);s.setRealNumberNotation(QTextStream::FixedNotation);
    s<<"M "<<lCrv[0].p0.x<<" "<<lCrv[0].p0.y<<" ";
    for(auto&c:lCrv)s<<"C "<<c.p1.x<<" "<<c.p1.y<<" "<<c.p2.x<<" "<<c.p2.y<<" "<<c.p3.x<<" "<<c.p3.y<<" ";
    if(!rCrv.empty()){
        s<<"L "<<rCrv[0].p0.x<<" "<<rCrv[0].p0.y<<" ";
        for(auto&c:rCrv)s<<"C "<<c.p1.x<<" "<<c.p1.y<<" "<<c.p2.x<<" "<<c.p2.y<<" "<<c.p3.x<<" "<<c.p3.y<<" ";
    }
    s<<"Z";
    return d;
}

// ═════════════════════════════════════════════════════════════════════════════
//  CameraView
// ═════════════════════════════════════════════════════════════════════════════
class CameraView : public QWidget {
    Q_OBJECT
public:
    explicit CameraView(QWidget*p=nullptr):QWidget(p){
        setMinimumSize(640,480);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    }
    void setFrame(const QPixmap&cam,const QPixmap&ov,const QRect&sr,const QPixmap&dbg){
        m_cam=cam;m_ov=ov;m_sr=sr;m_dbg=dbg;QWidget::update();
    }
    QRect computeScanRect(const QSize&ws,const QSize&cs)const{
        if(cs.isEmpty())return{};
        float ratio=85.6f/54.f;
        float sc=std::max(float(ws.width())/cs.width(),float(ws.height())/cs.height());
        int visW=int(ws.width()/sc);
        int sw=int(visW*0.55f),sh=int(sw/ratio);
        return{(cs.width()-sw)/2,(cs.height()-sh)/2,sw,sh};
    }
protected:
    void paintEvent(QPaintEvent*)override{
        if(m_cam.isNull())return;
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);
        const QSize ws=size(),cs=m_cam.size();
        float sc=std::max(float(ws.width())/cs.width(),float(ws.height())/cs.height());
        int dw=int(cs.width()*sc),dh=int(cs.height()*sc);
        int dx=(ws.width()-dw)/2,dy=(ws.height()-dh)/2;
        p.drawPixmap(QRect(dx,dy,dw,dh),m_cam);
        auto toW=[&](QRect r)->QRect{
            return{dx+int(r.x()*sc),dy+int(r.y()*sc),int(r.width()*sc),int(r.height()*sc)};};
        QRect sr=toW(m_sr);
        if(!m_ov.isNull())p.drawPixmap(sr,m_ov);
        // Corner ticks
        p.setPen(QPen(QColor(255,255,255,210),2.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        int t=20;
        QPoint tl=sr.topLeft(),tr=sr.topRight(),bl=sr.bottomLeft(),br=sr.bottomRight();
        const QLine L[8]={{tl,tl+QPoint(t,0)},{tl,tl+QPoint(0,t)},
                          {tr,tr+QPoint(-t,0)},{tr,tr+QPoint(0,t)},
                          {bl,bl+QPoint(t,0)},{bl,bl+QPoint(0,-t)},
                          {br,br+QPoint(-t,0)},{br,br+QPoint(0,-t)}};
        p.drawLines(L,8);
        int by=sr.top()+int(sr.height()*0.65f);
        p.setPen(QPen(QColor(255,255,255,120),1));
        p.drawLine(sr.left()+10,by,sr.right()-10,by);
        p.setPen(QColor(255,255,255,140));
        QFont f=p.font();f.setPointSizeF(8.5);p.setFont(f);
        p.drawText(sr.left()+12,by+13,"Sign here");
        if(!m_dbg.isNull()){
            int tw=200,th=int(tw*float(m_dbg.height())/m_dbg.width());
            QRect dr(ws.width()-tw-10,ws.height()-th-10,tw,th);
            p.setOpacity(0.85);
            p.fillRect(dr.adjusted(-2,-2,2,2),QColor(0,0,0,170));
            p.drawPixmap(dr,m_dbg);
            p.setOpacity(1.0);
            p.setPen(QColor(255,255,255,50));p.drawRect(dr);
            p.setPen(QColor(255,255,255,100));
            QFont lf=p.font();lf.setPointSizeF(7);p.setFont(lf);
            p.drawText(dr.left()+3,dr.top()-2,"processed mask");
        }
    }
private:
    QPixmap m_cam,m_ov,m_dbg;
    QRect m_sr;
};

// ═════════════════════════════════════════════════════════════════════════════
//  MainWindow
// ═════════════════════════════════════════════════════════════════════════════
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(){
        setWindowTitle("Signature Scanner");
        setStyleSheet(R"(
            QWidget{background:#1c1c1e;color:#f0f0f0;font-size:12px;}
            QGroupBox{border:1px solid #3a3a3c;border-radius:6px;margin-top:10px;padding:6px 4px 4px 4px;}
            QGroupBox::title{subcontrol-origin:margin;left:8px;color:#98989d;font-size:11px;}
            QSlider::groove:horizontal{height:3px;background:#3a3a3c;border-radius:2px;}
            QSlider::handle:horizontal{width:13px;height:13px;margin:-5px 0;background:#fff;border-radius:7px;}
            QSlider::sub-page:horizontal{background:#0a84ff;border-radius:2px;}
            QPushButton{background:#2c2c2e;border:1px solid #48484a;border-radius:7px;padding:6px 10px;}
            QPushButton:hover{background:#3a3a3c;}
            QCheckBox::indicator{width:15px;height:15px;border-radius:4px;border:1px solid #555;background:#2c2c2e;}
            QCheckBox::indicator:checked{background:#0a84ff;border-color:#0a84ff;}
            QComboBox,QSpinBox,QDoubleSpinBox{background:#2c2c2e;border:1px solid #48484a;border-radius:5px;padding:3px 6px;}
        )");

        auto*root=new QHBoxLayout(this);
        root->setSpacing(0);root->setContentsMargins(0,0,0,0);
        cameraView=new CameraView;
        root->addWidget(cameraView,1);

        auto*sb=new QWidget;sb->setFixedWidth(262);
        sb->setStyleSheet("QWidget{background:#111;}");
        auto*sbl=new QVBoxLayout(sb);
        sbl->setContentsMargins(12,14,12,12);sbl->setSpacing(10);

        // ── Detection ───────────────────────────────────────────────────────
        auto*dg=new QGroupBox("Detection");
        auto*dl=new QFormLayout(dg);dl->setSpacing(5);
        threshBlock=mkSl(3,51,11);threshC=mkSl(-10,20,4);
        minArea=new QSpinBox;minArea->setRange(50,50000);minArea->setValue(1500);
        dl->addRow("Block size",threshBlock);
        dl->addRow("Thresh C",threshC);
        dl->addRow("Min area px²",minArea);

        // ── Processing ──────────────────────────────────────────────────────
        auto*pg=new QGroupBox("Processing");
        auto*pl=new QVBoxLayout(pg);pl->setSpacing(4);
        chkInvert=new QCheckBox("Invert (dark bg)");
        chkMorph =new QCheckBox("Morph close");chkMorph->setChecked(true);
        chkAuto  =new QCheckBox("Auto-capture");chkAuto->setChecked(true);
        pl->addWidget(chkInvert);pl->addWidget(chkMorph);pl->addWidget(chkAuto);

        // ── Overlay ─────────────────────────────────────────────────────────
        auto*og=new QGroupBox("Overlay");
        auto*ol=new QFormLayout(og);ol->setSpacing(5);
        overlayMode=new QComboBox;
        overlayMode->addItems({"PNG – crisp ink","Vector – pen strokes"});
        overlayOpacity=mkSl(10,100,92);
        ol->addRow("Mode",overlayMode);
        ol->addRow("Opacity %",overlayOpacity);

        // ── PNG rendering ───────────────────────────────────────────────────
        auto*xg=new QGroupBox("PNG rendering");
        auto*xl=new QFormLayout(xg);xl->setSpacing(5);
        pngDilate   =mkSl(0,8,1);         // source-px dilation before upscale
        pngBlur     =mkSl(0,20,3);        // ×0.1 sigma – gentle AA
        pngSharpen  =mkSl(0,30,15);       // ×0.1 unsharp amount
        pngOutScale =new QSpinBox;pngOutScale->setRange(1,8);pngOutScale->setValue(3);
        pngBezTol   =mkSl(1,30,6);        // ×0.1 contour fit tolerance
        xl->addRow("Thicken px",pngDilate);
        xl->addRow("AA blur ×0.1",pngBlur);
        xl->addRow("Sharpen ×0.1",pngSharpen);
        xl->addRow("Output scale",pngOutScale);
        xl->addRow("Contour tol ×0.1",pngBezTol);

        // ── SVG pen rendering ───────────────────────────────────────────────
        auto*vg=new QGroupBox("SVG / Vector pen");
        auto*vl=new QFormLayout(vg);vl->setSpacing(5);
        penBezTol =mkSl(5,30,15);   // ×0.1 centre-line fit tol
        penScale  =mkSl(5,30,12);   // ×0.1  width = DT_radius × this
        penMinRad =mkSl(2,30,8);    // ×0.1 min half-width px (in source space)
        penTaper  =mkSl(0,10,6);    // ×0.1 taper strength
        skelMinLen=mkSl(4,80,12);   // min skeleton arc length px
        vl->addRow("Fit tol ×0.1",penBezTol);
        vl->addRow("Width scale ×0.1",penScale);
        vl->addRow("Min width ×0.1",penMinRad);
        vl->addRow("Taper ×0.1",penTaper);
        vl->addRow("Min stroke px",skelMinLen);

        auto*saveBtn=new QPushButton("💾  Save PNG");
        auto*svgBtn =new QPushButton("📄  Save SVG");
        auto*clrBtn =new QPushButton("✕  Clear");

        sbl->addWidget(dg);sbl->addWidget(pg);sbl->addWidget(og);
        sbl->addWidget(xg);sbl->addWidget(vg);
        sbl->addSpacing(4);
        sbl->addWidget(saveBtn);sbl->addWidget(svgBtn);sbl->addWidget(clrBtn);
        sbl->addStretch();
        root->addWidget(sb);

        connect(saveBtn,&QPushButton::clicked,this,&MainWindow::savePNG);
        connect(svgBtn, &QPushButton::clicked,this,&MainWindow::saveSVG);
        connect(clrBtn, &QPushButton::clicked,this,[this]{capturedMask=cv::Mat();});

        camera=new QCamera(this);
        session=new QMediaCaptureSession(this);
        sink=new QVideoSink(this);
        session->setCamera(camera);session->setVideoSink(sink);
        connect(sink,&QVideoSink::videoFrameChanged,this,&MainWindow::processFrame);
        camera->start();
    }

private:
    CameraView *cameraView;
    QSlider *threshBlock,*threshC,*overlayOpacity;
    QSlider *pngDilate,*pngBlur,*pngSharpen,*pngBezTol;
    QSlider *penBezTol,*penScale,*penMinRad,*penTaper,*skelMinLen;
    QSpinBox *minArea,*pngOutScale;
    QCheckBox *chkInvert,*chkMorph,*chkAuto;
    QComboBox *overlayMode;

    QCamera *camera;QMediaCaptureSession *session;QVideoSink *sink;
    cv::Mat capturedMask,lastROI;

    QSlider*mkSl(int a,int b,int v){auto*s=new QSlider(Qt::Horizontal);s->setRange(a,b);s->setValue(v);return s;}
    int oddV(QSlider*s){int v=s->value();return v%2==0?v+1:v;}

    // ── Detection ────────────────────────────────────────────────────────────
    cv::Mat detect(const cv::Mat&roi){
        cv::Mat gray;cv::cvtColor(roi,gray,cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray,gray,cv::Size(3,3),0);
        cv::Mat bin;
        cv::adaptiveThreshold(gray,bin,255,cv::ADAPTIVE_THRESH_GAUSSIAN_C,
            chkInvert->isChecked()?cv::THRESH_BINARY:cv::THRESH_BINARY_INV,
            oddV(threshBlock),threshC->value());
        if(chkMorph->isChecked()){
            cv::Mat k=cv::getStructuringElement(cv::MORPH_ELLIPSE,{3,3});
            cv::morphologyEx(bin,bin,cv::MORPH_CLOSE,k,{-1,-1},1);
        }
        std::vector<std::vector<cv::Point>>ctrs;std::vector<cv::Vec4i>hier;
        cv::findContours(bin,ctrs,hier,cv::RETR_CCOMP,cv::CHAIN_APPROX_SIMPLE);
        cv::Mat clean=cv::Mat::zeros(bin.size(),CV_8U);
        int minA=minArea->value();
        for(int i=0;i<int(ctrs.size());i++){
            if(hier[i][3]!=-1)continue;
            if(cv::contourArea(ctrs[i])<minA)continue;
            cv::drawContours(clean,ctrs,i,255,cv::FILLED);
        }
        for(int i=0;i<int(ctrs.size());i++){
            if(hier[i][3]==-1)continue;
            if(cv::contourArea(ctrs[hier[i][3]])<minA)continue;
            cv::drawContours(clean,ctrs,i,0,cv::FILLED);
        }
        return clean;
    }


// ═════════════════════════════════════════════════════════════════════════════
//  High-quality PNG rendering from binary mask
//  - Upscale mask by `upScale`
//  - Render via QPainter with antialiasing (contour paths → filled)
//  - Apply optional unsharp mask for snap
//  - Downscale back (INTER_AREA)  → crisp result at target resolution
// ═════════════════════════════════════════════════════════════════════════════
 QImage renderPNG(const cv::Mat &binMask,
                         int upScale,          // 2,3,4
                         int dilateRadius,     // thicken strokes in source px
                         float blurSigma,      // output anti-alias sigma
                         float sharpenAmt,     // unsharp amount
                         int   outScale,       // output upscale relative to source (1=native)
                         float bezTol,         // contour fit tolerance (in hi-res space)
                         int   alpha)          // 0-255
{
    // 1. Dilate in source space first
    cv::Mat m=binMask.clone();
    if(dilateRadius>0){
        int ks=dilateRadius*2+1;
        cv::Mat el=cv::getStructuringElement(cv::MORPH_ELLIPSE,{ks,ks});
        cv::dilate(m,m,el);
    }

    // 2. Upscale mask for rendering  (INTER_NEAREST keeps binary)
    int uW=m.cols*upScale, uH=m.rows*upScale;
    cv::Mat mUp;
    cv::resize(m,mUp,{uW,uH},0,0,cv::INTER_NEAREST);

    // 3. Extract contours at hi-res with RETR_CCOMP (hole support)
    // std::vector<std::vector<cv::Point>> ctrs;
    // std::vector<cv::Vec4i> hier;
    // cv::findContours(mUp,ctrs,hier,cv::RETR_CCOMP,cv::CHAIN_APPROX_NONE);

    // RETR_CCOMP gives us outer+hole hierarchy
        std::vector<std::vector<cv::Point>> ctrs;
        std::vector<cv::Vec4i> hier;
        cv::findContours(mUp,ctrs,hier,cv::RETR_CCOMP,cv::CHAIN_APPROX_SIMPLE);

        cv::Mat alphaC=cv::Mat::zeros(mUp.size(),CV_8U);
        int minA=minArea->value();

        // Pass 1: fill outer contours
        for(int i=0;i<int(ctrs.size());i++){
            if(hier[i][3]!=-1) continue;
            if(cv::contourArea(ctrs[i])<minA) continue;
            cv::drawContours(alphaC,ctrs,i,255,cv::FILLED);
        }
        // Pass 2: erase holes (child contours of large outer shapes)
        for(int i=0;i<int(ctrs.size());i++){
            if(hier[i][3]==-1) continue;
            int par=hier[i][3];
            if(cv::contourArea(ctrs[par])<minA) continue;
            cv::drawContours(alphaC,ctrs,i,0,cv::FILLED);
        }

    if(blurSigma>0.05f){
        cv::GaussianBlur(alphaC,alphaC,cv::Size(0,0),blurSigma*(float)upScale);
    }
    if(sharpenAmt>0.01f){
        cv::Mat blur2;cv::GaussianBlur(alphaC,blur2,cv::Size(0,0),(blurSigma+0.5f)*(float)upScale*1.4f);
        alphaC=alphaC+sharpenAmt*(alphaC-blur2);
        cv::threshold(alphaC,alphaC,0,0,cv::THRESH_TOZERO);
        cv::min(alphaC,1.f,alphaC);
    }

    // 6. Rebuild ARGB from processed alpha channel
    cv::Mat alphaU;alphaC.convertTo(alphaU,CV_8U,255.f);
    cv::Mat result(uH,uW,CV_8UC4,cv::Scalar(0,0,0,0));
    for(int y=0;y<uH;y++)
        for(int x=0;x<uW;x++)
            if(alphaU.at<uchar>(y,x))
                result.at<cv::Vec4b>(y,x)=cv::Vec4b(0,0,0,alphaU.at<uchar>(y,x));

    // 7. Downscale to output resolution  (upScale → outScale)
    int outW=m.cols*outScale, outH=m.rows*outScale;
    if(outW==uW&&outH==uH){
        return QImage(result.data,result.cols,result.rows,int(result.step),QImage::Format_RGBA8888).copy();
    }
    cv::Mat final;cv::resize(result,final,{outW,outH},0,0,cv::INTER_AREA);
    return QImage(final.data,final.cols,final.rows,int(final.step),QImage::Format_RGBA8888).copy();
}


    PenParams penParams()const{
        return {penScale->value()*0.1f, penMinRad->value()*0.1f,
                penTaper->value()*0.1f, penBezTol->value()*0.1f};
    }

    // ── PNG overlay (preview: fast 4× internal, display at scan-zone size) ──
    QPixmap pngOverlay(const cv::Mat&mask){
        int alpha=int(255*overlayOpacity->value()/100.0);
        // For the live overlay we render at 4× internal, output at 1× (display)
        QImage img=renderPNG(mask,
            /*upScale=*/2,
            pngDilate->value(),
            pngBlur->value()*0.1f,
            pngSharpen->value()*0.1f,
            /*outScale=*/1,
            pngBezTol->value()*0.1,
            alpha);
        return QPixmap::fromImage(img);
    }

    // ── Vector overlay ───────────────────────────────────────────────────────
    QPixmap vecOverlay(const cv::Mat&mask){
        // Upscale mask 4× for better skeleton quality
        int US=4;
        cv::Mat mUp;cv::resize(mask,mUp,{mask.cols*US,mask.rows*US},0,0,cv::INTER_NEAREST);
        cv::Mat dt;cv::distanceTransform(mUp,dt,cv::DIST_L2,5);
        auto strokes=Skel::extract(mUp,dt,skelMinLen->value()*float(US));

        QImage img(mUp.cols,mUp.rows,QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);

        int alpha=int(255*overlayOpacity->value()/100.0);
        // Scale params to hi-res space
        PenParams pp=penParams();
        pp.widthScale*=1.0f;   // DT already in hi-res pixels
        pp.minRadius*=float(US);
        pp.fitTol*=float(US);

        for(auto&st:strokes){
            QPainterPath path=makePenPath(st.pts,st.rad,pp);
            p.fillPath(path,QColor(0,0,0,alpha));
        }
        p.end();

        // Downscale to source-mask size
        QImage out=img.scaled(mask.cols,mask.rows,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        return QPixmap::fromImage(out);
    }

    // ── PNG save ─────────────────────────────────────────────────────────────
    void savePNG(){
        if(capturedMask.empty())return;
        QImage img=renderPNG(capturedMask,
            /*upScale=*/4,
            pngDilate->value(),
            pngBlur->value()*0.1f,
            pngSharpen->value()*0.1f,
            pngOutScale->value(),
            pngBezTol->value()*0.1,
            255);
        img.save("signature.png","PNG");
    }

    // ── SVG save ─────────────────────────────────────────────────────────────
    void saveSVG(){
        if(capturedMask.empty())return;
        // Work in 4× upscaled space for better skeleton quality
        int US=4;
        cv::Mat mUp;cv::resize(capturedMask,mUp,{capturedMask.cols*US,capturedMask.rows*US},0,0,cv::INTER_NEAREST);
        cv::Mat dt;cv::distanceTransform(mUp,dt,cv::DIST_L2,5);
        auto strokes=Skel::extract(mUp,dt,skelMinLen->value()*float(US));

        // Scale params to hi-res, but emit SVG coords divided by US so output
        // fits original source dimensions
        PenParams pp=penParams();
        pp.minRadius*=float(US);
        pp.fitTol*=float(US);

        // We'll divide all coords by US in the SVG viewBox
        int srcW=capturedMask.cols,srcH=capturedMask.rows;
        float invUS=1.f/float(US);

        QString svg;QTextStream out(&svg);
        out.setRealNumberPrecision(3);out.setRealNumberNotation(QTextStream::FixedNotation);
        out<<"<?xml version='1.0' encoding='UTF-8'?>\n";
        out<<"<svg xmlns='http://www.w3.org/2000/svg' width='"<<srcW<<"' height='"<<srcH
           <<"' viewBox='0 0 "<<srcW<<" "<<srcH<<"'>\n";
        // Scale group: divides hi-res coords back to source space
        out<<"<g transform='scale("<<invUS<<")'>\n";
        out<<"<g fill='black'>\n";
        for(auto&st:strokes){
            QString d=makePenSVGPath(st.pts,st.rad,pp);
            if(!d.isEmpty())out<<"<path d='"<<d<<"'/>\n";
        }
        out<<"</g></g>\n</svg>\n";

        QFile f("signature.svg");
        if(f.open(QIODevice::WriteOnly))f.write(svg.toUtf8());
    }

private slots:
    void processFrame(const QVideoFrame&frame){
        if(!frame.isValid())return;
        QImage img=frame.toImage().convertToFormat(QImage::Format_RGB888);
        cv::Mat full(img.height(),img.width(),CV_8UC3,
                     const_cast<uchar*>(img.bits()),img.bytesPerLine());
        QRect sr=cameraView->computeScanRect(cameraView->size(),img.size());
        cv::Rect cvSR(sr.x(),sr.y(),sr.width(),sr.height());
        if(cvSR.x<0||cvSR.y<0||cvSR.x+cvSR.width>full.cols||cvSR.y+cvSR.height>full.rows)return;
        cv::Mat roi=full(cvSR).clone();
        lastROI=roi.clone();
        cv::Mat mask=detect(roi);
        if(chkAuto->isChecked()){
            if(cv::countNonZero(mask)/double(mask.rows*mask.cols)>0.012)
                capturedMask=mask.clone();
        }else{
            capturedMask=mask.clone();
        }
        QPixmap overlay;
        if(!capturedMask.empty())
            overlay=overlayMode->currentIndex()==1?vecOverlay(capturedMask):pngOverlay(capturedMask);
        QPixmap dbg;
        if(!mask.empty()){
            cv::Mat d3;cv::cvtColor(mask,d3,cv::COLOR_GRAY2RGB);
            dbg=QPixmap::fromImage(QImage(d3.data,d3.cols,d3.rows,int(d3.step),QImage::Format_RGB888).copy());
        }
        QPixmap camPx=QPixmap::fromImage(
            QImage(img.bits(),img.width(),img.height(),img.bytesPerLine(),QImage::Format_RGB888).copy());
        cameraView->setFrame(camPx,overlay,sr,dbg);
    }
};

#include "signature_app.moc"

int main(int argc,char*argv[]){
    QApplication app(argc,argv);
    MainWindow w;w.resize(1200,820);w.show();
    return app.exec();
}