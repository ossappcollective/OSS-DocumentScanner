import { IgnoreError, PermissionError, SilentError } from '@akylas/nativescript-app-utils/error';
import { share } from '@akylas/nativescript-app-utils/share';
import { MultiResult, Permissions, Status, isPermResultAuthorized, openSettings, request } from '@nativescript-community/perms';
import { ComponentInstanceInfo, resolveComponentElement } from '@nativescript-community/svelte-native/dom';
import { openFilePicker, pickFolder } from '@nativescript-community/ui-document-picker';
import { Label } from '@nativescript-community/ui-label';
import { showBottomSheet } from '@nativescript-community/ui-material-bottomsheet/svelte';
import { MDCAlertControlerOptions, alert, confirm, prompt } from '@nativescript-community/ui-material-dialogs';
import { HorizontalPosition, PopoverOptions, VerticalPosition } from '@nativescript-community/ui-popover';
import { closePopover, showPopover } from '@nativescript-community/ui-popover/svelte';
import {
    Animation,
    AnimationDefinition,
    Application,
    ApplicationSettings,
    Color,
    File,
    GridLayout,
    ImageSource,
    ObservableArray,
    PageTransition,
    Screen,
    SharedTransition,
    Utils,
    View,
    knownFolders,
    path
} from '@nativescript/core';
import { ConfirmOptions } from '@nativescript/core/ui/dialogs/dialogs-common';
import { SDK_VERSION, copyToClipboard, debounce, openFile } from '@nativescript/core/utils';
import { create as createImagePicker } from '@nativescript/imagepicker';
import { doInBatch } from '@shared/utils/batch';
import { showError } from '@shared/utils/showError';
import { goBack, navigate, showModal } from '@shared/utils/svelte/ui';
import { hideLoading, showLoading, showSnack, updateLoadingProgress } from '@shared/utils/ui';
import dayjs from 'dayjs';
import {
    CropResult,
    Quads,
    cropDocumentFromFile,
    detectQRCodeFromFile,
    getFileName,
    getImageSize,
    getJSONDocumentCornersFromFile,
    importPdfToTempImages,
    printPDF,
    processFromFile
} from 'plugin-nativeprocessor';
import { zip } from 'plugin-zip';
import type { ComponentProps } from 'svelte';
import { get } from 'svelte/store';
import type ExportPDFAlertOptions__SvelteComponent_ from '~/components/common/ExportPDFAlertOptions.svelte';
import type OptionSelect__SvelteComponent_ from '@shared/components/OptionSelect.svelte';
import type BottomSnack__SvelteComponent_ from '~/components/widgets/BottomSnack.svelte';
import BottomSnack from '~/components/widgets/BottomSnack.svelte';
import { getFileNameForDocument, getFormatedDateForFilename, getLocaleDisplayName, l, lang, lc } from '~/helpers/locale';
import { DocFolder, ImportImageData, OCRDocument, OCRPage, PageData } from '~/models/OCRDocument';
import { PKPassType } from '~/models/PKPass';
import { OCRLanguages, ocrService } from '~/services/ocr';
import { getPDFDefaultExportOptions } from '~/services/pdf/PDFCanvas';
import { getTransformedImage } from '~/services/pdf/PDFExportCanvas.common';
import { exportPDFAsync } from '~/services/pdf/PDFExporter';
import { securityService } from '~/services/security';
import {
    ALWAYS_PROMPT_CROP_EDIT,
    ANDROID_CONTENT,
    AREA_SCALE_MIN_FACTOR,
    COLOR_PALETTE_RESIZE_THRESHOLD,
    CROP_ENABLED,
    DEFAULT_BRIGHTNESS,
    DEFAULT_COLORMATRIX,
    DEFAULT_COLORTYPE,
    DEFAULT_CONTRAST,
    DEFAULT_EXPORT_DIRECTORY,
    DEFAULT_OCR_COPY_USE_SPACE,
    DEFAULT_TRANSFORM,
    DOCUMENT_NOT_DETECTED_MARGIN,
    ESPASS_EXT,
    PDFImportImages,
    PDF_EXT,
    PDF_IMPORT_IMAGES,
    PKPASS_EXT,
    PREVIEW_RESIZE_THRESHOLD,
    QRCODE_RESIZE_THRESHOLD,
    SEPARATOR,
    SETTINGS_ALWAYS_PROMPT_CROP_EDIT,
    SETTINGS_CROP_ENABLED,
    SETTINGS_DEFAULT_BRIGHTNESS,
    SETTINGS_DEFAULT_COLORMATRIX,
    SETTINGS_DEFAULT_COLORTYPE,
    SETTINGS_DEFAULT_CONTRAST,
    SETTINGS_DEFAULT_TRANSFORM,
    SETTINGS_IMPORT_PDF_IMAGES,
    SETTINGS_OCR_COPY_USE_SPACE,
    SETTINGS_TRANSFORM_BATCH_SIZE,
    TRANSFORMS_SPLIT,
    TRANSFORM_BATCH_SIZE,
    USE_SYSTEM_CAMERA,
    getImageExportSettings
} from '~/utils/constants';
import { recycleImages } from '~/utils/images';
import { buildPassArchive, getStoredPassFormat } from '~/utils/pkpass';
import { importPKPassFiles } from '~/utils/pkpass-import';
import { showToast } from '~/utils/ui';
import { colors, fontScale, screenWidthDips } from '~/variables';
import { MatricesTypes, Matrix } from '../color_matrix';
import { cleanFilename, saveImage } from '../utils';

export { ColorMatricesType, ColorMatricesTypes, getColorMatrix } from '~/utils/matrix';

export * from '@shared/utils/ui';

