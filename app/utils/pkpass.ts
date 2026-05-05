import { lc } from '@nativescript-community/l';
import { createNativeAttributedString } from '@nativescript-community/text';
import { Align, Canvas, LayoutAlignment, Paint, Rect, StaticLayout } from '@nativescript-community/ui-canvas';
import { SVG } from '@nativescript-community/ui-svg/canvas';
import { Color, File, Folder, ImageSource, PercentLength, Screen, Utils, knownFolders, path } from '@nativescript/core';
import { generateQRCodeImage, getSVGFromQRCode, getSVGFromQRCodeSync } from 'plugin-nativeprocessor';
import { unzip, zip } from 'plugin-zip';
import type { OCRDocument } from '~/models/OCRDocument';
import { PKBarcodeFormat, PKPass, type PKPassBarcode, type PKPassData, type PKPassField, type PKPassImages, type PKPassStructure, PKPassStyle, PKPassTransitType, PKPassType } from '~/models/PKPass';
import { CARD_RATIO } from './constants';
import { loadImage, recycleImages } from './images';

const screenWidthDips = Screen.mainScreen.widthDIPs;

const PASS_JSON_FILE = 'pass.json';
const IMAGE_FILES = [
    'icon.png',
    'icon@2x.png',
    'icon@3x.png',
    'logo.png',
    'logo@2x.png',
    'logo@3x.png',
    'background.png',
    'background@2x.png',
    'background@3x.png',
    'footer.png',
    'footer@2x.png',
    'footer@3x.png',
    'strip.png',
    'strip@2x.png',
    'strip@3x.png',
    'thumbnail.png',
    'thumbnail@2x.png',
    'thumbnail@3x.png'
];

export interface PKPassParseResult {
    pass: PKPass;
    extractedPath: string;
}

/**
 * Map ESpass barcode format string to PKBarcodeFormat enum
 */
function mapEspassBarcodeFormat(format: string): PKBarcodeFormat {
    switch ((format || '').toUpperCase()) {
        case 'AZTEC':
            return PKBarcodeFormat.Aztec;
        case 'QR_CODE':
        case 'QR':
            return PKBarcodeFormat.QR;
        case 'PDF_417':
        case 'PDF417':
            return PKBarcodeFormat.PDF417;
        case 'CODE128':
        case 'CODE_128':
            return PKBarcodeFormat.Code128;
        default:
            return PKBarcodeFormat.QR;
    }
}

/**
 * Convert an ESpass JSON object into the standard PKPassData structure.
 * This allows all existing rendering code (PKPassView, PKPassCardCell, canvas renderer)
 * to work without any changes.
 *
 * ESpass spec: https://espass.it/how/
 */
export function convertEspassToPKPassData(espassJson: any): PKPassData {
    // Determine pass style from ESpass type
    let style: PKPassStyle;
    switch ((espassJson.type || '').toUpperCase()) {
        case 'BOARDING':
            style = PKPassStyle.BoardingPass;
            break;
        case 'COUPON':
        case 'VOUCHER':
            style = PKPassStyle.Coupon;
            break;
        case 'EVENT':
            style = PKPassStyle.EventTicket;
            break;
        case 'LOYALTY':
        case 'MEMBERSHIP':
        case 'STORECARD':
            style = PKPassStyle.StoreCard;
            break;
        default:
            style = PKPassStyle.Generic;
    }

    // Separate visible and hidden fields
    const allFields: any[] = espassJson.fields || [];
    const headerFields = allFields.filter((f) => !!f.header);
    const visibleFields = allFields.filter((f) => !f.hide && !f.header);
    const hiddenFields = allFields.filter((f) => f.hide);

    function toField(f: any, index: number): PKPassField {
        return { key: `field_${index}`, label: f.label, value: f.value };
    }

    // Distribute visible fields across sections similar to pkpass conventions
    // Primary (big display): up to 2 fields
    // Secondary: up to 4 fields
    // Auxiliary: up to 4 fields
    // Back: hidden fields + overflow
    const primaryFields: PKPassField[] = visibleFields.slice(0, 2).map((f, i) => toField(f, i));
    const secondaryFields: PKPassField[] = visibleFields.slice(2, 6).map((f, i) => toField(f, i + 2));
    const auxiliaryFields: PKPassField[] = visibleFields.slice(6, 10).map((f, i) => toField(f, i + 6));
    const backFields: PKPassField[] = [...visibleFields.slice(10).map((f, i) => toField(f, i + 10)), ...hiddenFields.map((f, i) => toField(f, i + visibleFields.length))];

    // Transit type for boarding passes (ESpass doesn't specify vehicle type, default to Generic)
    const transitType = espassJson.transitType ?? (style === PKPassStyle.BoardingPass ? PKPassTransitType.Generic : undefined);

    const structure: PKPassStructure = {
        headerFields: headerFields.length ? headerFields : undefined,
        primaryFields: primaryFields.length ? primaryFields : undefined,
        secondaryFields: secondaryFields.length ? secondaryFields : undefined,
        auxiliaryFields: auxiliaryFields.length ? auxiliaryFields : undefined,
        backFields: backFields.length ? backFields : undefined,
        transitType
    };

    // Convert barcode
    let barcode: PKPassBarcode | undefined;
    const bc = espassJson.barCode;
    if (bc && bc.message) {
        barcode = {
            format: mapEspassBarcodeFormat(bc.format),
            message: bc.message,
            messageEncoding: 'iso-8859-1',
            altText: bc.alternative_text
        };
    }

    // Convert locations
    const locations = (espassJson.locations || []).map((loc: any) => ({
        latitude: parseFloat(loc.lat) || 0,
        longitude: parseFloat(loc.lon) || 0,
        relevantText: loc.name
    }));

    // Expiration from validTimespans
    let expirationDate: string | undefined;
    if (espassJson.validTimespans && espassJson.validTimespans.length > 0) {
        expirationDate = espassJson.validTimespans[0].to;
    }

    let backgroundColor = espassJson.accentColor;
    if (backgroundColor && /^#([A-Fa-f0-9]{8})$/.test(backgroundColor)) {
        // color hex is #AARRGGBB instead of #RRGGBBAA
        const color = new Color(backgroundColor);
        backgroundColor = backgroundColor = new Color(color.r, color.g, color.b, color.a).hex;
    }

    const pkpassData: PKPassData = {
        formatVersion: 1,
        passTypeIdentifier: `espass.${espassJson.id || 'unknown'}`,
        serialNumber: espassJson.id || Date.now().toString(),
        teamIdentifier: espassJson.creator || 'espass',
        organizationName: espassJson.description || '',
        description: espassJson.description || '',
        backgroundColor,
        labelColor: espassJson.labelColor,
        barcode,
        locations: locations.length ? locations : undefined,
        expirationDate
    };

    // Attach structure under the matching key
    switch (style) {
        case PKPassStyle.BoardingPass:
            pkpassData.boardingPass = structure;
            break;
        case PKPassStyle.Coupon:
            pkpassData.coupon = structure;
            break;
        case PKPassStyle.EventTicket:
            pkpassData.eventTicket = structure;
            break;
        case PKPassStyle.StoreCard:
            pkpassData.storeCard = structure;
            break;
        default:
            pkpassData.generic = structure;
    }

    return pkpassData;
}

