<script context="module" lang="ts">
    import { SilentError } from '@akylas/nativescript-app-utils/error';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { CameraView } from '@nativescript-community/ui-cameraview';
    import { Canvas, CanvasView, Paint, Style } from '@nativescript-community/ui-canvas';
    import { showBottomSheet } from '@nativescript-community/ui-material-bottomsheet/svelte';
    import { confirm } from '@nativescript-community/ui-material-dialogs';
    import { Slider } from '@nativescript-community/ui-material-slider/slider';
    import { AbsoluteLayout, AndroidActivityBackPressedEventData, Application, ApplicationSettings, Page, Utils, knownFolders, path } from '@nativescript/core';
    import { ImageSource } from '@nativescript/core/image-source';
    import { debounce, wrapNativeException } from '@nativescript/core/utils';
    import { showError } from '@shared/utils/showError';
    import { closeModal, navigate } from '@shared/utils/svelte/ui';
    import { createAutoScanHandler, createQRCodeCallback } from 'plugin-nativeprocessor';
    import { CropView } from 'plugin-nativeprocessor/CropView';
    import { onDestroy, onMount } from 'svelte';
    import { writable } from 'svelte/store';
    import CActionBar from '~/components/common/CActionBar.svelte';
    import IconButton from '@shared/components/IconButton.svelte';
    import { lc } from '~/helpers/locale';
    import { DocFolder, OCRDocument, PageData } from '~/models/OCRDocument';
    import { documentsService } from '~/services/documents';
    import {
        AUTO_SCAN_DELAY,
        AUTO_SCAN_DISTANCETHRESHOLD,
        AUTO_SCAN_DURATION,
        AUTO_SCAN_ENABLED,
        CROP_ENABLED,
        DEFAULT_FONT_CAM_MIRRORED,
        IMAGE_CONTEXT_OPTIONS,
        IMG_FORMAT,
        PREVIEW_RESIZE_THRESHOLD,
        SETTINGS_CAMERA_SETTINGS,
        SETTINGS_CROP_ENABLED,
        SETTINGS_FONT_CAM_MIRRORED,
        SETTINGS_IMAGE_EXPORT_FORMAT,
        getImageExportSettings
    } from '~/utils/constants';
    import { recycleImages } from '~/utils/images';
    import { confirmGoBack, goToDocumentAfterScan, hideLoading, onBackButton, processCameraImage, requestCameraPermission, showLoading, showSettings } from '~/utils/ui';
    import { checkAvailableStorage } from '~/utils/utils';
    import { colors, shouldListenForSensorOrientation, startOnCam, windowInset } from '~/variables';
</script>