export async function importAndScanImageOrPdfFromUris({ canGoToView = true, document, folder, uris }: { uris: string[]; document?: OCRDocument; canGoToView?: boolean; folder?: DocFolder }) {
    let pagesToAdd: PageData[] = [];
    let items: ImportImageData[] = [];
    try {
        await showLoading(lc('computing'));
        const noDetectionMargin = ApplicationSettings.getNumber('documentNotDetectedMargin', DOCUMENT_NOT_DETECTED_MARGIN);
        const previewResizeThreshold = ApplicationSettings.getNumber('previewResizeThreshold', PREVIEW_RESIZE_THRESHOLD);
        const areaScaleMinFactor = ApplicationSettings.getNumber('areaScaleMinFactor', AREA_SCALE_MIN_FACTOR);
        const resizeThreshold = previewResizeThreshold * 1.5;
        const cropEnabled = ApplicationSettings.getBoolean(SETTINGS_CROP_ENABLED, CROP_ENABLED);

        const [pdf, images, pkpasses] = await uris.reduce(
            async (acc, e) => {
                let testStr = e.toLowerCase();
                if (__ANDROID__ && e.startsWith(ANDROID_CONTENT)) {
                    testStr = await getFileName(e);
                }
                acc.then((obj) => {
                    if (CARD_APP && (testStr.endsWith(PKPASS_EXT) || testStr.endsWith(ESPASS_EXT))) {
                        obj[2].push(e);
                    } else if (testStr.endsWith(PDF_EXT)) {
                        obj[0].push(e);
                    } else {
                        obj[1].push(e);
                    }
                });
                return acc;
                // return testStr.endsWith(PDF_EXT) ? [[...p, e], f] : [p, [...f, e]];
            },
            Promise.resolve([[], [], []] as [string[], string[], string[]])
        );
        DEV_LOG && console.log('importAndScanImageOrPdfFromUris', pdf, images, pkpasses);
        if (CARD_APP && pkpasses.length) {
            await importPKPassFromUris({ uris: pkpasses, canGoToView: true });
            return;
        }
        // First we check/ask the user if he wants to import PDF pages or images
        let pdfImportsImages = ApplicationSettings.getString(SETTINGS_IMPORT_PDF_IMAGES, PDF_IMPORT_IMAGES) as PDFImportImages;
        if (pdf.length > 0 && pdfImportsImages === PDFImportImages.ask) {
            const options = new ObservableArray([
                { group: 'pdf', name: lc('pdf_one_image_per_page'), data: PDFImportImages.never, type: 'checkbox', boxType: 'circle', value: true },
                { group: 'pdf', name: lc('pdf_one_image_per_pdf_image'), data: PDFImportImages.always, type: 'checkbox', boxType: 'circle', value: false }
            ]);
            // const component = (await import('@shared/components/OptionSelect.svelte')).default;
            const result = await showConfirmOptionSelect<PDFImportImages>(
                {
                    height: Math.min(options.length * 100, 400),
                    autoSizeListItem: true,
                    onlyOneSelectedCheckbox: true,
                    fontWeight: 'normal',
                    selectedIndex: 0,
                    options,
                    onCheckBox: () => {} // we dont want to close on checkbox which is the default
                },
                {
                    title: lc('import_pdf_images'),
                    message: lc('import_pdf_images_desc'),
                    okButtonText: lc('always'),
                    cancelButtonText: lc('just_once'),
                    iosForceClosePresentedViewController: true // force close current loading dialog, we ll show it again after
                }
            );
            pdfImportsImages = result.data;
            if (result.confirmed) {
                ApplicationSettings.setString(SETTINGS_IMPORT_PDF_IMAGES, pdfImportsImages);
            }
            if (__IOS__) {
                //we forced close current loading dialog,let s show it again after
                await showLoading(lc('computing'));
            }
            DEV_LOG && console.log('showPromptOptionSelect done', result);
        }
        const imageExportSettings = getImageExportSettings();

        // now we process PDF files
        // We do it in batch of 5 to prevent memory issues
        const pdfImages = await doInBatch(
            pdf,
            (pdfPath: string) =>
                new Promise<string[]>(async (resolve, reject) => {
                    try {
                        const start = Date.now();
                        DEV_LOG && console.log('importFromPdf', pdfPath, Date.now() - start, 'ms');

                        const pdfImages = await importPdfToTempImages(pdfPath, {
                            importPDFImages: pdfImportsImages === PDFImportImages.always,
                            compressFormat: 'png', // in case images are with transparent background we need to use png
                            compressQuality: imageExportSettings.imageQuality
                        });
                        DEV_LOG && console.log('importFromPdf done ', pdfPath, pdfImages, Date.now() - start, 'ms');
                        resolve(pdfImages);
                    } catch (error) {
                        reject(error);
                    }
                })
        );

        // now we process all image files
        // We do it in batch of 5 to prevent memory issues
        items = await doInBatch(
            images.concat(pdfImages.flat()),
            (sourceImagePath: string) =>
                new Promise<ImportImageData>(async (resolve, reject) => {
                    try {
                        const start = Date.now();
                        DEV_LOG && console.log('importFromImage', sourceImagePath);
                        const imageSize = await getImageSize(sourceImagePath);
                        DEV_LOG && console.log('importFromImage', sourceImagePath, JSON.stringify(imageSize), Date.now() - start, 'ms');

                        const imageRotation = imageSize.rotation;
                        // TODO: detect JSON and QRCode in one go
                        DEV_LOG && console.log('[importAndScanImageFromUris] getJSONDocumentCornersFromFile', sourceImagePath, resizeThreshold);
                        const quads = cropEnabled ? await getJSONDocumentCornersFromFile(sourceImagePath, { resizeThreshold, areaScaleMinFactor }) : undefined;
                        let qrcode;
                        if (CARD_APP) {
                            // try to get the qrcode to show it in the import screen
                            qrcode = await detectQRCodeFromFile(sourceImagePath, { resizeThreshold: QRCODE_RESIZE_THRESHOLD });
                        }
                        if (cropEnabled && quads.length === 0) {
                            let { width } = imageSize;
                            let { height } = imageSize;
                            if (imageRotation % 180 !== 0) {
                                width = imageSize.height;
                                height = imageSize.width;
                            }
                            quads.push([
                                [noDetectionMargin, noDetectionMargin],
                                [width - noDetectionMargin, noDetectionMargin],
                                [width - noDetectionMargin, height - noDetectionMargin],
                                [noDetectionMargin, height - noDetectionMargin]
                            ]);
                        }
                        DEV_LOG && console.log('importFromImage done', sourceImagePath, Date.now() - start, 'ms');
                        resolve({ quads, imagePath: sourceImagePath, qrcode, imageWidth: imageSize.width, imageHeight: imageSize.height, imageRotation, undos: [], redos: [] });
                    } catch (error) {
                        reject(error);
                    }
                })
        );

        DEV_LOG &&
            console.log(
                'items',
                items.map((i) => i.imagePath)
            );
        if (items?.length) {
            if (cropEnabled) {
                const ModalImportImage = (await import('~/components/ModalImportImages.svelte')).default;
                const newItems: ImportImageData[] = await showModal({
                    page: ModalImportImage,
                    animated: true,
                    fullscreen: true,
                    props: {
                        items
                    }
                });
                if (!newItems) {
                    return;
                }
            }
            if (items) {
                const transforms = ApplicationSettings.getString(SETTINGS_DEFAULT_TRANSFORM, DEFAULT_TRANSFORM);
                const colorType = ApplicationSettings.getString(SETTINGS_DEFAULT_COLORTYPE, DEFAULT_COLORTYPE) as MatricesTypes;
                const contrast = ApplicationSettings.getNumber(SETTINGS_DEFAULT_CONTRAST, DEFAULT_CONTRAST);
                const brightness = ApplicationSettings.getNumber(SETTINGS_DEFAULT_BRIGHTNESS, DEFAULT_BRIGHTNESS);
                let colorMatrix: Matrix;
                try {
                    colorMatrix = JSON.parse(ApplicationSettings.getString(SETTINGS_DEFAULT_COLORMATRIX, DEFAULT_COLORMATRIX));
                } catch (error) {
                    colorMatrix = null;
                    ApplicationSettings.remove(SETTINGS_DEFAULT_COLORMATRIX);
                }
                DEV_LOG &&
                    console.log(
                        'items after crop',
                        items.map((i) => i.imagePath)
                    );
                pagesToAdd = (
                    await doInBatch(
                        items,
                        (item: ImportImageData, index) =>
                            new Promise<PageData[]>(async (resolve, reject) => {
                                try {
                                    const start = Date.now();
                                    DEV_LOG && console.log('about to cropDocument', index, JSON.stringify(item));
                                    const images: CropResult[] = [];
                                    if (item.quads) {
                                        const compressFormat = item.imagePath.toLowerCase().endsWith('.png') ? 'png' : imageExportSettings.imageFormat;
                                        images.push(
                                            ...(await cropDocumentFromFile(item.imagePath, item.quads, {
                                                // rotation: item.imageRotation,
                                                transforms,
                                                fileName: `cropedBitmap_${index}.${compressFormat}`,
                                                saveInFolder: knownFolders.temp().path,
                                                compressFormat,
                                                compressQuality: imageExportSettings.imageQuality
                                            }))
                                        );
                                        // we generate
                                    } else {
                                        images.push({ imagePath: item.imagePath, width: item.imageWidth, height: item.imageHeight });
                                    }

                                    const result = [];
                                    DEV_LOG &&
                                        console.log(
                                            'images',
                                            images.map((i) => i.imagePath)
                                        );
                                    if (images?.length) {
                                        for (let index = 0; index < images.length; index++) {
                                            const image = images[index];
                                            let qrcode;
                                            let colors;
                                            if (CARD_APP) {
                                                [qrcode, colors] = await processFromFile(
                                                    image.imagePath,
                                                    [
                                                        {
                                                            type: 'qrcode'
                                                        },
                                                        {
                                                            type: 'palette',
                                                            shrunkImageHeight: COLOR_PALETTE_RESIZE_THRESHOLD,
                                                            colorsFilterDistanceThreshold: 20,
                                                            nbColors: 5,
                                                            colorPalette: 2
                                                        }
                                                    ],
                                                    {
                                                        maxSize: QRCODE_RESIZE_THRESHOLD
                                                    }
                                                );
                                                DEV_LOG && console.log('qrcode and colors', qrcode, colors);
                                            }
                                            result.push({
                                                ...image,
                                                transforms,
                                                colorMatrix,
                                                colorType,
                                                contrast,
                                                brightness,
                                                crop: item.quads?.[index] || [
                                                    [0, 0],
                                                    [item.imageWidth - 0, 0],
                                                    [item.imageWidth - 0, item.imageHeight - 0],
                                                    [0, item.imageHeight - 0]
                                                ],
                                                sourceImagePath: item.imagePath,
                                                sourceImageWidth: item.imageWidth,
                                                sourceImageHeight: item.imageHeight,
                                                sourceImageRotation: item.imageRotation,
                                                // rotation: item.imageRotation,
                                                ...(CARD_APP
                                                    ? {
                                                          qrcode,
                                                          colors,
                                                          extra: {
                                                              color: colors.length > 1 ? colors[0] : new Color(colors[0]).getBrightness() < 145 ? '#ffffff' : '#000000'
                                                          }
                                                      }
                                                    : {})
                                            } as PageData);
                                        }
                                    }
                                    DEV_LOG && console.log('cropAndOtherDone', index, Date.now() - start, 'ms');
                                    resolve(result);
                                } catch (error) {
                                    reject(error);
                                }
                            })
                    )
                ).flat();
                DEV_LOG && console.log('pagesToAdd', document?.id, JSON.stringify(pagesToAdd));
                if (pagesToAdd.length) {
                    const nbPagesBefore = document?.pages.length ?? 0;
                    if (document) {
                        await document.addPages(pagesToAdd);
                        await document.save({}, true);
                        showSnack({ message: lc('imported_nb_pages', pagesToAdd.length) });
                    } else {
                        document = await OCRDocument.createDocument(pagesToAdd, folder);
                    }

                    await goToDocumentAfterScan(document, nbPagesBefore, canGoToView);
                    return document;
                }
            }
        }
        showSnack({ message: lc('no_document_found') });
    } catch (error) {
        throw error;
    } finally {
        hideLoading();
    }
}
export async function importAndScanImage({
    canGoToView = true,
    document,
    folder,
    forceGalleryPick = false,
    importPDFs = false
}: {
    document?: OCRDocument;
    importPDFs?: boolean;
    canGoToView?: boolean;
    forceGalleryPick?: boolean;
    folder?: DocFolder;
}) {
    await request(__IOS__ ? { storage: {}, photo: {} } : { storage: {} });
    try {
        if (__ANDROID__) {
            // on android a background event will trigger while picking a file
            securityService.ignoreNextValidation();
        }
        let selection: string[];
        if ((__IOS__ && !importPDFs) || forceGalleryPick) {
            try {
                const data = await createImagePicker({
                    mediaType: 1,
                    mode: 'multiple' // use "multiple" for multiple selection
                }).present();
                selection = data.map((d) => d.path);
            } catch (error) {
                selection = null;
            }
        } else {
            selection = (
                await openFilePicker({
                    // we need */* or we cant pick up .espass as it is not a known andorid mimetype
                    mimeTypes: ['image/*', 'application/pdf'].concat(CARD_APP ? ['application/vnd.apple.pkpass', 'application/vnd.espass-espass', '*/*'] : []),
                    documentTypes: __IOS__
                        ? [UTTypeImage.identifier, UTTypePDF.identifier].concat(
                              CARD_APP ? [UTType.typeWithFilenameExtension('pkpass').identifier, UTType.typeWithFilenameExtension('espass').identifier] : []
                          )
                        : undefined,
                    multipleSelection: true,
                    pickerMode: 0,
                    forceSAF: true
                })
            )?.files // not sure why we need to add file:// to pdf files on android < 12 but we get an error otherwise
                .map((s) => (__ANDROID__ && !s.startsWith('file://') && !s.startsWith(ANDROID_CONTENT) && s.endsWith(PDF_EXT) ? 'file://' + s : s));
        }

        if (selection?.length > 0) {
            return await importAndScanImageOrPdfFromUris({ uris: selection, document, canGoToView, folder });
        }
    } catch (error) {
        throw error;
    } finally {
        hideLoading();
    }
}