const fieldsPaint = new Paint();
fieldsPaint.fontWeight = '500';
const testPaint = new Paint();
testPaint.color = 'red';

/**
 * Load localized strings from .lproj folders in PKPass
 * @param extractPath Path to extracted PKPass contents
 * @returns Object with language codes as keys and localized strings as values
 */
function loadPKPassLocalizations(extractPath: string): { [languageCode: string]: { [key: string]: string } } {
    const localizations: { [languageCode: string]: { [key: string]: string } } = {};

    try {
        const extractFolder = Folder.fromPath(extractPath);
        const entities = extractFolder.getEntitiesSync();
        for (const entity of entities) {
            const entityName = entity.name;
            // Check if it's a .lproj folder
            if (entityName.endsWith('.lproj') && entity instanceof Folder) {
                const languageCode = entityName.replace('.lproj', '');
                const stringsFilePath = path.join(entity.path, 'pass.strings');

                if (File.exists(stringsFilePath)) {
                    try {
                        const stringsFile = File.fromPath(stringsFilePath);
                        const stringsContent = stringsFile.readTextSync();

                        // Parse .strings file (format: "key" = "value";)
                        const strings: { [key: string]: string } = {};
                        const regex = /"([^"]+)"\s*=\s*"([^"]*)"\s*;/g;
                        let match;

                        while ((match = regex.exec(stringsContent)) !== null) {
                            strings[match[1]] = match[2];
                        }

                        if (Object.keys(strings).length > 0) {
                            localizations[languageCode] = strings;
                        }
                    } catch (error) {
                        console.error(`Error parsing pass.strings for ${languageCode}:`, error);
                    }
                }
            }
        }
    } catch (error) {
        console.error('Error loading PKPass localizations:', error);
    }

    return localizations;
}

/**
 * Parse a .pkpass or .espass file and extract its contents.
 * ESpass files use the same ZIP container format as PKPass but have a different JSON schema.
 * This function auto-detects the format and normalises ESpass data into PKPassData so that
 * the existing rendering pipeline (PKPassView, canvas renderer) can be reused without changes.
 * @param pkpassFilePath Path to the .pkpass or .espass file
 * @param targetFolder Folder where to extract the pass data
 * @returns PKPassParseResult with the parsed pass and extraction path
 */
export async function extractAndParsePKPassFile(pkpassFilePath: string, targetFolder: Folder): Promise<PKPassParseResult> {
    if (!File.exists(pkpassFilePath)) {
        throw new Error(`Pass file not found: ${pkpassFilePath}`);
    }

    // Create a unique folder for this pass
    const passId = Date.now().toString();
    const extractPath = path.join(targetFolder.path, passId);
    const extractFolder = Folder.fromPath(extractPath);

    try {
        // Both .pkpass and .espass are ZIP archives
        await unzip({
            archive: pkpassFilePath,
            directory: extractPath
        });

        // Try to locate the JSON descriptor:
        // 1. Standard PKPass: pass.json at the root
        // 2. ESpass: may also be pass.json, or any .json file at the root
        const passJsonPath = path.join(extractPath, PASS_JSON_FILE);
        let rawJsonContent: string;
        let passType: PKPassType = PKPassType.PKPass;

        if (File.exists(passJsonPath)) {
            rawJsonContent = await File.fromPath(passJsonPath).readText();
        } else {
            // Look for any .json file in the root of the extracted archive
            const passRootFolder = Folder.fromPath(extractPath);
            const entities = passRootFolder.getEntitiesSync();
            const jsonFile = entities.find((e) => !(e instanceof Folder) && e.name.endsWith('.json'));
            if (!jsonFile) {
                throw new Error('No JSON descriptor found in pass file');
            }
            rawJsonContent = await File.fromPath(jsonFile.path).readText();
        }
        const rawJson = JSON.parse(rawJsonContent);

        // Detect format:
        // ESpass: lacks PKPass's `formatVersion`, has a flat `fields` array AND an `id` or `type` field
        // PKPass: has `formatVersion` (required by spec)
        let passData: PKPassData;
        if (!rawJson.formatVersion && Array.isArray(rawJson.fields) && (rawJson.id || rawJson.type)) {
            // ESpass format — convert to unified PKPassData
            passType = PKPassType.ESpass;
            passData = convertEspassToPKPassData(rawJson);
        } else {
            // Standard PKPass format
            passType = PKPassType.PKPass;
            passData = rawJson as PKPassData;

            // Load localizations from .lproj folders (pkpass only)
            const allLocalizations = loadPKPassLocalizations(extractPath);
            if (Object.keys(allLocalizations).length > 0) {
                passData._allLocalizations = allLocalizations;
            }
        }

        // Extract image paths (same names for both formats)
        const images: PKPassImages = {};
        for (const imageFile of IMAGE_FILES) {
            const imagePath = path.join(extractPath, imageFile);
            if (File.exists(imagePath)) {
                const key = imageFile.replace('.png', '').replace('@2x', '2x').replace('@3x', '3x');
                images[key] = imagePath;
            }
        }

        // Create PKPass object (document_id will be set by caller)
        const pass = new PKPass(passId, '', passType);
        pass.passData = passData;
        pass.images = images;

        return {
            pass,
            extractedPath: extractPath
        };
    } catch (error) {
        // Clean up on error
        try {
            if (Folder.exists(extractPath)) {
                await extractFolder.remove();
            }
        } catch (cleanupError) {
            console.error('Error cleaning up after pass parse failure:', cleanupError);
        }
        throw error;
    }
}

/**
 * Get the display name for a PKPass
 * @param pass PKPass object
 * @returns Display name
 */
export function getPKPassDisplayName(pass: PKPass): string {
    const { passData } = pass;

    // Try to get a meaningful name from the pass
    if (passData.logoText) {
        return passData.logoText;
    }

    if (passData.organizationName) {
        return passData.organizationName;
    }

    return passData.description || 'Pass';
}

/**
 * Get the primary image for a PKPass (for display in lists)
 * @param pass PKPass object
 * @returns Path to primary image
 */
export function getPKPassPrimaryImage(pass: PKPass): string | undefined {
    const { images } = pass;

    // Priority order for primary image
    const imagePriority = [
        images.strip2x,
        images.strip,
        images.strip3x,
        images.background2x,
        images.background,
        images.background3x,
        images.logo2x,
        images.logo,
        images.logo3x,
        images.icon2x,
        images.icon,
        images.icon3x
    ];

    for (const imagePath of imagePriority) {
        if (imagePath && File.exists(imagePath)) {
            return imagePath;
        }
    }

    return undefined;
}

/**
 * Get the icon image for a PKPass
 * @param pass PKPass object
 * @returns Path to icon image
 */
export function getPKPassIconImage(pass: PKPass): string | undefined {
    const { images } = pass;

    if (images.icon2x && File.exists(images.icon2x)) {
        return images.icon2x;
    }
    if (images.icon && File.exists(images.icon)) {
        return images.icon;
    }
    if (images.icon3x && File.exists(images.icon3x)) {
        return images.icon3x;
    }

    return undefined;
}

/**
 * Validate if a file is a valid .pkpass file
 * @param filePath Path to file
 * @returns true if file appears to be a valid .pkpass file
 */