<script lang="ts">
    // technique for only specific properties to get updated on store change
    $: ({ colorBackground, colorOnBackground, colorOutlineVariant, colorPrimary } = $colors);

    let page: NativeViewElementNode<Page>;
    let cameraView: NativeViewElementNode<CameraView>;
    let focusRing: NativeViewElementNode<AbsoluteLayout>;
    let takPictureBtnCanvas: NativeViewElementNode<CanvasView>;
    let cropView: NativeViewElementNode<CropView>;
    let zoomSlider: NativeViewElementNode<Slider>;

    const cameraOptionsStore = writable<{ aspectRatio: string; stretch: string; viewsize: string; pictureSize: string }>(
        JSON.parse(ApplicationSettings.getString(SETTINGS_CAMERA_SETTINGS, '{"aspectRatio":"4:3", "stretch":"aspectFit","viewsize":"limited", "pictureSize":null}'))
    );
    $: DEV_LOG && console.log('cameraOptions', JSON.stringify($cameraOptionsStore));
    const cropEnabled = ApplicationSettings.getBoolean(SETTINGS_CROP_ENABLED, CROP_ENABLED);
    const frontMirrored = ApplicationSettings.getBoolean(SETTINGS_FONT_CAM_MIRRORED, DEFAULT_FONT_CAM_MIRRORED);
    cameraOptionsStore.subscribe((newValue) => {
        ApplicationSettings.setString(SETTINGS_CAMERA_SETTINGS, JSON.stringify(newValue));
    });
    $: ({ aspectRatio, pictureSize, stretch, viewsize } = $cameraOptionsStore);

    export let document: OCRDocument = null;
    export let QRCodeOnly = false;
    export let onlyForOCR = false;
    export let folder: DocFolder = null;
    export let onClose: Function = null;

    let nbPages = 0;
    let takingPicture = false;
    let smallImage: string = null;
    let smallImageRotation: number = 0;
    const previewResizeThreshold = ApplicationSettings.getNumber('previewResizeThreshold', PREVIEW_RESIZE_THRESHOLD);
    let flashMode = ApplicationSettings.getNumber('defaultFlashMode', 0);
    let zoom = 1;
    let _actualFlashMode = flashMode;
    let torchEnabled = false;
    let batchMode = !onlyForOCR && ApplicationSettings.getBoolean('batchMode', false);
    let canSaveDoc = false;
    let editing = false;
    const imageExportSettings = getImageExportSettings();
    const compressQuality = imageExportSettings.imageQuality;
    $: ApplicationSettings.setBoolean('batchMode', batchMode);

    async function showDocumentsList() {
        if (startOnCam) {
            if (CARD_APP) {
                const CardsList = (await import('~/components/list/CardsList.svelte')).default;
                return navigate({ page: CardsList });
            } else {
                const DocumentsList = (await import('~/components/list/DocumentsList.svelte')).default;
                return navigate({ page: DocumentsList });
            }
        }
    }

    async function showCameraSettings() {
        try {
            const addedProps: any = __ANDROID__
                ? {
                      resolutions: cameraView.nativeView.getAllAvailablePictureSizes(),
                      currentResolution: cameraView.nativeView.getCurrentResolutionInfo()
                  }
                : {};
            // DEV_LOG && console.log('showCameraSettings', JSON.stringify(addedProps), JSON.stringify(get(cameraOptionsStore)));
            const view = (await import('~/components/widgets/ImageProcessingSettingsBottomSheet.svelte')).default;
            await showBottomSheet({
                parent: page,
                view,
                backgroundOpacity: 0.8,
                skipCollapsedState: true,
                props: {
                    showCameraSettings: true,
                    cameraOptionsStore,
                    ...addedProps
                }
            });
        } catch (error) {
            showError(error);
        }
    }

    async function processAndAddImage(image: string | UIImage, autoScan = false, onlyForOCR = false) {
        let imageSource: ImageSource;
        try {
            showLoading(lc('computing'));
            let tempImagePath: string;
            if (typeof image === 'string') {
                tempImagePath = image;
            } else {
                imageSource = new ImageSource(image);
                const compressFormat = ApplicationSettings.getString(SETTINGS_IMAGE_EXPORT_FORMAT, IMG_FORMAT) as 'png' | 'jpeg' | 'jpg';
                tempImagePath = path.join(knownFolders.temp().path, `capture_${Date.now()}.${compressQuality}`);
                await imageSource.saveToFileAsync(tempImagePath, compressFormat, compressQuality);
                //clear memory as soon as possible
                recycleImages(imageSource);
                imageSource = null;
            }

            return await processCameraImage({
                onlyForOCR,
                imagePath: tempImagePath,
                fileName: `cropedBitmap_${pagesToAdd.length}.${IMG_FORMAT}`,
                autoScan,
                onBeforeModalImport: () => {
                    if (torchEnabled) {
                        forceTorchDisabled(true);
                    }
                },
                onAfterModalImport: () => {
                    if (torchEnabled) {
                        forceTorchDisabled(false);
                    }
                },
                pagesToAdd
            });
        } catch (err) {
            throw err;
        } finally {
            if (imageSource) {
                recycleImages(imageSource);
                imageSource = null;
            }
        }
    }

    function pauseAutoScan() {
        DEV_LOG && console.log('pauseAutoScan', autoScanHandler, autoScanHandler?.enabled);
        if (autoScanHandler) {
            autoScanHandler.enabled = false;
        }
    }
    function resumeAutoScan() {
        DEV_LOG && console.log('resumeAutoScan', autoScanHandler, autoScanHandler?.enabled);
        if (autoScanHandler) {
            autoScanHandler.enabled = true;
        }
    }
    async function takePicture(autoScan = false) {
        try {
            DEV_LOG && console.log('takePicture', takingPicture, cameraOpened);
            if (takingPicture || !cameraOpened) {
                return;
            }
            if (!checkAvailableStorage(49999872 /* ~47 MB */)) {
                throw new SilentError(lc('not_enough_space'));
            }
            takingPicture = true;
            if (autoScanHandler) {
                pauseAutoScan();
            }
            DEV_LOG && console.log('takePicture', autoScan, _actualFlashMode);
            const start = Date.now();
            await showLoading(lc('capturing'));
            // on Android we the capture will directly save the image to a temp directory
            // but thus maxWidth / maxHeight is ignored
            const { image } = await cameraView.nativeView.takePicture({
                savePhotoToDisk: __ANDROID__,
                storageLocation: 'file:' + knownFolders.temp().path,
                fileName: `capture_${Date.now()}.${compressQuality}`,
                flashMode: _actualFlashMode,
                maxWidth: 4500,
                maxHeight: 4500
            });
            const didAdd = await processAndAddImage(image, autoScan, onlyForOCR);
            DEV_LOG && console.log('takePicture got image', batchMode, !!image, !!didAdd, Date.now() - start, 'ms');
            if (didAdd) {
                if (onlyForOCR) {
                    return closeModal(didAdd);
                }
                if (batchMode) {
                    nbPages = pagesToAdd.length;
                    const lastPage = pagesToAdd[pagesToAdd.length - 1];
                    DEV_LOG && console.log('setCurrentImage', JSON.stringify(lastPage));
                    setCurrentImage(lastPage.imagePath, lastPage.rotation, true);
                } else {
                    await saveCurrentDocument();
                }
            }
            DEV_LOG && console.log('takePicture done', didAdd);
        } catch (err) {
            // we can get a native error here
            const error = wrapNativeException(err);
            if (__ANDROID__ && /(closed|submit|failed)/.test(error.message)) {
                DEV_LOG && console.warn('ignored error', error);
                // ignore camera closed errors as they can happen whil app is going to background
            } else {
                showError(error);
            }
        } finally {
            takingPicture = false;
            if (autoScanHandler) {
                resumeAutoScan();
            }
            if (!previewStarted) {
                stopPreview(true);
            }
            hideLoading();
        }
    }

    $: {
        _actualFlashMode = torchEnabled ? 4 : (flashMode as any);
    }
    function forceTorchDisabled(value) {
        if (value) {
            _actualFlashMode = flashMode;
        } else {
            _actualFlashMode = torchEnabled ? 4 : (flashMode as any);
        }
    }
    function switchTorch() {
        torchEnabled = !torchEnabled;
    }
    function toggleCamera() {
        cameraView.nativeView.toggleCamera();
    }

    onMount(async () => {
        $shouldListenForSensorOrientation = true;
        // if (!check('camera')) {
        //     stopPreview();
        // }

        onNavigatedTo();
        if (__ANDROID__) {
            Application.android.on(Application.android.activityBackPressedEvent, onAndroidBackButton);
        }
        Application.on(Application.backgroundEvent, onBackground);
        Application.on(Application.foregroundEvent, onForeground);
    });
    onDestroy(() => {
        $shouldListenForSensorOrientation = false;
        // clearImages();
        document = null;
        nbPages = 0;
        if (autoScanHandler) {
            autoScanHandler.enabled = false;
            autoScanHandler = null;
        }
        if (processor) {
            processor.autoScanHandler = null;
            processor = null;
        }
        if (__ANDROID__) {
            Application.android.off(Application.android.activityBackPressedEvent, onAndroidBackButton);
        }
        Application.off(Application.backgroundEvent, onBackground);
        Application.off(Application.foregroundEvent, onForeground);
    });

    let isVisible = false;
    function onNavigatedTo() {
        DEV_LOG && console.log('Camera', 'onNavigatedTo');
        isVisible = true;
        (async () => {
            try {
                await startPreview();
                applyProcessor();
            } catch (error) {
                console.error(error, error.stack);
            }
            if (torchEnabled) {
                forceTorchDisabled(false);
            }
        })();
    }
    function onNavigatedFrom() {
        DEV_LOG && console.log('Camera', 'onNavigatedFrom');
        isVisible = false;
        if (torchEnabled) {
            forceTorchDisabled(true);
        }
        // stopPreview();
        if (!startOnCam && document) {
            // we need to clear the current document which was not saved
            //especially memory images
            document.removeFromDisk();
            document = null;
        }
        stopPreview();
    }
    let previewStarted = false;
    async function startPreview() {
        try {
            DEV_LOG && console.log('[Camera]', 'startPreview', !!cameraView);
            if (!previewStarted) {
                await requestCameraPermission();
                previewStarted = true;
                if (cameraView?.nativeView) {
                    cameraView.nativeView.readyToStartPreview = true;
                    cameraView.nativeView.startPreview();
                }
                resumeAutoScan();
            }
        } catch (error) {
            showError(error);
        }
    }
    function stopPreview(force = false) {
        DEV_LOG && console.log('stopPreview', force, previewStarted, takingPicture);
        if (force || previewStarted) {
            previewStarted = false;
            // if (takingPicture) {
            //     return;
            // }
            pauseAutoScan();
            cameraView?.nativeView.stopPreview();
            if (cropView?.nativeView) {
                cropView.nativeView.quads = null;
            }
        }
    }

    function onBackground() {
        DEV_LOG && console.log('[Camera]', 'onBackground', !!cameraView);
        stopPreview();
    }
    function onForeground() {
        DEV_LOG && console.log('[Camera]', 'onForeground', !!cameraView);
        if (isVisible) {
            startPreview();
        }
    }
    let saveCalled = false;
    export let saveCurrentDocument = async () => {
        if (saveCalled) {
            return;
        }
        saveCalled = true;
        let theDocument = document;
        const newDocument = !theDocument;
        try {
            DEV_LOG && console.log('saveCurrentDocument', newDocument, !!theDocument);
            if (!theDocument) {
                theDocument = document = await OCRDocument.createDocument(pagesToAdd, folder);
                DEV_LOG && console.log('saveCurrentDocument document created', newDocument, !!theDocument);
                if (startOnCam) {
                    await goToDocumentAfterScan(theDocument);
                } else {
                    // we should already be in edit so closing should go back there
                }
            } else {
                await theDocument.addPages(pagesToAdd);
                await theDocument.save({}, false);
            }
            DEV_LOG && console.log('saveCurrentDocument done ', startOnCam, newDocument, !!theDocument);
            if (theDocument) {
                if (!startOnCam) {
                    DEV_LOG && console.log('closing cameral modal ', !!theDocument);
                    closeModal(theDocument);
                } else {
                    // clear
                    nbPages = 0;
                    smallImage = null;
                    smallImageRotation = 0;
                    pagesToAdd = [];
                    document = null;
                    saveCalled = false;
                }
            }
        } catch (error) {
            showError(error);
        }
    };

    function onGoBack() {
        if (PRODUCTION && pagesToAdd.length > 0) {
            confirmGoBack({ message: lc('sure_cancel_import'), onGoBack: closeModal });
        } else {
            if (onClose) {
                onClose();
            } else {
                closeModal(undefined);
            }
        }
    }

    const onAndroidBackButton = (data: AndroidActivityBackPressedEventData) =>
        onBackButton(page?.nativeView, () => {
            if (editing) {
                toggleEditing();
                data.cancel = true;
            } else if (!startOnCam && takingPicture) {
                data.cancel = true;
            } else if (pagesToAdd.length > 0) {
                data.cancel = true;
                onGoBack();
            }
        });

    export let pagesToAdd: PageData[] = [];

    async function setCurrentImage(image: string, rotation = 0, needAnimateBack = false) {
        smallImage = image;
        smallImageRotation = rotation;
    }
    $: canSaveDoc = nbPages > 0;
    function animateFocusRing(x: number, y: number) {
        const view = focusRing?.nativeView;
        view._batchUpdate(() => {
            view.left = x - 35;
            view.top = y - 35;
            view.opacity = 1;
            view.scaleX = 1;
            view.scaleY = 1;
        });
        view.animate({
            delay: 500,
            duration: 300,
            opacity: 0,
            scale: {
                x: 0.7,
                y: 0.7
            }
        });
    }
    function focusCamera(e) {
        try {
            const x = e.getX();
            const y = e.getY();
            DEV_LOG && console.log('focusCamera', x, y);
            animateFocusRing(x, y);
            const view = cameraView.nativeView;
            view.focusAtPoint(x, y);
        } catch (error) {
            showError(error);
        }
    }
    const onZoom = debounce(
        function onZoom(event) {
            zoom = event.zoom;
            ApplicationSettings.setNumber('defaultZoom', zoom);
            updateFloatZoom(zoom);
        },
        500,
        { leading: true }
    );
    const setZoomThrottled = debounce(
        function (value) {
            zoom = value;
            ApplicationSettings.setNumber('defaultZoom', zoom);
        },
        500,
        { leading: true }
    );
    let zoomPercentDelta = 0;
    let floatZoom = zoom;

    function updateFloatZoom(value) {
        floatZoom = value;
        const view = zoomSlider?.nativeView;
        if (view) {
            const zoomPercent = (floatZoom - minZoom) / (maxZoom - minZoom);
            const parentWidth = Utils.layout.toDeviceIndependentPixels(view.getMeasuredWidth()) - 40;
            zoomPercentDelta = parentWidth * zoomPercent;
        }
    }
    function onZoomValue(e) {
        updateFloatZoom(e.value);
        setZoomThrottled(floatZoom);
    }

    let autoScan = ApplicationSettings.getBoolean('autoScan', AUTO_SCAN_ENABLED);
    let processor;
    let autoScanHandler;
    function applyAutoScan(value: boolean) {
        const nCropView = cropView?.nativeView;
        if (!cropEnabled || !nCropView) {
            return;
        }
        if (value) {
            const newAutoScanHandler = createAutoScanHandler(nCropView, (result) => {
                DEV_LOG && console.log('onAutoScan', result);
                // TODO: safeguard though should never happen
                if (!saveCalled && cameraOpened && previewStarted && cameraView?.nativeView && autoScanHandler && autoScanHandler.enabled) {
                    takePicture(true);
                }
            });
            newAutoScanHandler.distanceThreshod = ApplicationSettings.getNumber('autoScan_distanceThreshold', AUTO_SCAN_DISTANCETHRESHOLD);
            newAutoScanHandler.autoScanDuration = ApplicationSettings.getNumber('autoScan_autoScanDuration', AUTO_SCAN_DURATION);
            newAutoScanHandler.preAutoScanDelay = ApplicationSettings.getNumber('autoScan_preAutoScanDelay', AUTO_SCAN_DELAY);
            autoScanHandler = newAutoScanHandler;
            if (processor) {
                processor.autoScanHandler = autoScanHandler;
            }
        } else {
            if (processor) {
                processor.autoScanHandler = null;
            }
            autoScanHandler = null;
        }
    }
    function toggleAutoScan(apply = true) {
        if (!cropEnabled) {
            return;
        }
        DEV_LOG && console.log('toggleAutoScan', autoScan, apply);
        autoScan = !autoScan;
        ApplicationSettings.setBoolean('autoScan', autoScan);
        if (apply) {
            applyAutoScan(autoScan);
        }
    }
    async function applyProcessor() {
        try {
            const nCropView = cropView?.nativeView?.nativeViewProtected;
            const nCameraView = cameraView?.nativeView;
            DEV_LOG && console.log('applyProcessor', processor, cropEnabled, nCropView, nCameraView, isVisible);
            if (processor || !cropEnabled || !isVisible || !nCameraView || !nCropView) {
                return;
            }
            if (!QRCodeOnly) {
                const showAutoScanWarning = ApplicationSettings.getBoolean('showAutoScanWarning', true);
                if (showAutoScanWarning) {
                    if (autoScan) {
                        const result = await confirm({
                            message: lc('auto_scan_first_use'),
                            okButtonText: lc('enable'),
                            cancelButtonText: lc('disable')
                        });
                        if (!result && autoScan) {
                            toggleAutoScan(false);
                        }
                    }
                    ApplicationSettings.setBoolean('showAutoScanWarning', false);
                }
            }
            if (__ANDROID__) {
                const context = Utils.android.getApplicationContext();
                processor = new com.akylas.documentscanner.CustomImageAnalysisCallback(
                    context,
                    nCropView,
                    createQRCodeCallback(
                        QRCodeOnly
                            ? (str) => {
                                  closeModal(JSON.parse(str));
                              }
                            : null
                    )
                );
                (processor as com.akylas.documentscanner.CustomImageAnalysisCallback).setDetectQRCode(QRCodeOnly);
                (processor as com.akylas.documentscanner.CustomImageAnalysisCallback).setDetectDocuments(!QRCodeOnly);
                nCameraView.processor = processor;
            } else {
                processor = OpencvDocumentProcessDelegate.alloc().initWithCropViewOnQRCode(
                    nCropView,
                    createQRCodeCallback(
                        QRCodeOnly
                            ? (str) => {
                                  closeModal(JSON.parse(str));
                              }
                            : null
                    )
                );
                nCameraView.processor = processor;
                (processor as OpencvDocumentProcessDelegate).detectQRCode = QRCodeOnly;
                (processor as OpencvDocumentProcessDelegate).detectDocuments = !QRCodeOnly;
            }
            DEV_LOG && console.log('applyProcessor', processor, previewResizeThreshold);
            applyAutoScan(autoScan);
            processor.previewResizeThreshold = previewResizeThreshold;

            if (documentsService.started) {
                await startPreview();
            } else {
                documentsService.once('started', startPreview);
            }
        } catch (error) {
            showError(error);
        }
    }

    // unused
    function toggleEditing() {
        editing = !editing;
        if (torchEnabled) {
            forceTorchDisabled(editing);
        }
        if (editing) {
            stopPreview();
        } else {
            startPreview();
        }
    }

    function getFlashIcon(flashMode) {
        switch (flashMode) {
            case 0:
                return 'mdi-flash-off';
            case 1:
                return 'mdi-flash';
            case 2:
                return 'mdi-flash-auto';
            case 3:
                return 'mdi-flash-red-eye';
            case 4:
                return 'mdi-flashlight';
        }
    }

    const borderStroke = 3;
    const borderPaint = new Paint();
    borderPaint.strokeWidth = borderStroke;
    borderPaint.style = Style.STROKE;
    borderPaint.color = 'white';

    function onAutoScanChanged(value) {
        if (takPictureBtnCanvas?.nativeView) {
            takPictureBtnCanvas.nativeView.invalidate();
        }
    }
    $: onAutoScanChanged(autoScan);
    function drawTakePictureBtnBorder({ canvas }: { canvas: Canvas }) {
        const w = canvas.getWidth();
        const h = canvas.getHeight();
        const radius = Math.min(w, h) / 2 - borderStroke / 2;
        if (autoScan) {
            canvas.drawArc(w / 2 - radius, h / 2 - radius, w / 2 + radius, h / 2 + radius, 0, 270, false, borderPaint);
        } else {
            canvas.drawCircle(w / 2, h / 2, radius, borderPaint);
        }
    }

    let maxZoom = 1;
    let minZoom = 1;
    let cameraOpened = false;
    function onCameraOpen({ object }: { object: CameraView }) {
        try {
            zoom = ApplicationSettings.getNumber('defaultZoom', cameraView.nativeView.neutralZoom);
            minZoom = cameraView.nativeView.minZoom;
            maxZoom = Math.min(cameraView.nativeView.maxZoom, 16);
            DEV_LOG && console.log('onCameraOpen', minZoom, maxZoom, zoom);
            if (__ANDROID__) {
                const currentResolution = cameraView.nativeView.getCurrentResolutionInfo();
                if (currentResolution) {
                    cameraOptionsStore.update((state) => {
                        state['aspectRatio'] = currentResolution.aspectRatio;
                        state['pictureSize'] = currentResolution.pictureSize;
                        return state;
                    });
                }
            }
            cameraOpened = true;
        } catch (error) {
            showError(error);
        }
    }
    function onCameraHolderLoaded() {
        DEV_LOG && console.log('Camera', 'onCameraHolderLoaded');
        applyProcessor();
    }