export async function showConfirmOptionSelect<T>(props?: ComponentProps<OptionSelect__SvelteComponent_>, options?: Partial<ConfirmOptions & MDCAlertControlerOptions>) {
    const component = (await import('@shared/components/OptionSelect.svelte')).default;
    let componentInstanceInfo: ComponentInstanceInfo<GridLayout, OptionSelect__SvelteComponent_>;
    try {
        componentInstanceInfo = resolveComponentElement(component, {
            onClose: (result) => {
                view.bindingContext.closeCallback(result);
            },
            // onCheckBox(item, value, e) {
            // view.bindingContext.closeCallback(item);
            // },
            trackingScrollView: 'collectionView',
            ...props
        }) as ComponentInstanceInfo<GridLayout, OptionSelect__SvelteComponent_>;
        const view: View = componentInstanceInfo.element.nativeView;
        const result = await confirm({
            view,
            okButtonText: lc('ok'),
            cancelButtonText: lc('cancel'),
            ...(options ? options : {})
        });
        return { confirmed: result, data: componentInstanceInfo.viewInstance.options.find((opt: T) => opt['value'] === true).data as T };
    } catch (err) {
        throw err;
    } finally {
        componentInstanceInfo.element.nativeElement._tearDownUI();
        componentInstanceInfo.viewInstance.$destroy();
        componentInstanceInfo = null;
    }
}

export async function showPopoverMenu<T = any>({
    anchor,
    closeOnClose = true,
    horizPos,
    onClose,
    options,
    props,
    title,
    vertPos
}: { options; anchor; onClose?; props?; title?: string; closeOnClose? } & Partial<PopoverOptions>) {
    const { colorSurfaceContainer } = get(colors);
    const OptionSelect = (await import('@shared/components/OptionSelect.svelte')).default;
    const rowHeight = (props?.rowHeight || 58) * get(fontScale);
    const maxHeight = Screen.mainScreen.heightDIPs - 50;
    const result: T = await showPopover({
        backgroundColor: colorSurfaceContainer,
        view: OptionSelect,
        anchor,
        horizPos: horizPos ?? HorizontalPosition.ALIGN_LEFT,
        vertPos: vertPos ?? VerticalPosition.CENTER,
        props: {
            title,
            borderRadius: 10,
            elevation: __ANDROID__ ? 3 : 0,
            margin: 4,
            fontWeight: 500,
            backgroundColor: colorSurfaceContainer,
            containerColumns: 'auto',
            rowHeight: !!props?.autoSizeListItem ? null : rowHeight,
            height: Math.min(rowHeight * options.length + (title ? 50 : 0), props?.maxHeight || maxHeight),
            width: 200 * get(fontScale),
            options,
            onClose: async (item) => {
                if (closeOnClose) {
                    if (__IOS__) {
                        // on iOS we need to wait or if onClose shows an alert dialog it wont work
                        await closePopover();
                    } else {
                        closePopover();
                    }
                }
                try {
                    await onClose?.(item);
                } catch (error) {
                    showError(error);
                } finally {
                    hideLoading();
                }
            },
            ...(props || {})
        }
    });
    return result;
}

export async function showSettings(props?) {
    try {
        const Settings = (await import('~/components/settings/Settings.svelte')).default;
        navigate({
            page: Settings,
            props
        });
    } catch (error) {
        showError(error);
    }
}

export async function showPDFPopoverMenu({
    anchor,
    document,
    documents,
    pages,
    popoverOptions
}: {
    pages: { page: OCRPage; document: OCRDocument }[];
    document?: OCRDocument;
    documents: OCRDocument[];
    anchor?;
    popoverOptions?: Partial<PopoverOptions>;
}) {
    let exportDirectory: string;

    // on IOS we dont persist the export folder
    // if (__IOS__) {
    //     const bookmark = NSUserDefaults.standardUserDefaults.objectForKey('pdf_export_directory');
    //     const stale = new interop.Reference(false);
    //     const resolvedURL = NSURL.URLByResolvingBookmarkDataOptionsRelativeToURLBookmarkDataIsStaleError(bookmark, NSURLBookmarkResolutionOptions.WithSecurityScope, null, stale);
    //     exportDirectory =
    // } else {
    exportDirectory = ApplicationSettings.getString('pdf_export_directory', DEFAULT_EXPORT_DIRECTORY);
    // }
    let exportDirectoryName = exportDirectory;
    function updateDirectoryName() {
        exportDirectoryName = exportDirectory ? getDirectoryName(exportDirectory) : lc('please_choose_export_folder');
    }
    updateDirectoryName();

    async function pickExportFolder() {
        const result = await pickFolder({
            multipleSelection: false,
            permissions: { write: true, persistable: true, read: true },
            forceSAF: true
        });
        if (result?.folders?.length) {
            exportDirectory = result.folders[0];
            DEV_LOG && console.log('set_export_directory', exportDirectory);
            // ApplicationSettings.setString('pdf_export_directory', exportDirectory);
            if (__IOS__) {
                const bookmark = NSURL.fileURLWithPathIsDirectory(result.folders[0], true).bookmarkDataWithOptionsIncludingResourceValuesForKeysRelativeToURLError(
                    NSURLBookmarkCreationOptions.WithSecurityScope,
                    null,
                    null
                );
                NSUserDefaults.standardUserDefaults.setObjectForKey(bookmark, 'pdf_export_directory');
            } else {
                ApplicationSettings.setString('pdf_export_directory', exportDirectory);
            }
            updateDirectoryName();
            return true;
        }
        return false;
    }

    const options = new ObservableArray(
        (__ANDROID__ ? [{ id: 'set_export_directory', name: lc('export_folder'), subtitle: exportDirectoryName, rightIcon: 'mdi-restore' }] : [])
            .concat([
                { id: 'settings', name: lc('pdf_export_settings'), icon: 'mdi-cog' },
                { id: 'open', name: lc('open'), icon: 'mdi-eye' },
                { id: 'share', name: lc('share'), icon: 'mdi-share-variant' },
                { id: 'export', name: lc('export'), icon: 'mdi-export' },
                ...(documents.length > 1 ? [{ id: 'bulk_export', name: lc('bulk_export'), icon: 'mdi-file-document-multiple-outline' }] : [])
            ] as any)
            .concat(__ANDROID__ ? ([{ id: 'print', name: lc('print'), icon: 'mdi-printer' }] as any) : [])
            .concat([{ id: 'preview', name: lc('preview'), icon: 'mdi-printer-eye' }] as any)
    );
    return showPopoverMenu({
        title: lc('pdf_export'),
        options,
        anchor,
        vertPos: VerticalPosition.BELOW,
        props: {
            width: 250,
            // rows: 'auto',
            // rowHeight: null,
            // height: null,
            // autoSizeListItem: true,
            onRightIconTap: (item, event) => {
                try {
                    switch (item.id) {
                        case 'set_export_directory': {
                            ApplicationSettings.remove('pdf_export_directory');
                            exportDirectory = DEFAULT_EXPORT_DIRECTORY;
                            updateDirectoryName();
                            const item = options.getItem(0);
                            item.subtitle = exportDirectoryName;
                            options.setItem(0, item);
                            break;
                        }
                    }
                } catch (error) {
                    showError(error);
                }
            }
        },

        closeOnClose: false,
        onClose: async (item) => {
            try {
                DEV_LOG && console.log('showPDFPopoverMenu', 'action', item.id);
                switch (item.id) {
                    case 'settings': {
                        closePopover();
                        showSettings({
                            subSettingsOptions: 'pdf_export'
                        });
                        break;
                    }
                    case 'set_export_directory': {
                        if (await pickExportFolder()) {
                            const item = options.getItem(0);
                            item.subtitle = exportDirectoryName;
                            options.setItem(0, item);
                        }
                        break;
                    }
                    case 'print': {
                        await closePopover();
                        await showLoading(lc('exporting'));
                        const filePath = await exportPDFAsync({ pages, document });
                        hideLoading();
                        if (filePath) {
                            DEV_LOG && console.log('print pdf', filePath);
                            printPDF(filePath, document?.name || 'PDF');
                        }
                        break;
                    }
                    case 'open': {
                        await closePopover();
                        await showLoading(lc('exporting'));
                        const filePath = await exportPDFAsync({ pages, document });
                        hideLoading();
                        DEV_LOG && console.log('opening pdf', filePath);
                        if (filePath) {
                            openFile(filePath);
                        } else {
                            throw new SilentError(lc('no_pages_in_pdf'));
                        }
                        break;
                    }
                    case 'share': {
                        await closePopover();
                        await showLoading(lc('exporting'));
                        const filePath = await exportPDFAsync({ pages, document });
                        hideLoading();
                        DEV_LOG && console.log('sharing pdf', filePath);
                        share({ file: filePath }, { mimetype: 'application/pdf' });
                        break;
                    }
                    case 'export': {
                        if (!exportDirectory) {
                            if (await pickExportFolder()) {
                                const item = options.getItem(0);
                                item.subtitle = exportDirectoryName;
                                options.setItem(0, item);
                            } else {
                                showSnack({ message: lc('please_choose_export_folder') });
                                return;
                            }
                        }
                        await closePopover();
                        const component = (await import('~/components/common/ExportPDFAlertOptions.svelte')).default;
                        let componentInstanceInfo: ComponentInstanceInfo<GridLayout, ExportPDFAlertOptions__SvelteComponent_>;
                        try {
                            const defaultOptions = getPDFDefaultExportOptions();
                            DEV_LOG && console.log('defaultOptions', JSON.stringify(defaultOptions));
                            componentInstanceInfo = resolveComponentElement(component, {
                                filename: getFileNameForDocument(document) + PDF_EXT,
                                jpegQuality: defaultOptions.jpegQuality,
                                password: defaultOptions.password,
                                folder: exportDirectory
                            }) as ComponentInstanceInfo<GridLayout, ExportPDFAlertOptions__SvelteComponent_>;
                            const result = await confirm({
                                okButtonText: lc('ok'),
                                cancelButtonText: lc('cancel'),
                                view: componentInstanceInfo.element.nativeView
                            });
                            if (result) {
                                let filename = componentInstanceInfo.viewInstance.filename;
                                if (filename.length) {
                                    const password = componentInstanceInfo.viewInstance.password;
                                    const jpegQuality = componentInstanceInfo.viewInstance.jpegQuality;
                                    const folder = componentInstanceInfo.viewInstance.folder;
                                    showLoading(lc('exporting'));
                                    DEV_LOG && console.log('exportPDF', folder, filename, jpegQuality, !!password);
                                    const filePath = await exportPDFAsync({
                                        pages,
                                        document,
                                        folder,
                                        filename,
                                        options: {
                                            jpegQuality,
                                            password
                                        }
                                    });
                                    hideLoading();
                                    if (!filePath) {
                                        return;
                                    }
                                    DEV_LOG && console.log('exportPDF done', filePath, File.exists(filePath));
                                    if (__ANDROID__ && filePath.startsWith(ANDROID_CONTENT)) {
                                        filename = com.nativescript.documentpicker.FilePath.getPath(Utils.android.getApplicationContext(), android.net.Uri.parse(filePath))?.split(SEPARATOR).pop();
                                    } else {
                                        filename = filePath.split(SEPARATOR).pop();
                                    }
                                    const onSnack = await showSnack({ message: lc('pdf_saved', filename || filePath), actionText: lc('open') });
                                    if (onSnack?.reason === 'action') {
                                        DEV_LOG && console.log('openFile', filePath);
                                        openFile(filePath);
                                    }
                                }
                            }
                        } catch (err) {
                            throw err;
                        } finally {
                            componentInstanceInfo.element.nativeElement._tearDownUI();
                            componentInstanceInfo.viewInstance.$destroy();
                            componentInstanceInfo = null;
                        }
                        break;
                    }
                    case 'bulk_export': {
                        if (!exportDirectory) {
                            if (await pickExportFolder()) {
                                const item = options.getItem(0);
                                item.subtitle = exportDirectoryName;
                                options.setItem(0, item);
                            } else {
                                showSnack({ message: lc('please_choose_export_folder') });
                                return;
                            }
                        }
                        await closePopover();
                        const component = (await import('~/components/common/ExportPDFAlertOptions.svelte')).default;
                        let componentInstanceInfo: ComponentInstanceInfo<GridLayout, ExportPDFAlertOptions__SvelteComponent_>;
                        try {
                            const defaultOptions = getPDFDefaultExportOptions();
                            componentInstanceInfo = resolveComponentElement(component, {
                                jpegQuality: defaultOptions.jpegQuality,
                                password: defaultOptions.password,
                                folder: exportDirectory,
                                showFilename: false
                            }) as ComponentInstanceInfo<GridLayout, ExportPDFAlertOptions__SvelteComponent_>;
                            const result = await confirm({
                                okButtonText: lc('ok'),
                                cancelButtonText: lc('cancel'),
                                view: componentInstanceInfo.element.nativeView
                            });
                            if (result) {
                                showLoading(lc('exporting'));
                                const password = componentInstanceInfo.viewInstance.password;
                                const jpegQuality = componentInstanceInfo.viewInstance.jpegQuality;
                                const folder = componentInstanceInfo.viewInstance.folder;
                                DEV_LOG &&
                                    console.log(
                                        'bulk export',
                                        documents.length,
                                        documents.map((d) => d.name)
                                    );
                                // we do it in sequence as it mixes documents in parallel on Android. Could be a itextpdf issue
                                for (let index = 0; index < documents.length; index++) {
                                    const doc = documents[index];
                                    const filename = getFileNameForDocument(doc) + PDF_EXT;
                                    DEV_LOG && console.log('exportPDF', folder, filename, jpegQuality, password);
                                    await exportPDFAsync({
                                        pages: doc.pages.map((page) => ({ document: doc, page })),
                                        document: doc,
                                        folder,
                                        filename,
                                        options: {
                                            jpegQuality,
                                            password
                                        }
                                    });
                                }

                                hideLoading();

                                DEV_LOG && console.log('bulk exportPDF done');

                                await showSnack({ message: lc('pdfs_saved') });
                            }
                        } catch (err) {
                            throw err;
                        } finally {
                            componentInstanceInfo.element.nativeElement._tearDownUI();
                            componentInstanceInfo.viewInstance.$destroy();
                            componentInstanceInfo = null;
                        }
                        break;
                    }
                    case 'preview':
                        await closePopover();
                        const component = (await import('~/components/pdf/PDFPreviewModal.svelte')).default;
                        await showModal({
                            page: component,
                            animated: true,
                            fullscreen: true,
                            props: {
                                pages,
                                document
                            }
                        });
                        break;
                }
            } catch (error) {
                showError(error);
            } finally {
                hideLoading();
            }
        },
        ...(popoverOptions ?? {})
    });
}