export function isPKPassFile(filePath: string): boolean {
    if (!File.exists(filePath)) {
        return false;
    }

    // Check file extension — both .pkpass and .espass are supported
    const lower = filePath.toLowerCase();
    return lower.endsWith('.pkpass') || lower.endsWith('.espass');
}

// ─── ESpass barcode format string ────────────────────────────────────────────

function pkBarcodeFormatToEspass(format: PKBarcodeFormat): string {
    switch (format) {
        case PKBarcodeFormat.Aztec:
            return 'AZTEC';
        case PKBarcodeFormat.PDF417:
            return 'PDF417';
        case PKBarcodeFormat.Code128:
            return 'CODE128';
        case PKBarcodeFormat.QR:
        default:
            return 'QR';
    }
}

// ─── PKPassStyle → ESpass type ───────────────────────────────────────────────

function pkPassStyleToEspassType(style: PKPassStyle): string {
    switch (style) {
        case PKPassStyle.BoardingPass:
            return 'BOARDING';
        case PKPassStyle.Coupon:
            return 'COUPON';
        case PKPassStyle.EventTicket:
            return 'EVENT';
        case PKPassStyle.StoreCard:
            return 'LOYALTY';
        default:
            return 'GENERIC';
    }
}

/**
 * Convert the internal (PKPass-normalised) pass data back to the ESpass JSON schema.
 * Used when the user exports/shares a pass "as ESpass".
 */
export function convertPKPassDataToEspassJson(pass: PKPass, lang: string): any {
    const passData = pass.passData;
    const structure = pass.getPassStructure();
    const style = pass.getPassStyle();

    // Flatten all visible fields (primary → secondary → auxiliary → back as hidden)
    const fields: any[] = [];
    const addFields = (section: PKPassField[] | undefined, hide: boolean, header: boolean = false) => {
        (section || []).forEach((f) => {
            fields.push({ label: pass.getLocalizedFieldLabel(f, lang), value: pass.formatFieldValue(f, lang), hide, ...(header ? { header: true } : {}) });
        });
    };
    addFields(structure?.headerFields, false, true);
    addFields(structure?.primaryFields, false);
    addFields(structure?.secondaryFields, false);
    addFields(structure?.auxiliaryFields, false);
    addFields(structure?.backFields, true);

    // Barcode
    const primaryBarcode = pass.getPrimaryBarcode();
    let barCode: any;
    if (primaryBarcode) {
        barCode = {
            format: pkBarcodeFormatToEspass(primaryBarcode.format),
            message: primaryBarcode.message,
            alternative_text: primaryBarcode.altText
        };
    }

    // Locations
    const locations = (passData.locations || []).map((loc) => ({
        lat: String(loc.latitude),
        lon: String(loc.longitude),
        name: loc.relevantText
    }));

    // Valid timespans from expirationDate; use the pass creation date as a stable `from` value.
    // createdDate is always set in the PKPass constructor, so the fallback is only a safety net.
    const validTimespans = passData.expirationDate ? [{ from: new Date(pass.createdDate).toISOString(), to: passData.expirationDate }] : undefined;

    return {
        id: passData.serialNumber || '',
        type: pkPassStyleToEspassType(style),
        description: passData.description || passData.organizationName || '',
        accentColor: passData.backgroundColor,
        labelColor: passData.labelColor,
        creator: passData.teamIdentifier || '',
        app: 'cardwallet',
        fields,
        ...(barCode ? { barCode } : {}),
        ...(locations.length ? { locations } : {}),
        ...(validTimespans ? { validTimespans } : {}),
        ...(style === PKPassStyle.BoardingPass ? { transitType: structure.transitType } : {})
    };
}

// ─── Derive the format a pass was originally stored in ───────────────────────

export function getStoredPassFormat(pkpass: PKPass | undefined): PKPassType {
    return pkpass?.passType === PKPassType.ESpass ? PKPassType.ESpass : PKPassType.PKPass;
}

// ─── Set of image filenames we expect inside a pass folder ───────────────────

const IMAGE_FILE_SET = new Set(IMAGE_FILES.map((f) => f.toLowerCase()));

/**
 * Build a pass archive (ZIP) in the requested target format.
 *
 * - The `pkpassFolderPath` contains the files that were originally stored when the
 *   pass was first imported (images + original JSON).
 * - The `pass` object holds the parsed (normalised) pass data.
 * - `targetFormat` controls whether the output ZIP should be a .pkpass or .espass.
 *
 * The function writes the converted JSON descriptor plus any image files from the
 * stored folder into a temporary directory, then zips everything to `outputPath`.
 */
export async function buildPassArchive(pkpassFolderPath: string, pass: PKPass, targetFormat: PKPassType, outputPath: string, lang: string): Promise<void> {
    // Create a unique scratch folder inside the temp directory
    const scratchPath = path.join(knownFolders.temp().path, `_scratch_${Date.now()}`);
    const scratchFolder = Folder.fromPath(scratchPath);

    try {
        // Write the JSON descriptor for the target format
        if (targetFormat === PKPassType.ESpass) {
            const espassJson = convertPKPassDataToEspassJson(pass, lang);
            await File.fromPath(path.join(scratchPath, PASS_JSON_FILE)).writeText(JSON.stringify(espassJson), 'UTF-8');
        } else {
            // PKPass format: write the normalised PKPassData as pass.json (without private _allLocalizations field)
            const { _allLocalizations, ...pkpassJson } = pass.passData as any;
            await File.fromPath(path.join(scratchPath, PASS_JSON_FILE)).writeText(JSON.stringify(pkpassJson), 'UTF-8');
        }

        // Copy image files from the stored pkpass folder into the scratch folder.
        // Only copy files whose names are listed in IMAGE_FILES to avoid accidentally
        // copying the original JSON descriptor (which we have replaced above).
        if (Folder.exists(pkpassFolderPath)) {
            const sourceFolder = Folder.fromPath(pkpassFolderPath);
            const entities = sourceFolder.getEntitiesSync();
            for (const entity of entities) {
                const isFile = !(entity instanceof Folder);
                if (isFile && IMAGE_FILE_SET.has(entity.name.toLowerCase())) {
                    await File.fromPath(entity.path).copy(path.join(scratchPath, entity.name));
                }
            }
        }

        // Zip the scratch folder into the output path
        await zip({ directory: scratchPath, archive: outputPath, keepParent: false });
    } finally {
        // Clean up scratch folder
        try {
            if (Folder.exists(scratchPath)) {
                await scratchFolder.remove();
            }
        } catch (_) {}
    }
}

export function getFieldTextAlignment(field: PKPassField, defaultValue = 'left'): 'left' | 'right' | 'center' | 'initial' {
    if (!field) {
        return;
    }
    if (field.textAlignment) {
        return field.textAlignment.slice(15).toLowerCase() as any;
    }
    return defaultValue as any;
}

export function getBarcodeFormat(barcode: PKPassBarcode) {
    let barcodeFormat = 'QRCode';
    switch (barcode.format) {
        case PKBarcodeFormat.QR:
            barcodeFormat = 'QRCode';
            break;
        case PKBarcodeFormat.PDF417:
            barcodeFormat = 'PDF417';
            break;
        case PKBarcodeFormat.Aztec:
            barcodeFormat = 'Aztec';
            break;
        case PKBarcodeFormat.Code128:
            barcodeFormat = 'Code128';
            break;
    }
    return barcodeFormat;
}