</script>

<page bind:this={page} id="camera" actionBarHidden={true} statusBarStyle="dark" on:navigatedTo={onNavigatedTo} on:navigatedFrom={onNavigatedFrom}>
    <gridlayout class="pageContent" backgroundColor="black" rows="auto,*,auto,auto">
        <absolutelayout rowSpan={viewsize === 'full' ? 4 : 2} on:loaded={onCameraHolderLoaded}>
            <cameraView
                bind:this={cameraView}
                {aspectRatio}
                autoFocus={true}
                captureMode={1}
                enablePinchZoom={true}
                flashMode={_actualFlashMode}
                {frontMirrored}
                height="100%"
                jpegQuality={compressQuality}
                {pictureSize}
                readyToStartPreview={false}
                ios:iosCaptureMode="photo"
                {stretch}
                width="100%"
                {zoom}
                on:cameraOpen={onCameraOpen}
                on:zoom={onZoom}
                on:tap={focusCamera} />
            <cropview bind:this={cropView} colors={[colorPrimary]} fillAlpha={120} height="100%" isUserInteractionEnabled={false} {stretch} strokeWidth={3} width="100%" />
            <absoluteLayout bind:this={focusRing} borderColor="white" borderRadius={35} borderWidth={1.5} height={70} opacity={0} width={70} />
        </absolutelayout>

        <!-- <canvasView bind:this={canvasView} rowSpan="2" on:draw={onCanvasDraw} on:tap={focusCamera} /> -->
        <CActionBar backgroundColor="transparent" buttonsDefaultVisualState="black" modalWindow={!startOnCam} {onGoBack} />

        <gridlayout columns="auto,auto,auto,auto,*,auto,auto" row={2}>
            <IconButton color="white" text={getFlashIcon(flashMode)} tooltip={lc('flash_mode')} on:tap={() => (flashMode = (flashMode + 1) % 3)} />
            <IconButton col={1} color="white" isSelected={torchEnabled} selectedColor={colorPrimary} text="mdi-flashlight" tooltip={lc('torch')} on:tap={switchTorch} />
            <IconButton col={2} color="white" text="mdi-camera-flip" tooltip={lc('toggle_camera')} on:tap={toggleCamera} />

            <IconButton col={3} color="white" isEnabled={cameraOpened} row={2} text="mdi-tune" on:tap={showCameraSettings} />
            {#if startOnCam}
                <IconButton col={5} color="white" row={2} text="mdi-folder" on:tap={() => showDocumentsList()} />
                <IconButton col={6} color="white" row={2} text="mdi-cogs" on:tap={() => showSettings()} />
            {/if}
        </gridlayout>
        <gridlayout
            horizontalAlignment="center"
            marginBottom={5}
            row={1}
            verticalAlignment="bottom"
            visibility={(maxZoom !== 1 || minZoom !== 1) && maxZoom !== minZoom ? 'visible' : 'collapsed'}
            width="70%">
            <slider
                bind:this={zoomSlider}
                backgroundColor={colorBackground}
                maxValue={maxZoom}
                minValue={minZoom}
                thumbColor="transparent"
                value={zoom}
                on:valueChange={onZoomValue}
                on:layoutChanged={(e) => updateFloatZoom(zoom)} />
            <label
                backgroundColor={colorBackground}
                borderColor={colorOutlineVariant}
                borderRadius={20}
                borderWidth={1}
                color={colorOnBackground}
                fontSize={13}
                height={40}
                horizontalAlignment="left"
                isUserInteractionEnabled={false}
                marginLeft={zoomPercentDelta}
                text={Math.round(floatZoom * 10) / 10 + 'x'}
                textAlignment="center"
                verticalTextAlignment="center"
                width={40} />
        </gridlayout>

        <gridlayout columns="60,*,auto,*,60" ios:paddingBottom={30} android:marginBottom={30 + $windowInset.bottom} paddingTop={10} row={3} visibility={QRCodeOnly ? 'collapsed' : 'visible'}>
            {#if !onlyForOCR}
                <IconButton
                    colSpan={2}
                    color="white"
                    horizontalAlignment="left"
                    isSelected={batchMode}
                    marginLeft={16}
                    marginTop={20}
                    rounded={false}
                    selectedColor={colorPrimary}
                    subtitle={lc('batch_mode')}
                    text={batchMode ? 'mdi-image-multiple' : 'mdi-image'}
                    tooltip={lc('batch_mode')}
                    verticalAlignment="center"
                    on:tap={() => (batchMode = !batchMode)} />
            {/if}

            <image
                borderColor="white"
                col={3}
                ios:contextOptions={IMAGE_CONTEXT_OPTIONS}
                decodeWidth={Utils.layout.toDevicePixels(60)}
                height={60}
                horizontalAlignment="center"
                imageRotation={smallImageRotation}
                src={smallImage}
                stretch="aspectFit"
                verticalAlignment="center"
                width={60} />
            <gridlayout col={2} height={70} horizontalAlignment="center" opacity={takingPicture ? 0.6 : 1} verticalAlignment="center" width={70}>
                <canvasView bind:this={takPictureBtnCanvas} class:infinite-rotate={autoScan} on:draw={drawTakePictureBtnBorder}> </canvasView>
                <gridlayout
                    backgroundColor={colorPrimary}
                    borderRadius={27}
                    height={54}
                    horizontalAlignment="center"
                    rippleColor="white"
                    width={54}
                    on:tap={() => takePicture()}
                    on:longPress={() => toggleAutoScan()} />
                <label color="white" fontSize={20} isUserInteractionEnabled={false} text={nbPages + ''} textAlignment="center" verticalAlignment="middle" visibility={nbPages ? 'visible' : 'hidden'} />
            </gridlayout>

            <IconButton
                col={4}
                color="white"
                horizontalAlignment="right"
                marginRight={16}
                text="mdi-check"
                tooltip={lc('finish')}
                verticalAlignment="center"
                visibility={canSaveDoc ? 'visible' : 'hidden'}
                on:tap={() => saveCurrentDocument()} />
        </gridlayout>

        <!-- <image
            bind:this={fullImageView}
            colorMatrix={getColorMatrix(colorType)}
            imageRotation={smallImageRotation}
            rowSpan={4}
            src={smallImage}
            stretch="aspectFit"
            visibility={showingFullScreenImage ? 'visible' : 'hidden'} /> -->
    </gridlayout>
</page>