async function exportImages(pages: { page: OCRPage; document: OCRDocument }[], exportDirectory: string, toGallery = false) {
    const sortedPages = pages.filter((p) => p.page).sort((a, b) => a.page.createdDate - b.page.createdDate);
    const imagePaths = sortedPages.map((p) => p.page.imagePath);

    const imageExportSettings = getImageExportSettings();
    const canSetName = !toGallery && imagePaths.length === 1;
    let outputImageNames = [];
    if (canSetName) {
        const result = await prompt({
            okButtonText: lc('ok'),
            cancelButtonText: lc('cancel'),
            defaultText: getFileNameForDocument() + '.' + imageExportSettings.imageFormat,
            hintText: lc('image_filename'),
            view: createView(Label, {
                padding: '10 20 0 20',
                textWrap: true,
                color: get(colors).colorOnSurfaceVariant as any,
                html: lc('set_filename_format_settings')
            })
        });
        if (!result?.result || !result?.text?.length) {
            return;
        }
        outputImageNames.push(result.text);
    } else {
        outputImageNames = sortedPages.map((page) => getFormatedDateForFilename(page.page.createdDate));
        // find duplicates and rename if any
        let lastName;
        let renameDelta = 1;
        for (let index = 0; index < outputImageNames.length; index++) {
            const name = outputImageNames[index];
            if (name === lastName) {
                outputImageNames[index] = name + '_' + (renameDelta++ + '').padStart(3, '0');
                // we dont reset lastName so that we compare to the first one found
            } else {
                lastName = name;
                renameDelta = 1;
            }
        }
    }
    DEV_LOG && console.log('exporting images', imageExportSettings.imageFormat, imageExportSettings.imageQuality, exportDirectory, sortedPages.length, outputImageNames);
    showLoading(lc('exporting'));
    // const destinationPaths = [];
    let finalMessagePart;
    if (toGallery) {
        if (__ANDROID__) {
            await requestStoragePermission();
        } else if (__IOS__) {
            await requestPhotoPermission();
        }
    }
    await doInBatch(
        sortedPages,
        (data: { page: OCRPage; document: OCRDocument }, index) =>
            new Promise<void>(async (resolve, reject) => {
                let imageSource: ImageSource;
                try {
                    const fileName = outputImageNames[index];
                    let destinationName = fileName;
                    if (!destinationName.endsWith(imageExportSettings.imageFormat)) {
                        destinationName += '.' + imageExportSettings.imageFormat;
                    }
                    // const imageSource = await ImageSource.fromFile(imagePath);
                    // DEV_LOG && console.warn('exporting image', index, data.page.imagePath);
                    imageSource = await getTransformedImage({ ...data, defaultBackgroundColor: get(colors).colorPrimary });
                    // DEV_LOG && console.info('exporting image done', index, data.page.imagePath, imageSource);
                    if (imageSource) {
                        finalMessagePart = await saveImage(imageSource, {
                            exportDirectory,
                            fileName,
                            toGallery,
                            ...imageExportSettings,
                            reportName: canSetName
                        });
                    }
                    // DEV_LOG && console.info('exporting image saved', index, data.page.imagePath, imageSource);
                    resolve();
                } catch (error) {
                    if (/error creating file/.test(error.toString())) {
                        reject(new SilentError(lc('please_choose_export_folder_again')));
                    } else {
                        reject(error);
                    }
                } finally {
                    recycleImages(imageSource);
                }
            }),
        ApplicationSettings.getNumber(SETTINGS_TRANSFORM_BATCH_SIZE, TRANSFORM_BATCH_SIZE)
    );
    if (outputImageNames.length === 1) {
        showSnack({ message: lc('image_saved', finalMessagePart) });
    } else {
        showSnack({ message: lc('images_saved', finalMessagePart) });
    }
}

export function getDirectoryName(folderPath: string) {
    const exportDirectoryName = folderPath;
    if (__ANDROID__ && folderPath.startsWith(ANDROID_CONTENT)) {
        const context = Utils.android.getApplicationContext();
        const outdocument = androidx.documentfile.provider.DocumentFile.fromTreeUri(context, android.net.Uri.parse(folderPath));
        return outdocument.getName() + '@' + folderPath.slice(ANDROID_CONTENT.length).split('/')[0];
        // exportDirectoryName = outdocument.getName() || com.nativescript.documentpicker.FilePath.getPath(Utils.android.getApplicationContext(), outdocument.getUri()) || exportDirectoryName;
        // exportDirectoryName = exportDirectory?.split(/(\/|%3A)/).pop() || lc('');
    }
    return exportDirectoryName
        .split(SEPARATOR)
        .filter((s) => s.length)
        .pop();
}

async function exportPkPassToTemp(document: OCRDocument, page: OCRPage, targetFormat?: PKPassType) {
    if (CARD_APP) {
        const format = targetFormat ?? getStoredPassFormat(page.pkpass);
        const passExt = format === PKPassType.ESpass ? ESPASS_EXT : PKPASS_EXT;
        const fileName = page.name ?? document.name ?? `${document.id}_${page.pkpass_id}`;
        const actualFileName = `${cleanFilename(fileName)}${passExt}`;
        const outputZipPath = path.join(knownFolders.temp().path, actualFileName);

        const docFolder = document.folderPath;
        const pageFolder = docFolder.getFolder(page.id);
        const pkpassFolderPath = pageFolder.getFolder('pkpass').path;

        if (format !== getStoredPassFormat(page.pkpass)) {
            await buildPassArchive(pkpassFolderPath, page.pkpass, format, outputZipPath, lang);
        } else {
            await zip({ directory: pkpassFolderPath, archive: outputZipPath, keepParent: false });
        }
        return outputZipPath;
    }
}

/**
 * Determine the pass export format from a menu action id.
 * Returns null for legacy ids (e.g. 'export_pkpass') so that callers can
 * fall back to the pass's stored format.
 */
function getTargetFormatFromActionId(id: string): PKPassType | null {
    if (CARD_APP) {
        if (id === 'export_as_espass' || id === 'share_as_espass') return PKPassType.ESpass;
        if (id === 'export_as_pkpass' || id === 'share_as_pkpass') return PKPassType.PKPass;
    }
    return null; // legacy / native-format action
}