export async function getBarcodeSVG({ barcode, foregroundColor, width = 300 }: { barcode: PKPassBarcode; foregroundColor; width?: number }) {
    if (!barcode) return undefined;
    const barcodeFormat = getBarcodeFormat(barcode);
    try {
        const color = new Color(foregroundColor);
        return await getSVGFromQRCode(barcode.message, barcodeFormat, width, {
            color: color instanceof Color ? color.hex : color
        });
    } catch (error) {
        console.error('Error generating barcode image:', error);
        return undefined;
    }
}
export function getBarcodeSVGSync({ barcode, foregroundColor, width = 300 }: { barcode: PKPassBarcode; foregroundColor; width?: number }) {
    if (!barcode) return undefined;
    const barcodeFormat = getBarcodeFormat(barcode);
    try {
        const color = new Color(foregroundColor);
        return getSVGFromQRCodeSync(barcode.message, barcodeFormat, width, {
            color: color instanceof Color ? color.hex : color
        });
    } catch (error) {
        console.error('Error generating barcode image:', error);
        return undefined;
    }
}
export async function getBarcodeImage({ barcode, foregroundColor, height, width = 300 }: { barcode: PKPassBarcode; foregroundColor; width?: number; height?: number }) {
    if (!barcode) return undefined;
    try {
        // Convert PKPass barcode format to QRCodeData format
        const qrcodeData = {
            text: barcode.message,
            format: getBarcodeFormat(barcode),
            position: null
        };
        const color = new Color(foregroundColor);
        return generateQRCodeImage(barcode.message, getBarcodeFormat(barcode), width, height, {
            color: color instanceof Color ? color.hex : color
        });
    } catch (error) {
        console.error('Error generating barcode image:', error);
        return undefined;
    }
}

// Keep for backward compatibility - now checks if document has any pages with PKPass
export function documentHasPKPassData(document: OCRDocument): boolean {
    return document.pages?.some((page) => !!page.pkpass) || false;
}

/**
 * Get Material Design Icon for transit type
 * Used to display an icon between primary fields on boarding passes
 */
export function getTransitIcon(transitType?: PKPassTransitType): string | undefined {
    if (!transitType) return undefined;

    switch (transitType) {
        case PKPassTransitType.Air:
            return 'mdi-airplane';
        case PKPassTransitType.Boat:
            return 'mdi-ferry';
        case PKPassTransitType.Bus:
            return 'mdi-bus';
        case PKPassTransitType.Train:
            return 'mdi-train';
        case PKPassTransitType.Generic:
            return 'mdi-transit-connection-variant';
        default:
            return undefined;
    }
}

export async function pkpassToImage(
    pkpass: PKPass,
    options: {
        lang: string;
        layout?: 'card' | 'full'; // Card for compact credit-card sized, full for complete details
        includeBackFields?: boolean;
        backgroundColor?: string;
        width?: number;
        height?: number;
        // rendering scale (dpi multiplier). 1 = native points, 2 = double resolution, etc.
        scale?: number;
    }
) {
    // const component = (options?.layout === 'full' ? await import('~/components/pkpass/PKPassView.svelte') : await import('~/components/pkpass/PKPassCardCell.svelte')).default;
    // const primaryBarcode = pkpass.getPrimaryBarcode();
    // const foregroundColor = pkpass.passData.foregroundColor || get(colors).colorOnBackground;
    // let barcodeSvg;
    // if (primaryBarcode && foregroundColor) {
    //     barcodeSvg = await getBarcodeSVG({ barcode: primaryBarcode, foregroundColor });
    // }
    // const itemWidth = screenWidthDips;
    // const scale = (options.scale ?? options?.width) ? options?.width / itemWidth : 1;
    // const componentInstanceInfo = resolveComponentElement(component, {
    //     pkpass,
    //     itemWidth,
    //     document: null,
    //     forImageRendering: true,
    //     barcodeSvg,
    //     ...(options?.layout === 'full'
    //         ? {
    //               width: itemWidth,
    //               verticalAlignment: 'top',
    //               includeBackFields: false,
    //               iosOverflowSafeArea: false,
    //               iosIgnoreSafeArea: false
    //           }
    //         : {
    //               width: itemWidth,
    //               height: itemWidth * CARD_RATIO
    //           })
    // }) as ComponentInstanceInfo<GridLayout, any>;
    // const view = componentInstanceInfo.element.nativeElement;
    // const gesturerootview = new GestureRootView();
    // // gesturerootview.parent = Application.getRootView()
    // const rootView = Application.getRootView();
    // gesturerootview.addChild(view);
    // if (__ANDROID__) {
    //     gesturerootview.visibility = 'collapsed';
    // }
    // (rootView as GridLayout).addChild(gesturerootview);
    // const imageSource = await new Promise<ImageSource>((resolve) => {
    //     const onLayoutChanged = (event) => {
    //         const nView = view.nativeView;
    //         if (__ANDROID__) {
    //             // create a higher-resolution bitmap and scale the canvas so view is rendered at higher DPI
    //             const w = Math.max(1, Math.round(nView.getWidth() * scale));
    //             const h = Math.max(1, Math.round(nView.getHeight() * scale));
    //             const bmp = android.graphics.Bitmap.createBitmap(w, h, android.graphics.Bitmap.Config.ARGB_8888);
    //             // Set density so Android knows this is a high-DPI bitmap (improves image scaling/filtering)
    //             const metrics = Utils.android.getApplicationContext().getResources().getDisplayMetrics();
    //             // bmp.setDensity(metrics.densityDpi * scale);
    //             const canvas = new android.graphics.Canvas(bmp);
    //             canvas.setDensity(metrics.densityDpi);
    //             // scale so that drawing commands map correctly (view draws in logical pixels)
    //             canvas.scale(scale, scale);
    //             // nView.setLayerType(android.view.View.LAYER_TYPE_SOFTWARE, null);
    //             nView.draw(canvas);
    //             resolve(new ImageSource(bmp));
    //         } else {
    //             const size = nView.frame.size;
    //             try {
    //                 // Use UIGraphicsBeginImageContextWithOptions to specify the scale (dpi multiplier)
    //                 UIGraphicsBeginImageContextWithOptions(size, false, 2);
    //                 nView.drawViewHierarchyInRectAfterScreenUpdates(CGRectMake(0, 0, size.width, size.height), true);
    //                 const imageFromContext = UIGraphicsGetImageFromCurrentImageContext();
    //                 UIGraphicsEndImageContext();
    //                 resolve(new ImageSource(imageFromContext));
    //             } catch (err) {
    //                 DEV_LOG && console.log(err, err.stack);
    //                 // fallback to previous renderer if anything goes wrong
    //                 const renderer = new UIGraphicsImageRenderer({ size });
    //                 const image = renderer.imageWithActions((context) => {
    //                     nView.drawViewHierarchyInRectAfterScreenUpdates(CGRectMake(0, 0, size.width, size.height), true);
    //                 });
    //                 resolve(new ImageSource(image));
    //             }
    //         }
    //     };
    //     view.once('layoutChanged', onLayoutChanged);
    //     gesturerootview.measure(Utils.layout.makeMeasureSpec(0, Utils.layout.UNSPECIFIED), Utils.layout.makeMeasureSpec(0, Utils.layout.UNSPECIFIED));

    //     gesturerootview.layout(0, 0, gesturerootview.getMeasuredWidth(), gesturerootview.getMeasuredHeight());
    // });
    // (rootView as GridLayout).removeChild(gesturerootview);
    // componentInstanceInfo.element.nativeElement._tearDownUI();
    // componentInstanceInfo.viewInstance?.$destroy();
    // DEV_LOG && console.log('imageSource', imageSource.width, imageSource.height);
    // return imageSource;

    const width = Utils.layout.toDevicePixels(options?.width ?? screenWidthDips);
    const height = options?.height ? Utils.layout.toDevicePixels(options?.height) : undefined;

    // canvas.drawColor('green')
    const canvas = await renderPKPassToCanvas(pkpass, {
        ...options,
        width: Utils.layout.toDevicePixels(width),
        height: height ? Utils.layout.toDevicePixels(height) : undefined
    });
    const result = new ImageSource(canvas.getImage());
    // DEV_LOG && console.log('pkpassToImage', width, canvas.getWidth(), result.width, result.height);
    // const paint = new Paint();
    // paint.color = 'black'
    // canvas.drawText("test", 0, 4, 20,60, paint)
    return result;
}

