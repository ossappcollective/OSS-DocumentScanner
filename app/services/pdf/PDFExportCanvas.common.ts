import { lc } from '@nativescript-community/l';
import { Canvas, ColorMatrixColorFilter, LayoutAlignment, Paint, StaticLayout } from '@nativescript-community/ui-canvas';
import { Color, File, ImageSource } from '@nativescript/core';
import { getActualLanguage } from '@shared/helpers/lang';
import type { OCRDocument, OCRPage } from '~/models/OCRDocument';
import { CARD_RATIO } from '~/utils/constants';
import { loadImage, recycleImages } from '~/utils/images';
import { getPageColorMatrix } from '~/utils/matrix';
import { pkpassToImage } from '~/utils/pkpass';
import PDFCanvas from './PDFCanvas';

const textPaint = new Paint();
textPaint.setTextSize(40);
textPaint.setFontWeight('bold');
function calculateMaxTextSize({
    maxFontSize,
    maxHeight,
    maxWidth,
    minFontSize,
    paint,
    startFontSize,
    step = 2,
    text
}: {
    text;
    paint: Paint;
    startFontSize;
    minFontSize;
    maxFontSize;
    step?;
    maxWidth: number;
    maxHeight: number;
}) {
    if (text == null || paint == null) return 0;
    maxWidth = maxWidth;
    maxHeight = maxHeight;
    let size = Math.max(startFontSize, minFontSize);
    let oldSize = size;
    paint.setTextSize(size);
    let staticLayout = new StaticLayout(text, paint, maxWidth);
    if (staticLayout.getWidth() > maxWidth || staticLayout.getHeight() > maxHeight) {
        while ((staticLayout.getWidth() > maxWidth || staticLayout.getHeight() > maxHeight || size > maxFontSize) && size >= minFontSize) {
            size -= step;
            paint.setTextSize(size);
            staticLayout = new StaticLayout(text, paint, maxWidth);
        }
        return size;
    } else {
        while (size < maxFontSize && (staticLayout.getWidth() < maxWidth || staticLayout.getHeight() < maxHeight)) {
            oldSize = size;
            size += step;
            paint.setTextSize(size);
            staticLayout = new StaticLayout(text, paint, maxWidth);
        }
        return oldSize;
    }
}

export async function getTransformedImage({
    defaultBackgroundColor,
    document,
    loadOptions = {},
    options = {},
    page
}: {
    page: OCRPage;
    options?: { width?; height?; scale?; colorMatrix?; rotation?; brightness?; contrast?; pkpassLayout? };
    loadOptions?;
    document?: OCRDocument;
    defaultBackgroundColor?: string;
}) {
    try {
        let imagePath = page.imagePath;
        let canvasBackgroundColor;

        // DEV_LOG && console.log('getTransformedImage', JSON.stringify(page), JSON.stringify(options), JSON.stringify(loadOptions));
        if (imagePath) {
            const imageSource = await loadImage(imagePath, { sourceWidth: page.width, sourceHeight: page.height, ...loadOptions });
            if (isNaN(imageSource.width)) {
                throw new Error(lc('error_loading_image', page.imagePath, File.exists(page.imagePath)));
            }
            const colorMatrix = options?.colorMatrix || getPageColorMatrix(page, undefined, options?.brightness, options?.contrast);
            const rotation = options?.rotation || page.rotation;
            if (colorMatrix || rotation || options.width || options.height) {
                const scale = options.scale || page.scale;
                const width = options.width || imageSource.width * scale;
                const height = options.height || imageSource.height * scale;
                let rotateWidth = width;
                let rotateHeight = height;
                if (rotation % 180 !== 0) {
                    const oldWidth = rotateWidth;
                    rotateWidth = rotateHeight;
                    rotateHeight = oldWidth;
                }
                const pageCanvas = new Canvas(rotateWidth, rotateHeight);
                const bitmapPaint: Paint = new Paint();
                if (__ANDROID__) {
                    pageCanvas.setDensity(imageSource.android.getDensity());
                }
                if (colorMatrix) {
                    bitmapPaint.setColorFilter(new ColorMatrixColorFilter(colorMatrix));
                }
                if (canvasBackgroundColor) {
                    pageCanvas.drawColor(canvasBackgroundColor);
                }
                pageCanvas.translate(rotateWidth / 2, rotateHeight / 2);
                pageCanvas.rotate(page.rotation, 0, 0);
                pageCanvas.scale(scale, scale, 0, 0);
                pageCanvas.drawBitmap(imageSource, -imageSource.width / 2, -imageSource.height / 2, bitmapPaint);
                recycleImages(imageSource);
                return new ImageSource(pageCanvas.getImage());
            }
            return imageSource;
        } else if (CARD_APP && page.pkpass) {
            const logoImage = page.pkpass?.images?.logo2x || page.pkpass?.images?.logo; // Max 160x50 points
            if (options?.pkpassLayout === 'logo' && logoImage && File.exists(logoImage)) {
                imagePath = logoImage;
                canvasBackgroundColor = page.pkpass.passData.backgroundColor;
            } else {
                return pkpassToImage(page.pkpass, {
                    lang: getActualLanguage(),
                    width: options?.width,
                    layout: options?.pkpassLayout === 'card' ? 'card' : 'full',
                    includeBackFields: false
                });
            }
        } else if (CARD_APP && document) {
            const backgroundColor = new Color(page.colors?.[0] || document.extra?.color || defaultBackgroundColor);
            const padding = 16 * 4;
            const width = 800;
            const height = width * CARD_RATIO;
            const canvas = new Canvas(width, height);
            let textColor = 'black';
            if (document.extra?.color) {
                canvas.drawColor(backgroundColor);
                textColor = backgroundColor.getBrightness() < 145 ? 'white' : 'black';
            }
            textPaint.color = textColor;
            const fontSize = calculateMaxTextSize({
                text: document.name,
                paint: textPaint,
                maxWidth: width - 2 * padding,
                maxHeight: height - 2 * padding,
                minFontSize: 40,
                maxFontSize: 200,
                startFontSize: 150
            });
            textPaint.setTextSize(fontSize);
            const staticLayout = new StaticLayout(document.name, textPaint, width - 2 * padding, LayoutAlignment.ALIGN_CENTER, 1, 0, true);
            canvas.translate(padding, height / 2 - staticLayout.getHeight() / 2);
            staticLayout.draw(canvas);
            return new ImageSource(canvas.getImage());
        }
    } catch (error) {
        DEV_LOG && console.error('getTransformedImage', error, error.stack);
    }
}

export default class PDFExportCanvasBase extends PDFCanvas {}