export async function showImagePopoverMenu(pages: { page: OCRPage; document: OCRDocument }[], anchor, popoverOptions?: Partial<PopoverOptions>) {
    let exportDirectory = ApplicationSettings.getString('image_export_directory', DEFAULT_EXPORT_DIRECTORY);
    let exportDirectoryName = exportDirectory;
    DEV_LOG && console.log('showImagePopoverMenu', exportDirectoryName, pages.length);
    function updateDirectoryName() {
        exportDirectoryName = exportDirectory ? getDirectoryName(exportDirectory) : lc('please_choose_export_folder');
    }
    updateDirectoryName();
    async function pickExportFolder() {
        const result = await pickFolder({
            multipleSelection: false,
            permissions: { write: true, persistable: true, read: true },
            forceSAF: true
        });
        if (result.folders?.[0]) {
            exportDirectory = result.folders[0];
            DEV_LOG && console.log('set_export_directory', exportDirectory);
            if (__IOS__) {
                const bookmark = NSURL.fileURLWithPathIsDirectory(exportDirectory, true).bookmarkDataWithOptionsIncludingResourceValuesForKeysRelativeToURLError(
                    NSURLBookmarkCreationOptions.WithSecurityScope,
                    null,
                    null
                );
                NSUserDefaults.standardUserDefaults.setObjectForKey(bookmark, 'image_export_directory');
            } else {
                ApplicationSettings.setString('image_export_directory', exportDirectory);
            }
            updateDirectoryName();
            return true;
        }
        return false;
    }

    const options = new ObservableArray(
        (__ANDROID__ ? [{ id: 'set_export_directory', name: lc('export_folder'), subtitle: exportDirectoryName, rightIcon: 'mdi-restore' }] : []).concat([
            { id: 'export', name: lc('export'), icon: 'mdi-export' },
            { id: 'save_gallery', name: lc('save_gallery'), icon: 'mdi-image-multiple' },
            { id: 'share', name: lc('share'), icon: 'mdi-share-variant' }
        ] as any)
    );
    if (CARD_APP && pages.some((p) => p.page.pkpass)) {
        options.splice(options.length - 2, 0, {
            id: 'export_as_pkpass',
            name: lc('export_as_pkpass'),
            icon: 'mdi-export'
        } as any);
        options.splice(options.length - 2, 0, {
            id: 'export_as_espass',
            name: lc('export_as_espass'),
            icon: 'mdi-export'
        } as any);
        options.push({
            id: 'share_as_pkpass',
            name: lc('share_as_pkpass'),
            icon: 'mdi-share-variant'
        } as any);
        options.push({
            id: 'share_as_espass',
            name: lc('share_as_espass'),
            icon: 'mdi-share-variant'
        } as any);
    }
    return new Promise<boolean>((resolve, reject) => {
        showPopoverMenu({
            title: lc('image_export'),
            options,
            anchor,
            vertPos: VerticalPosition.BELOW,
            props: {
                width: 250,
                // rows: 'auto',
                // rowHeight: null,
                // height: null,
                // autoSizeListItem: true,
                onRightIconTap: (item, event) => {
                    try {
                        switch (item.id) {
                            case 'set_export_directory': {
                                ApplicationSettings.remove('image_export_directory');
                                exportDirectory = DEFAULT_EXPORT_DIRECTORY;
                                updateDirectoryName();
                                const item = options.getItem(0);
                                item.subtitle = exportDirectoryName;
                                options.setItem(0, item);
                                break;
                            }
                        }
                    } catch (error) {
                        reject(error);
                    }
                }
            },

            closeOnClose: false,
            onClose: async (item) => {
                DEV_LOG && console.log('showImagePopoverMenu action', item.id);
                try {
                    let didDoSomething = false;
                    switch (item.id) {
                        case 'set_export_directory': {
                            if (await pickExportFolder()) {
                                const item = options.getItem(0);
                                item.subtitle = exportDirectoryName;
                                options.setItem(0, item);
                                didDoSomething = true;
                            }
                            break;
                        }
                        case 'share':
                            await closePopover();
                            const images = [];
                            const files = [];
                            try {
                                await doInBatch(
                                    pages,
                                    async (
                                        p: {
                                            page: OCRPage;
                                            document: OCRDocument;
                                        },
                                        index
                                    ) => {
                                        const page = p.page;
                                        if (page.colorMatrix || page.rotation !== 0 || page.brightness !== DEFAULT_BRIGHTNESS || page.contrast !== DEFAULT_CONTRAST) {
                                            const imageSource = await getTransformedImage({ page, defaultBackgroundColor: get(colors).colorPrimary });
                                            if (imageSource) {
                                                images.push(imageSource);
                                            }
                                        } else {
                                            files.push(page.imagePath);
                                        }
                                    }
                                );
                                // for (let index = 0; index < pages.length; index++) {
                                //     const page = pages[index];
                                // }
                                await share({ images, files });
                                didDoSomething = true;
                            } catch (error) {
                                DEV_LOG && console.log('error while sharing images', error);
                                throw error;
                            } finally {
                                recycleImages(images);
                            }
                            break;
                        case 'share_as_pkpass':
                        case 'share_as_espass':
                        case 'share_pkpass': {
                            if (CARD_APP) {
                                await closePopover();
                                const targetFormat = getTargetFormatFromActionId(item.id);
                                const files = [];
                                await doInBatch(
                                    pages.filter((p) => p.page.pkpass),
                                    async (
                                        p: {
                                            page: OCRPage;
                                            document: OCRDocument;
                                        },
                                        index
                                    ) => {
                                        const outputZipPath = await exportPkPassToTemp(p.document, p.page, targetFormat);

                                        files.push(outputZipPath);
                                    }
                                );
                                await share({ files });
                                didDoSomething = true;
                            }
                            break;
                        }
                        case 'export_as_pkpass':
                        case 'export_as_espass':
                        case 'export_pkpass': {
                            if (CARD_APP) {
                                DEV_LOG && console.log('export_pkpass', exportDirectory);
                                if (!exportDirectory) {
                                    if (await pickExportFolder()) {
                                        const item = options.getItem(0);
                                        item.subtitle = exportDirectoryName;
                                        options.setItem(0, item);
                                    } else {
                                        showSnack({ message: lc('please_choose_export_folder') });
                                        return;
                                    }
                                }
                                await closePopover();
                                const targetFormat = getTargetFormatFromActionId(item.id);
                                await doInBatch(
                                    pages.filter((p) => p.page.pkpass),
                                    async (
                                        p: {
                                            page: OCRPage;
                                            document: OCRDocument;
                                        },
                                        index
                                    ) => {
                                        const needsCopy = __ANDROID__ && exportDirectory.startsWith(ANDROID_CONTENT);
                                        const format = targetFormat ?? getStoredPassFormat(p.page.pkpass);

                                        const outputZipPath = await exportPkPassToTemp(p.document, p.page, format);

                                        if (__ANDROID__ && needsCopy) {
                                            const mimeType = targetFormat === PKPassType.ESpass ? 'application/vnd.espass-espass' : 'application/vnd.apple.pkpass';
                                            const context = Utils.android.getApplicationContext();
                                            com.akylas.documentscanner.utils.FileUtils.Companion.copyFileToDocumentFile(context, outputZipPath, mimeType, exportDirectory);
                                        }
                                    }
                                );
                                didDoSomething = true;
                            }
                            showSnack({ message: lc('passbooks_saved') });
                            break;
                        }
                        case 'export':
                        case 'save_gallery': {
                            if (!exportDirectory && item.id !== 'save_gallery') {
                                if (await pickExportFolder()) {
                                    const item = options.getItem(0);
                                    item.subtitle = exportDirectoryName;
                                    options.setItem(0, item);
                                } else {
                                    showSnack({ message: lc('please_choose_export_folder') });
                                    return;
                                }
                            }
                            await closePopover();
                            await exportImages(pages, exportDirectory, item.id === 'save_gallery');
                            didDoSomething = true;
                            break;
                        }
                    }
                    resolve(didDoSomething);
                } catch (error) {
                    reject(error);
                } finally {
                    hideLoading();
                }
            },
            ...(popoverOptions ?? {})
        });
    });
}

export interface ShowSnackMessageOptions {
    text: string;
    progress?: number;
    translateY?: number;
}
let snackMessage: ComponentInstanceInfo<GridLayout, BottomSnack__SvelteComponent_>;
let snackMessageVisible = false;
function getSnackMessage(props?) {
    if (!snackMessage) {
        snackMessage = resolveComponentElement(BottomSnack, props || {}) as ComponentInstanceInfo<GridLayout, BottomSnack__SvelteComponent_>;
        try {
            (Application.getRootView() as GridLayout).addChild(snackMessage.element.nativeView);
        } catch (error) {
            console.error(error, error.stack);
        }
    }
    return snackMessage;
}
export function updateSnackMessage(msg: Partial<ShowSnackMessageOptions>) {
    if (snackMessage) {
        const snackMessage = getSnackMessage();
        const props = {
            progress: msg.progress
        };
        if (msg.text) {
            props['text'] = msg.text;
        }
        snackMessage.viewInstance.$set(props);
    }
}
export async function showSnackMessage(props: ShowSnackMessageOptions) {
    if (snackMessage) {
        updateSnackMessage(props);
    } else {
        snackMessageVisible = true;
        const snackMessage = getSnackMessage(props);
        const animationArgs = [
            {
                target: snackMessage.element.nativeView,
                translate: { x: 0, y: 0 },
                duration: 100
            }
        ];
        Application.notify({ eventName: 'snackMessageAnimation', animationArgs });
        await new Animation(animationArgs).play();
        updateSnackMessage({ translateY: 0 });
    }
}
export async function hideSnackMessage() {
    if (snackMessage && snackMessageVisible) {
        snackMessageVisible = false;
        const animationArgs: AnimationDefinition[] = [
            {
                target: snackMessage.element.nativeView,
                translate: { x: 0, y: 100 },
                duration: 100
            }
        ];
        Application.notify({ eventName: 'snackMessageAnimation', animationArgs });
        await new Animation(animationArgs).play();
        if (snackMessage) {
            (Application.getRootView() as GridLayout).removeChild(snackMessage.element.nativeView);
            snackMessage.element.nativeElement._tearDownUI();
            snackMessage.viewInstance?.$destroy();
            snackMessage = null;
        }
    }
}

export function localizedLanguage(key: string, languages: { [k: string]: string }) {
    const result = getLocaleDisplayName(key, true);
    return result?.length && result !== key ? result : languages[key];
}