/**
 * Render PKPass to a canvas for export to PDF or image
 * @param canvas Canvas element to render to
 * @param options Rendering options
 * @returns Promise that resolves when rendering is complete
 */
export async function renderPKPassToCanvas(
    pkpass: PKPass,
    options: {
        canvas?: Canvas;
        createCanvas?: Function;
        lang: string;
        layout?: 'card' | 'full'; // Card for compact credit-card sized, full for complete details
        includeBackFields?: boolean;
        backgroundColor?: string;
        width?: number;
        height?: number;
    }
): Promise<Canvas> {
    const passData = pkpass.passData;
    let canvas = options.canvas;
    const { backgroundColor = passData.backgroundColor || '#ffffff', includeBackFields = false, layout = 'full', width = 400 } = options;

    // Calculate dimensions based on layout
    const baseWidth = 1100;
    const scaleFactor = width / baseWidth;
    // DEV_LOG && console.log('renderPKPassToCanvas', width, scaleFactor);

    // Credit card aspect ratio: 1.586:1
    const cardHeight = Math.round(width / CARD_RATIO);

    // For full layout, compute height dynamically based on content
    let computedHeight = 0;
    if (layout === 'card') {
        computedHeight = cardHeight;
    } else {
        // Estimate height for full layout
        computedHeight = width;
    }

    const canvasWidth = Utils.layout.toDeviceIndependentPixels(width);
    const canvasHeight = Utils.layout.toDeviceIndependentPixels(options.height || computedHeight);

    // Create paints for rendering
    // const bgPaint = new Paint();
    // bgPaint.color = backgroundColor;

    const fgPaint = new Paint();
    const fgColor = passData.foregroundColor || '#000000';
    fgPaint.color = fgColor;

    const labelPaint = new Paint();
    const labelColor = passData.labelColor || passData.foregroundColor || '#666666';
    labelPaint.color = labelColor;

    const iconPaint = new Paint();
    iconPaint.color = labelColor;
    iconPaint.fontFamily = MDI_FONT_FAMILY;

    const imagePaint = new Paint();

    // Draw background

    const padding = 20 * scaleFactor;
    const sectionSpacing = 0 * scaleFactor;

    // Get pass structure and style
    const structure = pkpass.getPassStructure();
    const style = pkpass.getPassStyle();

    if (layout === 'card') {
        imagePaint.color = backgroundColor;
        if (options.createCanvas) {
            canvas = options.createCanvas(width, computedHeight);
        } else if (!canvas) {
            canvas = new Canvas(width, computedHeight);
        }
        canvas.drawRoundRect(0, 0, width, computedHeight, 12 * scaleFactor, 12 * scaleFactor, imagePaint);
        // Card layout: compact credit-card sized rendering
        return renderCardLayout({
            pkpass,
            canvas,
            canvasWidth,
            scaleFactor,
            structure,
            style,
            padding,
            labelColor,
            fgColor,
            imagePaint,
            iconPaint,
            lang: options.lang,
            fgPaint,
            labelPaint
        });
    } else {
        // Full layout: comprehensive pass view
        return renderFullLayout({
            pkpass,
            canvas,
            createCanvas: options.createCanvas,
            canvasWidth,
            backgroundColor,
            scaleFactor,
            structure,
            style,
            padding,
            sectionSpacing,
            fgPaint,
            labelPaint,
            includeBackFields,
            labelColor,
            fgColor,
            imagePaint,
            iconPaint,
            lang: options.lang
        });
    }
}

/**
 * Render card layout (compact)
 */
