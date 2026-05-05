declare class AutoScanHandler extends NSObject {
    static alloc(): AutoScanHandler; // inherited from NSObject

    static getHashWithPoints(points: NSArray<NSValue> | NSValue[]): number;

    static getPointHash(point: CGPoint): number;

    static new(): AutoScanHandler; // inherited from NSObject

    autoScanDuration: number;

    cropView: NSCropView;

    distanceThreshold: number;

    enabled: boolean;

    onAutoScan: OnAutoScan;

    preAutoScanDelay: number;

    constructor(o: { cropView: NSCropView; onAutoScan: OnAutoScan });

    clearAll(): void;

    initWithCropViewOnAutoScan(cropView: NSCropView, onAutoScan: OnAutoScan): this;

    processWithPoints(points: NSArray<NSArray<NSValue>> | NSArray<NSValue>[]): void;

    replaceHashWithOldValueNewValue(oldValue: number, newValue: number): void;

    startAutoScanJobWithPoints(points: NSArray<NSValue> | NSValue[]): void;

    startAutoScanPreJobWithPoints(points: NSArray<NSValue> | NSValue[]): void;
}
declare class NSCropView extends UIView {
    static alloc(): NSCropView; // inherited from NSObject

    static appearance(): NSCropView; // inherited from UIAppearance

    static appearanceForTraitCollection(trait: UITraitCollection): NSCropView; // inherited from UIAppearance

    static appearanceForTraitCollectionWhenContainedIn(trait: UITraitCollection, ContainerClass: typeof NSObject): NSCropView; // inherited from UIAppearance

    static appearanceForTraitCollectionWhenContainedInInstancesOfClasses(trait: UITraitCollection, containerTypes: NSArray<typeof NSObject> | (typeof NSObject)[]): NSCropView; // inherited from UIAppearance

    static appearanceWhenContainedIn(ContainerClass: typeof NSObject): NSCropView; // inherited from UIAppearance

    static appearanceWhenContainedInInstancesOfClasses(containerTypes: NSArray<typeof NSObject> | (typeof NSObject)[]): NSCropView; // inherited from UIAppearance

    static new(): NSCropView; // inherited from NSObject

    animationDuration: number;

    colors: NSArray<UIColor>;

    drawFill: boolean;

    fillAlpha: number;

    imageSize: CGSize;

    quads: NSArray<NSArray<NSValue>>;

    strokeWidth: number;

    videoGravity: string;

    interpolatePointWithPoint1Point2Value(point1: NSValue, point2: NSValue, value: number): CGPoint;

    replaceProgressHashWithOldHashNewHash(oldHash: number, newHash: number): void;

    setQuadsAnimatedWithPoints(points: NSArray<NSArray<NSValue>> | NSArray<NSValue>[]): void;

    updateProgressWithHashProgress(hash: number, progress: number): void;
}

interface OnAutoScan {
    onAutoScan(corners: string): void;
}
declare var OnAutoScan: {
    prototype: OnAutoScan;
};
interface OnQRCode {
    onQRCodes(data: string): void;
}
declare var OnQRCode: {
    prototype: OnQRCode;
};

interface CompletionDelegate {
    onCompleteError(result: NSObject, error: NSError): void;

    onProgress(progress: number): void;
}
declare var CompletionDelegate: {
    prototype: CompletionDelegate;
};
declare class PDFUtils extends NSObject {
    static alloc(): PDFUtils; // inherited from NSObject

    static importPdfToTempImagesDelegateOptions(pdfPath: string, delegate: CompletionDelegate, options: string): void;

    static new(): PDFUtils; // inherited from NSObject
}