export function checkOrDownloadOCRLanguages({
    dataType = ocrService.dataType,
    hideLoading: hideLoadingDialog = true,
    languages = ocrService.languagesArray,
    shouldConfirm = true,
    showAsSnackMessage = false
}: {
    dataType?: string;
    languages?: string[];
    hideLoading?: boolean;
    shouldConfirm?: boolean;
    showAsSnackMessage?: boolean;
}) {
    return ocrService.checkOrDownload({
        dataType,
        languages: languages.join('+'),
        onProgress: async (progress) => {
            if (progress === 100) {
                if (hideLoadingDialog) {
                    if (showAsSnackMessage) {
                        await hideSnackMessage();
                    } else {
                        await hideLoading();
                    }
                }
            } else {
                if (showAsSnackMessage) {
                    showSnackMessage({ text: l('downloading', progress), progress });
                } else {
                    showLoading({ text: l('downloading', progress), progress });
                }
            }
        },
        confirmDownload: (toDownload) =>
            shouldConfirm
                ? confirm({
                      message: lc('ocr_missing_languages_download', toDownload.map((l) => localizedLanguage(l, OCRLanguages)).join(',')),
                      okButtonText: lc('download'),
                      cancelButtonText: lc('cancel')
                  })
                : Promise.resolve(true)
    });
}

export async function detectOCROnPage(document: OCRDocument, index: number) {
    try {
        if (!(await checkOrDownloadOCRLanguages({}))) {
            return;
        }
        showLoading({ text: l('ocr_computing', 0), progress: 0 });
        const ocrData = await ocrService.ocrPage({
            document,
            pageIndex: index,
            onProgress: (progress: number) => {
                updateLoadingProgress({ progress, text: l('ocr_computing', progress) });
            }
        });
        return ocrData;
    } catch (err) {
        throw err;
    } finally {
        hideLoading();
        // recycleImages(ocrImage);
    }
}
interface PageTransformData {
    page: OCRPage;
    pageIndex: number;
    document: OCRDocument;
}
export async function transformPages({ documents, pages }: { documents?: OCRDocument[]; pages?: PageTransformData[] }) {
    try {
        const view = (await import('~/components/common/TransformPagesBottomSheet.svelte')).default;
        const updateOptions = await showBottomSheet({
            view,
            skipCollapsedState: true
        });
        if (updateOptions) {
            // await showLoading(lc('computing'));

            // we want to ocr the full document.
            const progress = 0;
            if (!pages && documents) {
                pages = [];
                documents.forEach((document) => {
                    pages.push(...document.pages.reduce((acc, page, pageIndex) => acc.concat([{ page, pageIndex, document }]), []));
                });
            }
            const totalPages = pages.length;
            let pagesDone = 0;
            showSnackMessage({
                text: lc('updating_pages', progress),
                progress: 0
            });
            const { transforms, ...otherUpdates } = updateOptions;
            await doInBatch(
                pages,
                async (p: PageTransformData, index) => {
                    await p.document.updatePageTransforms(p.pageIndex, transforms.join(TRANSFORMS_SPLIT), otherUpdates);

                    pagesDone += 1;
                    const progress = Math.round((pagesDone / totalPages) * 100);
                    updateSnackMessage({
                        text: lc('updating_pages', progress),
                        progress
                    });
                },
                ApplicationSettings.getNumber(SETTINGS_TRANSFORM_BATCH_SIZE, TRANSFORM_BATCH_SIZE)
            );
            return true;
        }
    } catch (error) {
        throw error;
    } finally {
        hideSnackMessage();
    }
}
export async function detectOCR({ documents, pages }: { documents?: OCRDocument[]; pages?: { page: OCRPage; pageIndex: number; document: OCRDocument }[] }) {
    try {
        const OCRSettingsBottomSheet = (await import('~/components/ocr/OCRSettingsBottomSheet.svelte')).default;
        const shouldStart = await showBottomSheet({
            view: OCRSettingsBottomSheet,
            skipCollapsedState: true,
            props: {}
        });
        if (shouldStart) {
            if (!(await checkOrDownloadOCRLanguages({ showAsSnackMessage: true }))) {
                return;
            }

            // we want to ocr the full document.
            const progress = 0;
            if (!pages && documents) {
                pages = [];
                documents.forEach((document) => {
                    pages.push(...document.pages.reduce((acc, page, pageIndex) => acc.concat([{ page, pageIndex, document }]), []));
                });
            }
            const totalPages = pages.length;
            let pagesDone = 0;
            showSnackMessage({
                text: lc('ocr_computing_document', progress),
                progress: 0
            });
            const runnningOcr: { [k: string]: number } = {};
            await Promise.all(
                pages.map(async (p, index) => {
                    const pageId = p.page.id;
                    runnningOcr[pageId] = 0;
                    await ocrService.ocrPage({
                        document: p.document,
                        pageIndex: p.pageIndex,
                        onProgress: (progress: number) => {
                            runnningOcr[pageId] = progress;
                            const totalProgress = Math.round((100 / totalPages) * pagesDone + Object.values(runnningOcr).reduce((a, b) => a + b) / totalPages);
                            updateSnackMessage({
                                text: lc('ocr_computing_document', totalProgress),
                                progress: totalProgress
                            });
                        }
                    });
                    delete runnningOcr[pageId];
                    pagesDone += 1;
                })
            );
            return true;
        }
    } catch (error) {
        throw error;
    } finally {
        hideSnackMessage();
    }
}

export function copyOCRToClipboard(text) {
    const replaceLineBreaks = ApplicationSettings.getBoolean(SETTINGS_OCR_COPY_USE_SPACE, DEFAULT_OCR_COPY_USE_SPACE);
    if (replaceLineBreaks) {
        text = text.replace(/\n/g, ' ');
    }
    copyTextToClipboard(text);
}

export function copyTextToClipboard(text) {
    DEV_LOG && console.log('copyTextToClipboard', text);
    copyToClipboard(text);
    if (__IOS__ || (__ANDROID__ && SDK_VERSION < 13)) {
        showToast(lc('copied'));
    }
}

export async function showSliderPopover({
    anchor,
    debounceDuration = 100,
    formatter,
    horizPos = HorizontalPosition.ALIGN_LEFT,
    icon,
    max = 100,
    min = 0,
    onChange,
    step = 1,
    title,
    value,
    vertPos = VerticalPosition.CENTER,
    width = 0.8 * screenWidthDips
}: {
    title?;
    debounceDuration?;
    icon?;
    min?;
    max?;
    step?;
    formatter?;
    horizPos?;
    anchor;
    vertPos?;
    width?;
    value?;
    onChange?;
}) {
    const component = (await import('~/components/common/SliderPopover.svelte')).default;
    const { colorSurfaceContainer } = get(colors);

    return showPopover({
        backgroundColor: colorSurfaceContainer,
        view: component,
        anchor,
        horizPos,
        vertPos,
        props: {
            title,
            icon,
            min,
            max,
            step,
            width,
            formatter,
            value,
            onChange: debounce(onChange, debounceDuration)
        }

        // trackingScrollView: 'collectionView'
    });
}
export async function showSlidersPopover({
    anchor,
    debounceDuration = 100,
    horizPos = HorizontalPosition.ALIGN_LEFT,
    items,
    vertPos = VerticalPosition.CENTER,
    width = 0.8 * screenWidthDips
}: {
    debounceDuration?;
    horizPos?;
    anchor;
    vertPos?;
    width?;
    items;
}) {
    const component = (await import('~/components/common/SlidersPopover.svelte')).default;
    const { colorSurfaceContainer } = get(colors);

    return showPopover({
        backgroundColor: colorSurfaceContainer,
        view: component,
        anchor,
        horizPos,
        vertPos,
        props: {
            width,
            items
        }

        // trackingScrollView: 'collectionView'
    });
}

export async function showMatrixLevelPopover({ anchor, currentValue, item, onChange }) {
    if (!item.range) {
        return;
    }

    return showSliderPopover({
        vertPos: VerticalPosition.ABOVE,
        min: 0.5,
        max: 2,
        step: 0.1,
        anchor,
        value: currentValue,
        onChange
    });
}
export async function goToDocumentView(doc: OCRDocument, useTransition = true) {
    // DEV_LOG && console.log('goToDocumentView');
    if (CARD_APP) {
        const page = (await import('~/components/view/CardView.svelte')).default;
        return navigate({
            page,
            props: {
                document: doc
            }
        });
    } else {
        const page = (await import('~/components/view/DocumentView.svelte')).default;
        return navigate({
            page,
            transition: __ANDROID__ && useTransition ? SharedTransition.custom(new PageTransition(300, null, 10)) : null,
            props: {
                document: doc
            }
        });
    }
}
export async function goToFolderView(folder: DocFolder, useTransition = true) {
    if (CARD_APP) {
        const page = (await import('~/components/list/CardsList.svelte')).default;
        return navigate({
            page,
            props: {
                folder
            }
        });
    } else {
        const page = (await import('~/components/list/DocumentsList.svelte')).default;
        return navigate({
            page,
            transition: __ANDROID__ && useTransition ? SharedTransition.custom(new PageTransition(300, null, 10)) : null,
            props: {
                folder
            }
        });
    }
}

export async function pickFolderColor(folder, event) {
    try {
        const ColorPickerView = (await import('~/components/common/ColorPickerView.svelte')).default;
        // const result: any = await showModal({ page: Settings, fullscreen: true, props: { position } });
        const anchorView = event.object as View;
        const color: string = await showPopover({
            backgroundColor: get(colors).colorSurfaceContainer,
            vertPos: VerticalPosition.BELOW,
            horizPos: HorizontalPosition.RIGHT,
            view: ColorPickerView,
            anchor: anchorView,
            props: {
                borderRadius: 10,
                elevation: __ANDROID__ ? 3 : 0,
                margin: 4,
                width: screenWidthDips * 0.7,
                backgroundColor: get(colors).colorSurfaceContainer,
                defaultColor: folder.color
            }
        });
        if (color) {
            await folder.save({ color });
        }
        return color;
    } catch (error) {
        showError(error);
    }
}