async function renderCardLayout({
    canvas,
    canvasWidth,
    fgColor,
    fgPaint,
    iconPaint,
    imagePaint,
    labelColor,
    labelPaint,
    lang,
    padding,
    pkpass,
    scaleFactor,
    structure,
    style
}: {
    pkpass: PKPass;
    canvas: Canvas;
    canvasWidth: number;
    scaleFactor: number;
    structure: PKPassStructure;
    style: PKPassStyle;
    padding: number;
    labelPaint: Paint;
    fgPaint: Paint;
    iconPaint: Paint;
    labelColor: string;
    fgColor: string;
    imagePaint: Paint;
    lang: string;
}): Promise<Canvas> {
    let y = padding;

    // Top row: Logo or Icon + Name, and header fields
    const headerY = y;
    const logoX = padding;

    // Apple spec: prefer @2x images for quality
    const logo2x = pkpass.images.logo2x || pkpass.images.logo;
    const icon2x = pkpass.images.icon2x || pkpass.images.icon;

    // Draw logo (Apple spec: max 160x50, scaled to 80px width for card) or icon (Apple spec: 29x29, scaled to ~20px)
    if (logo2x) {
        // Apple spec: max 160x50 points, scale proportionally for card
        const logoImage = await loadImage(logo2x, { width: Math.round(80 * scaleFactor), height: Math.round(40 * scaleFactor) });
        canvas.drawBitmap(logoImage, logoX, headerY, imagePaint);
        recycleImages(logoImage);
    } else if (icon2x) {
        // Apple spec: 29x29 points, scaled to ~20px for card
        const iconImage = await loadImage(icon2x, { width: Math.round(20 * scaleFactor), height: Math.round(20 * scaleFactor) });
        canvas.drawBitmap(iconImage, logoX, headerY, imagePaint);
        recycleImages(iconImage);

        // Draw organization name next to icon
        fgPaint.setTextAlign(Align.LEFT);
        fgPaint.textSize = 14 * scaleFactor;
        canvas.drawText(pkpass.passData.organizationName || '', logoX + 26 * scaleFactor, headerY + 15 * scaleFactor, fgPaint);
    } else {
        // Just name
        fgPaint.setTextAlign(Align.LEFT);
        fgPaint.textSize = 14 * scaleFactor;
        canvas.drawText(pkpass.passData.organizationName || '', logoX, headerY + 15 * scaleFactor, fgPaint);
    }

    // Draw header fields on the right
    if (structure?.headerFields?.length) {
        drawFieldGroup(canvas, {
            pkpass,
            canvasWidth,
            fields: structure.headerFields,
            startX: canvasWidth - padding,
            startY: y,
            scaleFactor,
            padding,
            labelColor,
            fgColor,
            labelSize: 10,
            valueSize: 13,
            lang,
            horizontalAlignment: 'right'
        });
        // const headerFieldsX = canvasWidth - padding - 100 * scaleFactor;
        // let fieldY = headerY;
        // for (const field of structure.headerFields) {
        //     labelPaint.textSize = 1 * scaleFactor;
        //     const label = pkpass.getLocalizedValue(field.label || field.key, lang);
        //     canvas.drawText(label, headerFieldsX, fieldY, labelPaint);

        //     fgPaint.textSize = 14 * scaleFactor;
        //     const value = pkpass.formatFieldValue(field, lang);
        //     canvas.drawText(value, headerFieldsX, fieldY + 15 * scaleFactor, fgPaint);

        //     fieldY += 30 * scaleFactor;
        // }
    }

    y += 50 * scaleFactor;

    // Middle row: Primary fields with optional transit icon
    const transitType = structure?.transitType;
    if (transitType && structure?.primaryFields?.length === 2) {
        // Boarding pass layout with transit icon
        const fieldWidth = (canvasWidth - 2 * padding - 50 * scaleFactor) / 2;

        // Left field (departure)
        drawFieldGroup(canvas, {
            pkpass,
            fields: [structure.primaryFields[0]],
            startX: logoX,
            startY: y,
            canvasWidth,
            padding,
            labelColor,
            fgColor,
            labelSize: 10,
            valueSize: 24,
            lang,
            scaleFactor
        });

        const transitIcon = getTransitIcon(transitType);
        // Transit icon (center) - simplified, just text representation
        iconPaint.textSize = 32 * scaleFactor;
        canvas.drawText(transitIcon, padding + fieldWidth, y + 20 * scaleFactor, iconPaint);

        drawFieldGroup(canvas, {
            pkpass,
            fields: [structure.primaryFields[1]],
            startX: logoX,
            startY: y,
            canvasWidth,
            padding,
            labelColor,
            fgColor,
            labelSize: 10,
            valueSize: 24,
            lang,
            scaleFactor
        });

        y += 60 * scaleFactor;
    } else if (structure?.primaryFields?.length) {
        y = drawFieldGroup(canvas, {
            pkpass,
            fields: structure.secondaryFields,
            startX: logoX,
            startY: y,
            canvasWidth,
            padding,
            labelColor,
            fgColor,
            labelSize: 10,
            valueSize: 24,
            lang,
            scaleFactor
        });
    }

    y += 20 * scaleFactor;

    // Bottom row: Secondary and auxiliary fields
    if (structure?.secondaryFields?.length) {
        y = drawFieldGroup(canvas, {
            pkpass,
            fields: structure.secondaryFields,
            startX: logoX,
            startY: y,
            canvasWidth,
            padding,
            labelColor,
            fgColor,
            labelSize: 9,
            valueSize: 11,
            lang,
            scaleFactor
        });
        y += 10 * scaleFactor;
    }

    if (structure?.auxiliaryFields?.length) {
        y = drawFieldGroup(canvas, { pkpass, fields: structure.auxiliaryFields, startX: logoX, startY: y, canvasWidth, padding, labelColor, fgColor, labelSize: 9, valueSize: 11, lang, scaleFactor });
    }
    return canvas;
}

/**
 * Render full layout (comprehensive)
 */
async function renderFullLayout({
    backgroundColor,
    canvas,
    canvasWidth,
    createCanvas,
    fgColor,
    fgPaint,
    iconPaint,
    imagePaint,
    includeBackFields,
    labelColor,
    labelPaint,
    lang,
    padding,
    pkpass,
    scaleFactor,
    sectionSpacing,
    structure,
    style
}: {
    pkpass: PKPass;
    canvas: Canvas;
    createCanvas?: Function;
    canvasWidth: number;
    scaleFactor: number;
    structure: PKPassStructure;
    style: PKPassStyle;
    padding: number;
    backgroundColor: string;
    labelColor: string;
    fgColor: string;
    labelPaint: Paint;
    fgPaint: Paint;
    iconPaint: Paint;
    sectionSpacing: number;
    includeBackFields: boolean;
    imagePaint: Paint;
    lang: string;
}): Promise<Canvas> {
    // Apple spec: prefer @2x images for quality
    const strip2x = pkpass.images.strip2x || pkpass.images.strip;
    const background2x = pkpass.images.background2x || pkpass.images.background;
    const icon2x = pkpass.images.icon2x || pkpass.images.icon;
    const logo2x = pkpass.images.logo2x || pkpass.images.logo;
    const thumbnail2x = pkpass.images.thumbnail2x || pkpass.images.thumbnail;
    const footer2x = pkpass.images.footer2x || pkpass.images.footer;

    // first we compute all layouts to measure
    const imagesToDraw = [];
    const staticLayoutsToDraw = [];
    const svgsToDraw = [];
    let y = padding;
    // DEV_LOG && console.log('renderFullLayout', canvasWidth);
    if (background2x && style === PKPassStyle.EventTicket) {
        const bgImage = await loadImage(background2x, { width: canvasWidth });
        imagesToDraw.push({ image: bgImage, x: 0, y: 0, width: '100%', height: '100%' });
    }

    const headerY = y;
    const logoX = padding;

    // Draw logo if available (Apple spec: max 160x50 points)
    let headerRemainingWidth = canvasWidth - 2 * padding;
    if (logo2x) {
        const height = Math.round(50 * scaleFactor);
        const image = await loadImage(logo2x, { height });
        const scale = height / image.height;
        headerRemainingWidth -= scale * image.width;
        imagesToDraw.push({ image, x: logoX, y: headerY, height });
    } else if (icon2x) {
        const height = Math.round(50 * scaleFactor);
        const image = await loadImage(icon2x, { height });
        const scale = height / image.height;
        headerRemainingWidth -= scale * image.width;
        imagesToDraw.push({ image, x: logoX, y: headerY, height });
    }
    const orgName = pkpass.passData?.organizationName || pkpass.getLocalizedValue(pkpass.passData?.logoText, lang) || '';
    if (orgName && !logo2x) {
        staticLayoutsToDraw.push({
            staticLayout: new StaticLayout(
                createNativeAttributedString({
                    spans: [
                        {
                            text: orgName,
                            fontSize: 14 * scaleFactor,
                            fontWeight: 'bold'
                        }
                    ]
                }),
                fgPaint,
                headerRemainingWidth,
                LayoutAlignment.ALIGN_NORMAL,
                1,
                0,
                true
            ),
            x: logoX,
            y: headerY + Math.round((25 - 7) * scaleFactor)
        });
    }

    // Draw header fields
    if (structure?.headerFields?.length) {
        // we draw aligned right
        addFieldGroup(
            {
                pkpass,
                canvasWidth: headerRemainingWidth,
                fields: structure.headerFields,
                startX: canvasWidth - padding,
                startY: y,
                scaleFactor,
                padding,
                labelColor,
                fgColor,
                labelSize: 13,
                valueSize: 17,
                lang,
                horizontalAlignment: 'right'
            },
            staticLayoutsToDraw
        );
        // y += sectionSpacing;
    }

    y = headerY + 50 * scaleFactor;

    if (pkpass.passData?.organizationName) {
        y += 20 * scaleFactor;
        staticLayoutsToDraw.push({
            text: pkpass.passData?.organizationName,
            textSize: 14 * scaleFactor,
            textAlign: Align.CENTER,
            x: canvasWidth / 2,
            y,
            paint: labelPaint
        });
    }
    y += 16 * scaleFactor;
    y += 16 * scaleFactor;

    // Draw primary fields (larger text)
    const transitType = structure?.transitType;
    if (transitType && structure?.primaryFields?.length === 2) {
        // Boarding pass layout with transit icon
        addFieldGroup(
            {
                pkpass,
                fields: [structure.primaryFields[0]],
                startX: padding,
                startY: y,
                scaleFactor,
                padding,
                labelColor,
                valueFontWeight: 'bold',
                fgColor,
                labelSize: 12,
                valueSize: 32,
                lang,
                canvasWidth
            },
            staticLayoutsToDraw
        );

        const transitIcon = getTransitIcon(transitType);
        // Transit icon (center) - simplified, just text representation
        staticLayoutsToDraw.push({
            text: transitIcon,
            textSize: 50 * scaleFactor,
            textAlign: Align.CENTER,
            x: canvasWidth / 2,
            y: y + 50 * scaleFactor,
            paint: iconPaint
        });

        y = addFieldGroup(
            {
                pkpass,
                fields: [structure.primaryFields[1]],
                startX: canvasWidth - padding,
                startY: y,
                scaleFactor,
                padding,
                labelColor,
                fgColor,
                labelSize: 12,
                valueSize: 32,
                lang,
                valueFontWeight: 'bold',
                horizontalAlignment: 'right',
                canvasWidth: canvasWidth / 2
            },
            staticLayoutsToDraw
        );
        y += 16 * scaleFactor;
    } else if (structure?.primaryFields?.length) {
        if (strip2x && style !== PKPassStyle.BoardingPass) {
            const stripImage = await loadImage(strip2x, { width: canvasWidth, height: Math.round(150 * scaleFactor) });
            imagesToDraw.push({ image: stripImage, x: 0, y });
        }
        // Draw thumbnail if available (Apple spec: 90x90 points)
        if (thumbnail2x) {
            const image = await loadImage(thumbnail2x, { height: Math.round(90 * scaleFactor) });
            imagesToDraw.push({ image, x: 0, y, height: Math.round(90 * scaleFactor), rightAligned: true });
        }
        y = addFieldGroup(
            {
                pkpass,
                fields: structure.primaryFields,
                startX: logoX,
                startY: y,
                scaleFactor,
                padding,
                labelColor,
                fgColor,
                valueFontWeight: 'bold',
                labelSize: 12,
                valueSize: 32,
                lang,
                canvasWidth
            },
            staticLayoutsToDraw
        );
        y += 16 * scaleFactor;
    }

    // Draw secondary fields
    if (structure?.secondaryFields?.length) {
        y += 16 * scaleFactor;
        y = addFieldGroup(
            {
                pkpass,
                fields: structure.secondaryFields,
                startX: logoX,
                startY: y,
                canvasWidth,
                padding,
                labelColor,
                fgColor,
                labelSize: 11,
                valueSize: 18,
                lang,
                scaleFactor
            },
            staticLayoutsToDraw
        );
    }

    // Draw auxiliary fields
    if (structure?.auxiliaryFields?.length) {
        y += 16 * scaleFactor;
        y = addFieldGroup(
            { pkpass, fields: structure.auxiliaryFields, startX: logoX, startY: y, canvasWidth, padding, labelColor, fgColor, labelSize: 10, valueSize: 14, lang, scaleFactor },
            staticLayoutsToDraw
        );
    }
    y += 24 * scaleFactor;

    // Draw footer image if available (Apple spec: 286x15 points)
    if (footer2x) {
        try {
            const image = await loadImage(footer2x, { height: Math.round(15 * scaleFactor) });
            imagesToDraw.push({ image, x: 0, y, height: Math.round(1590 * scaleFactor), width: '100%' });
            y += 20 * scaleFactor;
        } catch (error) {
            console.error('Error rendering footer image:', error);
        }
    }
    // Draw barcode if available
    const barcode = pkpass.getPrimaryBarcode();
    if (barcode) {
        try {
            const barcodeWidth = Math.round((canvasWidth - 2 * padding) / 2);

            const barcodeImage = await getBarcodeSVG({ barcode, foregroundColor: pkpass.passData.foregroundColor || '#000000' });

            if (barcodeImage) {
                svgsToDraw.push({
                    src: barcodeImage,
                    width: barcodeWidth,
                    height: barcodeWidth,
                    horizontalAlignment: 'middle',
                    x: (canvasWidth - barcodeWidth) / 2,
                    y
                });

                y += barcodeWidth + 10 * scaleFactor;
                // Draw barcode alt text if available
                if (barcode.altText) {
                    staticLayoutsToDraw.push({
                        text: barcode.altText,
                        textSize: 12 * scaleFactor,
                        textAlign: Align.CENTER,
                        x: canvasWidth / 2,
                        y,
                        paint: fgPaint
                    });
                }
            }
        } catch (error) {
            console.error('Error rendering barcode:', error);
        }
    }

    // Draw back fields if requested
    if (includeBackFields && structure?.backFields?.length) {
        y += Math.round(44 * scaleFactor);
        staticLayoutsToDraw.push({
            text: lc('additional_information'),
            textSize: 16 * scaleFactor,
            fontWeight: 'bold',
            x: logoX,
            y,
            paint: labelPaint
        });
        y += Math.round(12 * scaleFactor);
        for (let i = 0; i < structure.backFields.length; i++) {
            y = addFieldGroup(
                {
                    pkpass,
                    fields: [structure.backFields[i]],
                    startX: logoX,
                    startY: y,
                    valueFontWeight: 'normal',
                    canvasWidth,
                    padding,
                    labelColor,
                    fgColor,
                    labelSize: 11,
                    valueSize: 14,
                    lang,
                    scaleFactor,
                    fieldSpacing: 10
                },
                staticLayoutsToDraw
            );
            y += Math.round(10 * scaleFactor);
        }
    }
    y += padding;
    if (createCanvas) {
        canvas = createCanvas(canvasWidth, y);
    } else if (!canvas) {
        canvas = new Canvas(canvasWidth, y);
    }
    canvas.drawColor(backgroundColor);

    for (let i = 0; i < imagesToDraw.length; i++) {
        const imageData = imagesToDraw[i];
        const imageSource = imageData.image;
        // const srcRect = new Rect(0, 0, imageSource.width, imageSource.height);
        let scale = 1;
        let width = imageSource.width;
        if (imageData.width) {
            width = imageData.width;
            if (typeof width === 'string') {
                width = PercentLength.toDevicePixels(PercentLength.parse(width));
            }
            scale = width / imageSource.width;
        }
        let height = imageSource.height;
        if (imageData.height) {
            height = imageData.height;
            if (typeof height === 'string') {
                height = PercentLength.toDevicePixels(PercentLength.parse(height));
            }
            if (!imageData.width) {
                scale = height / imageSource.height;
            }
        }
        // DEV_LOG && console.log('draw image', imageSource, imageSource.width, imageSource.height,scale , imageData.height );
        const dstRect = new Rect(
            imageData.rightAligned ? imageData.x - imageSource.width * scale : imageData.x,
            imageData.y,
            imageData.rightAligned ? imageData.x : imageData.x + imageSource.width * scale,
            imageData.y + imageSource.height * scale
        );
        canvas.drawBitmap(imageSource, null, dstRect, imagePaint);
    }
    for (let i = 0; i < staticLayoutsToDraw.length; i++) {
        const staticLayoutData = staticLayoutsToDraw[i];
        if (staticLayoutData.staticLayout) {
            // canvas.drawRect(staticLayoutData.x, staticLayoutData.y, staticLayoutData.x + staticLayoutData.width, staticLayoutData.y + staticLayoutData.height, testPaint);
            canvas.save();
            canvas.translate(staticLayoutData.x, staticLayoutData.y);
            staticLayoutData.staticLayout.draw(canvas);
            canvas.restore();
        } else if (staticLayoutData.text) {
            const paint = staticLayoutData.paint as Paint;
            let oldTextAlign;
            let oldFontWeight;
            if (staticLayoutData.textSize) {
                paint.setTextSize(staticLayoutData.textSize);
            }
            if (staticLayoutData.textAlign !== undefined) {
                oldTextAlign = paint.getTextAlign();
                paint.setTextAlign(staticLayoutData.textAlign);
            }
            if (staticLayoutData.fontWeight !== undefined) {
                oldFontWeight = paint.fontWeight;
                paint.fontWeight = staticLayoutData.fontWeight;
            }
            canvas.drawText(staticLayoutData.text, staticLayoutData.x, staticLayoutData.y, staticLayoutData.paint);

            if (oldTextAlign !== undefined) {
                paint.setTextAlign(oldTextAlign);
            }
            if (oldFontWeight !== undefined) {
                paint.fontWeight = oldFontWeight;
            }
        }
    }

    for (let index = 0; index < svgsToDraw.length; index++) {
        const svgData = svgsToDraw[index];
        const svg = new SVG();
        svg.width = svgData.width;
        svg.height = svgData.height;
        svg.src = svgData.src;
        svg.cache = false;
        svg.horizontalAlignment = 'middle';
        canvas.save();
        canvas.translate(svgData.x, svgData.y);
        svg.drawMyShapeOnCanvas(canvas, {} as any, svg.width as any, svg.height as any);
        canvas.restore();
    }

    recycleImages(imagesToDraw.map((d) => d.image));
    return canvas;
}