export async function addCurrentImageToDocument({
    autoRotate,
    fileName,
    imageHeight,
    imageRotation,
    imageWidth,
    pagesToAdd,
    quads,
    sourceImagePath
}: {
    pagesToAdd;
    sourceImagePath;
    imageWidth;
    imageHeight;
    autoRotate?: boolean;
    imageRotation;
    quads;
    fileName?: string;
    transforms?: string[];
}) {
    if (!sourceImagePath) {
        return;
    }
    const start = Date.now();
    const colorType = ApplicationSettings.getString(SETTINGS_DEFAULT_COLORTYPE, DEFAULT_COLORTYPE) as MatricesTypes;
    const contrast = ApplicationSettings.getNumber(SETTINGS_DEFAULT_CONTRAST, DEFAULT_CONTRAST);
    const brightness = ApplicationSettings.getNumber(SETTINGS_DEFAULT_BRIGHTNESS, DEFAULT_BRIGHTNESS);
    let colorMatrix: Matrix;
    try {
        colorMatrix = JSON.parse(ApplicationSettings.getString(SETTINGS_DEFAULT_COLORMATRIX, DEFAULT_COLORMATRIX));
    } catch (error) {
        colorMatrix = null;
        ApplicationSettings.remove(SETTINGS_DEFAULT_COLORMATRIX);
    }
    const transforms = ApplicationSettings.getString(SETTINGS_DEFAULT_TRANSFORM, '');
    DEV_LOG && console.log('addCurrentImageToDocument', sourceImagePath, quads);
    const images: CropResult[] = [];
    const imageExportSettings = getImageExportSettings();
    if (quads) {
        const compressFormat = sourceImagePath.toLowerCase().endsWith('.png') ? 'png' : imageExportSettings.imageFormat;

        images.push(
            ...(await cropDocumentFromFile(sourceImagePath, quads, {
                transforms,
                saveInFolder: knownFolders.temp().path,
                fileName,
                autoRotate,
                compressFormat,
                compressQuality: imageExportSettings.imageQuality
            }))
        );
    } else {
        images.push({ imagePath: sourceImagePath, width: imageWidth, height: imageHeight });
    }
    // let images = quads ? await cropDocumentFromFile(sourceImagePath, quads, strTransforms) : [sourceImagePath];
    if (images.length) {
        // if (!document) {
        //     document = await OCRDocument.createDocument();
        // }

        for (let index = 0; index < images.length; index++) {
            const image = images[index];
            let qrcode;
            let colors;
            if (CARD_APP) {
                [qrcode, colors] = await processFromFile(
                    image.imagePath,
                    [
                        {
                            type: 'qrcode'
                        },
                        {
                            type: 'palette',
                            shrunkImageHeight: COLOR_PALETTE_RESIZE_THRESHOLD,
                            colorsFilterDistanceThreshold: 20,
                            nbColors: 5,
                            colorPalette: 2
                        }
                    ],
                    {
                        resizeThreshold: QRCODE_RESIZE_THRESHOLD
                    }
                );
                DEV_LOG && console.log('qrcode and colors', qrcode, colors);
            }
            pagesToAdd.push({
                ...image,
                crop: quads?.[index] || [
                    [0, 0],
                    [imageWidth - 0, 0],
                    [imageWidth - 0, imageHeight - 0],
                    [0, imageHeight - 0]
                ],
                transforms,
                colorMatrix,
                colorType,
                contrast,
                brightness,
                sourceImagePath,
                sourceImageWidth: imageWidth,
                sourceImageHeight: imageHeight,
                sourceImageRotation: imageRotation,
                rotation: 0, // cropped image is always rotated
                ...(CARD_APP
                    ? {
                          qrcode,
                          colors
                      }
                    : {})
            });
        }
    }
    DEV_LOG && console.log('addCurrentImageToDocument done', sourceImagePath, Date.now() - start, 'ms');
}
export async function processCameraImage({
    autoScan = false,
    fileName,
    imagePath,
    onAfterModalImport,
    onBeforeModalImport,
    onlyForOCR = false,
    pagesToAdd
}: {
    imagePath: string;
    fileName?: string;
    autoScan?: boolean;
    onlyForOCR?: boolean;
    onBeforeModalImport?: Function;
    onAfterModalImport?: Function;
    pagesToAdd;
}) {
    const previewResizeThreshold = ApplicationSettings.getNumber('previewResizeThreshold', PREVIEW_RESIZE_THRESHOLD);
    const areaScaleMinFactor = ApplicationSettings.getNumber('areaScaleMinFactor', AREA_SCALE_MIN_FACTOR);
    const noDetectionMargin = ApplicationSettings.getNumber('documentNotDetectedMargin', DOCUMENT_NOT_DETECTED_MARGIN);
    const cropEnabled = ApplicationSettings.getBoolean(SETTINGS_CROP_ENABLED, CROP_ENABLED);
    // const colorType = ApplicationSettings.getString('defaultColorType', 'normal');
    // const transforms = ApplicationSettings.getString(SETTINGS_DEFAULT_TRANSFORM, DEFAULT_TRANSFORM);

    const alwaysPromptForCrop = ApplicationSettings.getBoolean(SETTINGS_ALWAYS_PROMPT_CROP_EDIT, ALWAYS_PROMPT_CROP_EDIT);
    let quads: Quads;
    const imageSize = await getImageSize(imagePath);
    const imageRotation = imageSize.rotation;
    const imageWidth = imageSize.width;
    const imageHeight = imageSize.height;
    DEV_LOG && console.log('processCameraImage', imagePath, previewResizeThreshold, quads, imageSize.width, imageSize.height, alwaysPromptForCrop);
    if (cropEnabled) {
        const start = Date.now();
        quads = await getJSONDocumentCornersFromFile(imagePath, { resizeThreshold: previewResizeThreshold * 1.5, areaScaleMinFactor });
        DEV_LOG && console.log('processCameraImage got quads', Date.now() - start, 'ms');
    }
    if (cropEnabled && (quads.length === 0 || alwaysPromptForCrop)) {
        let width = imageWidth;
        let height = imageHeight;
        if (imageRotation % 180 !== 0) {
            width = imageHeight;
            height = imageWidth;
        }
        let items = [
            {
                imagePath,
                imageWidth,
                imageHeight,
                imageRotation,
                undos: [],
                redos: [],
                quads:
                    quads.length > 0
                        ? quads
                        : ([
                              [
                                  [noDetectionMargin, noDetectionMargin],
                                  [width - noDetectionMargin, noDetectionMargin],
                                  [width - noDetectionMargin, height - noDetectionMargin],
                                  [noDetectionMargin, height - noDetectionMargin]
                              ]
                          ] as Quads)
            }
        ];
        if (alwaysPromptForCrop || autoScan === false) {
            onBeforeModalImport?.();
            const ModalImportImage = (await import('~/components/ModalImportImages.svelte')).default;
            items = await showModal({
                page: ModalImportImage,
                animated: true,
                fullscreen: true,
                props: {
                    items
                }
            });
            quads = items?.[0]?.quads;
            onAfterModalImport?.();
        }
    }

    if (onlyForOCR) {
        return { sourceImagePath: imagePath, fileName, imageWidth, imageHeight, imageRotation, quads };
    }
    if (!cropEnabled || quads?.length) {
        await addCurrentImageToDocument({ sourceImagePath: imagePath, fileName, imageWidth, imageHeight, imageRotation, quads, pagesToAdd });
        return true;
    }
    showSnack({ message: lc('no_document_found') });
    return false;
}
export async function goToDocumentAfterScan(document?: OCRDocument, oldPagesNumber = 0, canGoToView = true) {
    await hideLoading();
    // await timeout(1000);
    DEV_LOG && console.log('goToDocumentAfterScan', document.pages.length, oldPagesNumber, canGoToView);
    if (oldPagesNumber === 0 || document.pages.length - oldPagesNumber === 1) {
        const component = (await import('~/components/edit/DocumentEdit.svelte')).default;
        DEV_LOG && console.log('goToDocumentAfterScan', document.pages.length);
        navigate({
            page: component,
            props: {
                document,
                startPageIndex: document.pages.length - 1,
                transitionOnBack: null
            }
        });
    } else if (canGoToView) {
        return goToDocumentView(document, false);
    }
}

export async function importImageFromCamera({
    canGoToView = true,
    document,
    folder,
    inverseUseSystemCamera = false
}: { document?: OCRDocument; canGoToView?: boolean; inverseUseSystemCamera?; folder?: DocFolder } = {}) {
    const useSystemCamera = __ANDROID__ ? ApplicationSettings.getBoolean('use_system_camera', USE_SYSTEM_CAMERA) : false;

    await requestCameraPermission();
    DEV_LOG && console.log('importImageFromCamera', useSystemCamera, inverseUseSystemCamera);
    if (__ANDROID__ && (inverseUseSystemCamera ? !useSystemCamera : useSystemCamera)) {
        const resultImagePath = await new Promise<string>((resolve, reject) => {
            const takePictureIntent = new android.content.Intent(android.provider.MediaStore.ACTION_IMAGE_CAPTURE);

            let tempPictureUri;
            const context = Utils.android.getApplicationContext();
            const picturePath = context.getExternalFilesDir(null).getAbsolutePath() + SEPARATOR + 'NSIMG_' + dayjs().format('MM_DD_YYYY') + '.jpg';
            const nativeFile = new java.io.File(picturePath);

            if (SDK_VERSION >= 21) {
                tempPictureUri = androidx.core.content.FileProvider.getUriForFile(context, __APP_ID__ + '.provider', nativeFile);
            } else {
                tempPictureUri = android.net.Uri.fromFile(nativeFile);
            }

            takePictureIntent.putExtra(android.provider.MediaStore.EXTRA_OUTPUT, tempPictureUri);
            takePictureIntent.putExtra('android.intent.extras.CAMERA_FACING', 0);
            let resolved = false;
            // if (takePictureIntent.resolveActivity(context.getPackageManager()) != null) {
            const REQUEST_IMAGE_CAPTURE = 3453;
            function onActivityResult(args) {
                const { requestCode } = args;
                const { resultCode } = args;

                if (requestCode === REQUEST_IMAGE_CAPTURE) {
                    Application.android.off(Application.android.activityResultEvent, onActivityResult);
                    if (resultCode === android.app.Activity.RESULT_OK) {
                        DEV_LOG && console.log('startActivityForResult got image', picturePath);
                        if (!resolved) {
                            resolved = true;
                            resolve(picturePath);
                        } else {
                            DEV_LOG && console.warn('startActivityForResult got another image!', picturePath);
                        }
                    } else if (resultCode === android.app.Activity.RESULT_CANCELED) {
                        // User cancelled the image capture
                        reject();
                    }
                }
            }
            Application.android.on(Application.android.activityResultEvent, onActivityResult);
            // on android a background event will trigger while picking a file
            securityService.ignoreNextValidation();
            DEV_LOG && console.log('startActivityForResult REQUEST_IMAGE_CAPTURE');
            Application.android.startActivity.startActivityForResult(takePictureIntent, REQUEST_IMAGE_CAPTURE);
            // } else {
            //     reject(new Error('camera_intent_not_supported'));
            // }
        });
        if (resultImagePath) {
            const pagesToAdd = [];
            const result = await processCameraImage({
                imagePath: resultImagePath,
                pagesToAdd
            });
            if (result && pagesToAdd.length) {
                if (!document) {
                    document = await OCRDocument.createDocument(pagesToAdd, folder);
                } else {
                    await document.addPages(pagesToAdd);
                }
                await goToDocumentAfterScan(document, 0, canGoToView);
            }
        }

        return;
    }
    const oldPagesNumber = document?.pages.length ?? 0;
    const Camera = (await import('~/components/camera/Camera.svelte')).default;
    document = await showModal({
        page: Camera,
        fullscreen: true,
        props: {
            folder,
            document
        }
    });
    if (document) {
        return goToDocumentAfterScan(document, oldPagesNumber);
    }
}