function drawFieldGroup(
    canvas,
    {
        canvasWidth,
        fgColor,
        fields,
        fieldSpacing = 10,
        horizontalAlignment = 'left',
        labelColor,
        labelSize,
        lang,
        padding,
        pkpass,
        scaleFactor,
        startX,
        startY,
        valueFontWeight,
        valueSize
    }: {
        pkpass: PKPass;
        fields: PKPassField[];
        startX: number;
        startY: number;
        scaleFactor: number;
        canvasWidth;
        padding: number;
        fieldSpacing?: number;
        labelColor: string;
        fgColor: string;
        labelSize: number;
        valueSize: number;
        valueFontWeight?: string;
        lang: string;
        horizontalAlignment?: string;
    }
): number {
    const staticLayoutsToDraw = [];
    const y = addFieldGroup(
        {
            canvasWidth,
            fgColor,
            fields,
            fieldSpacing,
            horizontalAlignment,
            labelColor,
            labelSize,
            lang,
            padding,
            pkpass,
            scaleFactor,
            startX,
            startY,
            valueFontWeight,
            valueSize
        },
        staticLayoutsToDraw
    );
    for (let index = 0; index < staticLayoutsToDraw.length; index++) {
        const staticLayoutData = staticLayoutsToDraw[index];
        canvas.save();
        canvas.translate(staticLayoutData.x, staticLayoutData.y);
        staticLayoutData.staticLayout.draw(canvas);
        canvas.restore();
    }
    return y;
}
/**
 * Helper method to draw a group of fields
 */
function addFieldGroup(
    {
        canvasWidth,
        fgColor,
        fields,
        fieldSpacing = 10,
        horizontalAlignment = 'left',
        labelColor,
        labelSize,
        lang,
        padding,
        pkpass,
        scaleFactor,
        startX,
        startY,
        valueFontWeight,
        valueSize
    }: {
        pkpass: PKPass;
        fields: PKPassField[];
        startX: number;
        startY: number;
        scaleFactor: number;
        canvasWidth;
        padding: number;
        fieldSpacing?: number;
        labelColor: string;
        fgColor: string;
        labelSize: number;
        valueSize: number;
        valueFontWeight?: string;
        lang: string;
        horizontalAlignment?: string;
    },
    staticLayoutsToDraw
): number {
    const y = startY;
    const fieldWidth = (canvasWidth - 2 * padding - (fields.length - 1) * fieldSpacing * scaleFactor) / fields.length;
    let x = startX;
    let deltaY = 0;
    (horizontalAlignment === 'right' ? fields.reverse() : fields).forEach((field, index) => {
        const paint = new Paint();
        paint.fontWeight = '500';
        const topText = createNativeAttributedString({
            spans: [
                {
                    text: field.label || field.key ? pkpass.getLocalizedValue(field.label || field.key, lang).toUpperCase() + '\n' : '',
                    fontSize: labelSize * scaleFactor,
                    fontWeight: '500',
                    color: labelColor,
                    lineHeight: 16 * scaleFactor
                },
                {
                    text: pkpass.formatFieldValue(field, lang),
                    fontWeight: valueFontWeight ?? '500',
                    fontSize: valueSize * scaleFactor,
                    color: fgColor
                }
            ]
        });
        let textAlignment = LayoutAlignment.ALIGN_NORMAL;
        let deltaX = 0;
        if (field.textAlignment === 'PKTextAlignmentRight') {
            paint.setTextAlign(Align.RIGHT);
            if (horizontalAlignment !== 'right') {
                deltaX += fieldWidth;
            }
            // textAlignment = LayoutAlignment.ALIGN_OPPOSITE;
        } else if (field.textAlignment === 'PKTextAlignmentCenter') {
            // paint.setTextAlign(Align.CENTER);
            textAlignment = LayoutAlignment.ALIGN_CENTER;
        } else {
            paint.setTextAlign(Align.LEFT);
        }
        // DEV_LOG && console.log('addFieldGroup', field.label, field.textAlignment, horizontalAlignment, canvasWidth, padding, fieldSpacing, fields.length, fieldWidth);
        if (__IOS__ && horizontalAlignment === 'right') {
            paint.setTextAlign(Align.RIGHT);
        }
        const staticLayout = new StaticLayout(topText, paint, fieldWidth, textAlignment, 1, 0, true);
        const actualWidth = staticLayout.getActualWidth();
        // if (horizontalAlignment === 'right') {
        //     deltaX -= actualWidth;
        // }
        // canvas.translate(x + deltaX, y);
        staticLayoutsToDraw.push({ staticLayout, x: x + deltaX, y, width: fieldWidth, actualWidth, height: staticLayout.getHeight() });
        // staticLayout.draw(canvas);
        // canvas.restore();
        deltaY = Math.max(deltaY, staticLayout.getHeight());

        if (horizontalAlignment === 'right') {
            x -= fieldWidth + fieldSpacing * scaleFactor;
        } else {
            x += fieldWidth + fieldSpacing * scaleFactor;
        }
    });

    return y + deltaY;
}