export async function getOCRFromCamera({ inverseUseSystemCamera = false }: { inverseUseSystemCamera? } = {}) {
    const useSystemCamera = __ANDROID__ ? ApplicationSettings.getBoolean('use_system_camera', USE_SYSTEM_CAMERA) : false;
    let result: {
        autoRotate?;
        fileName?;
        quads?;
        sourceImagePath;
    };
    await requestCameraPermission();
    DEV_LOG && console.log('getOCRFromCamera', useSystemCamera, inverseUseSystemCamera);
    if (__ANDROID__ && (inverseUseSystemCamera ? !useSystemCamera : useSystemCamera)) {
        const resultImagePath = await new Promise<string>((resolve, reject) => {
            const takePictureIntent = new android.content.Intent(android.provider.MediaStore.ACTION_IMAGE_CAPTURE);

            let tempPictureUri;
            const context = Utils.android.getApplicationContext();
            const picturePath = context.getExternalFilesDir(null).getAbsolutePath() + SEPARATOR + 'NSIMG_' + dayjs().format('MM_DD_YYYY') + '.jpg';
            const nativeFile = new java.io.File(picturePath);

            if (SDK_VERSION >= 21) {
                tempPictureUri = androidx.core.content.FileProvider.getUriForFile(context, __APP_ID__ + '.provider', nativeFile);
            } else {
                tempPictureUri = android.net.Uri.fromFile(nativeFile);
            }

            takePictureIntent.putExtra(android.provider.MediaStore.EXTRA_OUTPUT, tempPictureUri);
            takePictureIntent.putExtra('android.intent.extras.CAMERA_FACING', 0);
            let resolved = false;
            // if (takePictureIntent.resolveActivity(context.getPackageManager()) != null) {
            const REQUEST_IMAGE_CAPTURE = 3453;
            function onActivityResult(args) {
                const { requestCode } = args;
                const { resultCode } = args;

                if (requestCode === REQUEST_IMAGE_CAPTURE) {
                    Application.android.off(Application.android.activityResultEvent, onActivityResult);
                    if (resultCode === android.app.Activity.RESULT_OK) {
                        DEV_LOG && console.log('startActivityForResult got image', picturePath);
                        if (!resolved) {
                            resolved = true;
                            resolve(picturePath);
                        } else {
                            DEV_LOG && console.warn('startActivityForResult got another image!', picturePath);
                        }
                    } else if (resultCode === android.app.Activity.RESULT_CANCELED) {
                        // User cancelled the image capture
                        reject();
                    }
                }
            }
            Application.android.on(Application.android.activityResultEvent, onActivityResult);
            // on android a background event will trigger while picking a file
            securityService.ignoreNextValidation();
            DEV_LOG && console.log('startActivityForResult REQUEST_IMAGE_CAPTURE');
            Application.android.startActivity.startActivityForResult(takePictureIntent, REQUEST_IMAGE_CAPTURE);
            // } else {
            //     reject(new Error('camera_intent_not_supported'));
            // }
        });
        if (resultImagePath) {
            result = { sourceImagePath: resultImagePath };
        }
    } else {
        const Camera = (await import('~/components/camera/Camera.svelte')).default;
        result = await showModal({
            page: Camera,
            fullscreen: true,
            props: {
                onlyForOCR: true
            }
        });
    }
    if (result) {
        const imageExportSettings = getImageExportSettings();
        DEV_LOG && console.log('camera for ocr', JSON.stringify(result));
        const imagePath = result.quads
            ? (
                  await cropDocumentFromFile(result.sourceImagePath, result.quads, {
                      saveInFolder: knownFolders.temp().path,
                      fileName: result.fileName,
                      autoRotate: result.autoRotate,
                      compressFormat: imageExportSettings.imageFormat,
                      compressQuality: imageExportSettings.imageQuality
                  })
              )[0].imagePath
            : result.sourceImagePath;
        DEV_LOG && console.log('camera for ocr imagePath', imagePath);

        return ocrService.ocrImage({
            imagePath
        });
    }
}

export function createView<T extends View>(claz: new () => T, props: Partial<Pick<T, keyof T>> = {}, events?) {
    const view: T = new claz();
    Object.assign(view, props);
    if (events) {
        Object.keys(events).forEach((k) => view.on(k, events[k]));
    }
    return view;
}

export async function confirmGoBack({ message, onGoBack }: { onGoBack?; message? } = {}) {
    try {
        const result = await confirm({
            message: message || lc('sure_go_back')
        });
        if (result) {
            if (onGoBack) {
                onGoBack();
            } else {
                goBack();
            }
        }
    } catch (error) {
        showError(error);
    }
}

export async function onStartCam(inverseUseSystemCamera = false) {
    try {
        await importImageFromCamera({ inverseUseSystemCamera });
    } catch (error) {
        showError(error);
    }
}

export function getNameFormatHTMLArgs() {
    const cols = get(colors);
    return [
        `<span style="background-color:${cols.colorSurfaceContainerHigh};">iso</span>`,
        '<a href="https://en.m.wikipedia.org/wiki/ISO_8601">ISO 8641</a>',
        `<span style="background-color:${cols.colorSurfaceContainerHigh};">timestamp</span>`,
        `<span style="background-color:${cols.colorSurfaceContainerHigh};">YYYY,M,MM,D,H,s...</span>`,
        `<a href="https://day.js.org/docs/en/display/format">${l('here')}</a>`
    ];
}
export async function promptForFolderName(defaultGroup: string, groups?: DocFolder[]): Promise<string> {
    const TagView = (await import('~/components/common/FolderView.svelte')).default;
    const componentInstanceInfo = resolveComponentElement(TagView, { groups, defaultGroup });
    const modalView: View = componentInstanceInfo.element.nativeView;
    const result = await confirm({
        title: lc('move_folder'),
        message: lc('move_folder_desc'),
        view: modalView,
        okButtonText: lc('move'),
        cancelButtonText: lc('cancel')
    });
    const currentFolderText = componentInstanceInfo.viewInstance['currentFolderText'];
    try {
        modalView._tearDownUI();
        componentInstanceInfo.viewInstance.$destroy(); // don't let an exception in destroy kill the promise callback
    } catch (error) {}
    if (result) {
        return currentFolderText || 'none';
    }
    return null;
}

export async function showCustomAlert<T>(component, options, props = {}): Promise<any> {
    const componentInstanceInfo = resolveComponentElement(component, props);
    const modalView: View = componentInstanceInfo.element.nativeView;
    const result = await alert({
        view: modalView,
        ...options
    });
    try {
        modalView._tearDownUI();
        componentInstanceInfo.viewInstance.$destroy(); // don't let an exception in destroy kill the promise callback
    } catch (error) {}
    return result;
}

export async function importPKPassFromUris({ canGoToView = true, uris }: { uris: string[]; canGoToView?: boolean }) {
    if (CARD_APP) {
        try {
            await showLoading(lc('importing'));

            // Import the PKPass file
            const document = await importPKPassFiles(uris, null);

            if (canGoToView && document) {
                await hideLoading();
                await goToDocumentView(document);
            }

            showSnack({ message: lc('imported') });
        } catch (error) {
            showError(error);
        } finally {
            DEV_LOG && console.log('importPKPassFromUris finally');
            hideLoading();
        }
    }
}

export function isPermResultNeverAskAgain(r: MultiResult | Status) {
    const statusCheck = __IOS__ ? Status.Denied : Status.NeverAskAgain;
    if (typeof r === 'object') {
        const denied = Object.keys(r).some((s) => r[s] === statusCheck);
        return denied;
    }
    return r === statusCheck;
}

export async function requestPermission(perm: Permissions, errorMessage: string) {
    const result = await request(perm);
    if (isPermResultNeverAskAgain(result)) {
        const showSetting = await confirm({
            title: lc('permission_denied'),
            message: errorMessage,
            iosForceClosePresentedViewController: true, //ensure error popup is always showing
            okButtonText: l('open_settings'),
            cancelButtonText: l('cancel')
        });
        if (showSetting) {
            await openSettings();
        } else {
            throw new IgnoreError();
        }
    } else if (!isPermResultAuthorized(result)) {
        throw new PermissionError(errorMessage);
    }
}

export async function requestCameraPermission() {
    return requestPermission('camera', lc('camera_permission_needed'));
}

export async function requestStoragePermission() {
    if (__ANDROID__ && SDK_VERSION <= 29) {
        return requestPermission('storage', lc('storage_permission_needed'));
    }
}

export async function requestPhotoPermission() {
    return requestPermission('photo', lc('media_library_permission_needed'));
}
export async function requestGalleryPermission() {
    return requestPermission('mediaLibrary', lc('media_library_permission_needed'));
}
export async function requestNotificationPermission() {
    return requestPermission('notification', lc('notification_permission_needed'));
}
