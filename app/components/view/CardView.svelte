<script context="module" lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { Img } from '@nativescript-community/ui-image';
    import { SVGView } from '@nativescript-community/ui-svg';
    import { showBottomSheet } from '@nativescript-community/ui-material-bottomsheet/svelte';
    import { confirm } from '@nativescript-community/ui-material-dialogs';
    import { Pager } from '@nativescript-community/ui-pager';
    import { VerticalPosition } from '@nativescript-community/ui-popover';
    import {
        AnimationDefinition,
        Application,
        ApplicationSettings,
        Color,
        ContentView,
        EventData,
        ImageSource,
        ObservableArray,
        Page,
        PageTransition,
        SharedTransition,
        StackLayout,
        View
    } from '@nativescript/core';
    import { AndroidActivityBackPressedEventData } from '@nativescript/core/application';
    import { debounce, throttle } from '@nativescript/core/utils';
    import { create as createImagePicker } from '@nativescript/imagepicker';
    import { showError } from '@shared/utils/showError';
    import { goBack, navigate, showModal } from '@shared/utils/svelte/ui';
    import dayjs from 'dayjs';
    import { QRCodeData, QRCodeSingleData, detectQRCodeFromFile } from 'plugin-nativeprocessor';
    import { onDestroy, onMount } from 'svelte';
    import CActionBar from '~/components/common/CActionBar.svelte';
    import PageIndicator from '~/components/common/PageIndicator.svelte';
    import RotableImageView from '~/components/common/RotableImageView.svelte';
    import SelectedIndicator from '~/components/common/SelectedIndicator.svelte';
    import PdfEdit from '~/components/edit/DocumentEdit.svelte';
    import { lc } from '~/helpers/locale';
    import { colorTheme, isDarkTheme, isEInk, onThemeChanged } from '~/helpers/theme';
    import { Document, ExtraFieldType, OCRDocument, OCRPage } from '~/models/OCRDocument';
    import {
        DocumentDeletedEventData,
        DocumentPageDeletedEventData,
        DocumentPageUpdatedEventData,
        DocumentPagesAddedEventData,
        DocumentUpdatedEventData,
        documentsService
    } from '~/services/documents';
    import { getBarcodeFallbackString, qrcodeService } from '~/services/qrcode';
    import { shortcutService } from '~/services/shortcuts';
    import {
        CARD_RATIO,
        DEFAULT_FORCE_WHITE_BACKGROUND_QRCODE,
        EVENT_DOCUMENT_DELETED,
        EVENT_DOCUMENT_PAGES_ADDED,
        EVENT_DOCUMENT_PAGE_DELETED,
        EVENT_DOCUMENT_PAGE_UPDATED,
        EVENT_DOCUMENT_UPDATED,
        FAB_BUTTON_OFFSET,
        QRCODE_RESIZE_THRESHOLD,
        SETTINGS_FORCE_WHITE_BACKGROUND_QRCODE
    } from '~/utils/constants';
    import { recycleImages } from '~/utils/images';
    import { documentHasPKPassData } from '~/utils/pkpass';
    import {
        detectOCR,
        importAndScanImage,
        importImageFromCamera,
        onBackButton,
        pickColor,
        requestCameraPermission,
        showImagePopoverMenu,
        showPDFPopoverMenu,
        showPopoverMenu,
        showSnack,
        transformPages
    } from '~/utils/ui';
    import { colors, fontScale, fonts, hasCamera, isLandscape, onFontScaleChanged, screenHeightDips, screenWidthDips, windowInset } from '~/variables';
    import EditNameActionBar from '~/components/common/EditNameActionBar.svelte';
    import IconButton from '@shared/components/IconButton.svelte';
    import ListItemAutoSize from '@shared/components/ListItemAutoSize.svelte';
    import PKPassView from '~/components/pkpass/PKPassView.svelte';

    const rowMargin = 8;
    // -10 show just a bit of the one hidden on the right
    const colWidth = screenWidthDips / 2 - 10;
    const itemHeight = (colWidth - 2 * rowMargin) * CARD_RATIO + 2 * rowMargin;
    interface Item {
        page: OCRPage;
        selected: boolean;
        index: number;
    }
    const QRCODES_TYPE = 'qrcodes';

    type QRCodeItem = QRCodeSingleData & { svg: string; pageIndex: number; pageQRCodeIndex: number; color: string; id: string; error?: string };
</script>

<script lang="ts">
    import SliderPopover from '../common/SliderPopover.svelte';
    import { TextField } from '@nativescript-community/ui-material-textfield';
    import { Label } from '@nativescript-community/ui-label';

    let { colorBackground, colorError, colorOnBackground, colorOnSurfaceVariant, colorOutline, colorSurface, colorSurfaceContainerHigh, colorTertiary } = $colors;
    $: ({ colorBackground, colorError, colorOnBackground, colorOnSurfaceVariant, colorOutline, colorSurface, colorSurfaceContainerHigh, colorTertiary } = $colors);

    const forceWhiteBackgroundForQRCode = ApplicationSettings.getBoolean(SETTINGS_FORCE_WHITE_BACKGROUND_QRCODE, DEFAULT_FORCE_WHITE_BACKGROUND_QRCODE);

    export let document: OCRDocument;
    export let transitionOnBack = true;
    let editingTitle = false;
    let editingUpdates: Partial<Document> = {};
    let topBackgroundColor = computeTopBackgroundColor();
    let qrcodes: QRCodeItem[];
    let editableQRcodes: ObservableArray<QRCodeItem>;
    let currentQRCodeIndex = 0;
    let page: NativeViewElementNode<Page>;
    let collectionView: NativeViewElementNode<CollectionView>;
    let collectionViewExtras: NativeViewElementNode<CollectionView>;
    let fabHolder: NativeViewElementNode<StackLayout>;
    let pager: NativeViewElementNode<Pager>;
    let statusBarStyle;
    // set hasQRCodes as soon as possible to ensure the layout is correct and does not "jump"
    let hasQRCodes = document.pages.some((p) => p.qrcode?.length > 0);
    $: canBeExportedToPDF = document.pages.some((p) => p.pkpass || p.imagePath);
    $: hasImages = document.pages.some((p) => p.imagePath);
    // Check if any page in the document has PKPass data
    $: isPKPassDocument = documentHasPKPassData(document);
    const PKPASS_TYPE = 'pkpass';

    function computeTopBackgroundColor() {
        const color = isEInk
            ? 'white'
            : (editingUpdates?.extra?.color ??
              (typeof document.extra?.color === 'string' ? document.extra?.color : undefined) ??
              document.pages[0]?.extra?.color ??
              document.pages[0]?.colors?.[1] ??
              colorBackground);
        return color;
    }

    $: statusBarStyle = new Color(topBackgroundColor).getBrightness() < 145 ? 'dark' : 'light';

    let editing = false;

    onThemeChanged(() => {
        DEV_LOG && console.log('onThemeChanged', $colors.colorOnBackground);
        updateQRCodes();
    });

    function getExtraItems() {
        const result = [];

        let extra = document.extra || {};
        if (typeof extra.color === 'string') {
            if (editing && colorTheme !== 'eink') {
                result.push({ type: 'color' });
            }
            extra = { ...extra };
            delete extra.color;
        }

        // Add PKPass views for pages that have PKPass data (not in editing mode)
        if (!editing) {
            document.pages.forEach((page, index) => {
                if (page.pkpass) {
                    result.push({
                        type: PKPASS_TYPE,
                        pkpass: page.pkpass,
                        pageIndex: index
                    });
                }
            });
        }

        // if (editing) {
        //     // add qrcodes
        //     for (let index = 0; index < qrcodes.length; index++) {
        //         const qrcode = qrcodes[index];
        //         result.push({
        //             type: 'qrcode',
        //             ...qrcode
        //         });
        //     }
        // } else if (hasQRCodes) {
        if (qrcodes && editing) {
            editableQRcodes = new ObservableArray(JSON.parse(JSON.stringify(qrcodes)));
        } else {
            editableQRcodes = null;
        }
        if (qrcodes || editableQRcodes) {
            result.push({
                type: QRCODES_TYPE,
                qrcodes: editableQRcodes ?? qrcodes
            });
        }
        // }
        Object.keys(extra).forEach((k) => {
            // Skip the pkpass flag as it's already handled
            if (k !== 'pkpass') {
                result.push({
                    name: k,
                    extra: { ...(extra[k] as any), name: k }
                });
            }
        });
        return result;
    }
    // $: {
    const pages = document.getObservablePages();
    let items = pages.map((page, index) => ({ selected: false, page, index, document })) as ObservableArray<Item>;
    const extraItems: ObservableArray<any> = new ObservableArray(getExtraItems());
    // pages.on('change', (event)=>{
    //     switch(event.action) {
    //         case ChangeType.Splice:
    //         items.splice(event.index, event.removed.length, event.added)
    //     }
    // });
    // }
    // function onImageUpdated (event: any)  {
    //         const index = event.data.index;
    //         const current = document.pages.getItem(index)
    //         current.config =
    //         const data = {
    //             image: document.images.getItem(index).page,
    //             config: document.imagesConfig[index],
    //         };
    //         console.log('onImageUpdated', event.data.index, document, data);
    //         document.pages.setItem(index, data);
    //     }

    let qrcodeImages: { [k: string]: ImageSource } = {};
    function clearQRCodeImages(timeout = 0) {
        const toClear = Object.values(qrcodeImages);
        qrcodeImages = {};
        setTimeout(() => {
            recycleImages(toClear);
        }, timeout);
    }
    let qrcodeColor;
    let qrcodeBackgroundColor;

    function updateQRCodeColors() {
        qrcodeColor = forceWhiteBackgroundForQRCode ? 'black' : $colors.colorOnBackground;
        // qrcodeBackgroundColor = 'red';
        qrcodeBackgroundColor = forceWhiteBackgroundForQRCode ? (isDarkTheme() ? $colors.colorOnBackground : $colors.colorBackground) : $colors.colorBackground;
    }

    async function updateQRCodes(updateList = true) {
        try {
            updateQRCodeColors();
            const color = qrcodeColor;
            const newQrCodes = [];
            for (let index = 0; index < document.pages.length; index++) {
                const page = document.pages[index];
                if (page.qrcode?.length) {
                    for (let j = 0; j < page.qrcode.length; j++) {
                        const qr = page.qrcode[j];
                        newQrCodes.push({
                            ...qr,
                            color,
                            svg: await getQRCodeImage(qr, color),
                            pageQRCodeIndex: j,
                            pageIndex: index,
                            id: Date.now().toString(36) + Math.random().toString(36).substring(2)
                        });
                    }
                }
            }
            qrcodes = newQrCodes;
            hasQRCodes = qrcodes?.length > 0;
            if (updateList && !editing) {
                if (hasQRCodes) {
                    if (extraItems.getItem(0)?.type !== QRCODES_TYPE) {
                        extraItems.splice(0, 0, { type: QRCODES_TYPE, qrcodes });
                    } else {
                        extraItems.setItem(0, { type: QRCODES_TYPE, qrcodes });
                    }
                } else {
                    if (extraItems.getItem(0)?.type === QRCODES_TYPE) {
                        extraItems.splice(0, 1);
                    }
                }
            }
            // qrcodes = document.pages.reduce((acc, page) => acc.concat(page.qrcode?.map((qr) => ({ ...qr, color, svg: await getQRCodeImage(qr, color) })) || []), []);
            //we dont recycle images, it will be done in onDestroy
        } catch (error) {
            showError(error);
        }
    }
    // updateQRCodes();

    async function getQRCodeImage(qrcode, color) {
        try {
            return qrcodeService.getQRCodeSVG(qrcode, screenWidthDips, color);
        } catch (error) {
            console.error(error);
        }
    }
    function onSelectedIndex(event) {
        currentQRCodeIndex = event.object.selectedIndex;
    }
    function geAllPages() {
        const selected: { page: OCRPage; document: OCRDocument }[] = [];
        items.forEach((d, index) => {
            selected.push({ page: d.page, document });
        });
        return selected;
    }
    function getSelectedPages() {
        const selected: { page: OCRPage; document: OCRDocument }[] = [];
        items.forEach((d, index) => {
            if (d.selected) {
                selected.push({ page: d.page, document });
            }
        });
        return selected;
    }
    function getSelectedPagesWithData() {
        const selected: { page: OCRPage; pageIndex: number; document: OCRDocument }[] = [];
        items.forEach((d, index) => {
            if (d.selected) {
                selected.push({ page: d.page, document, pageIndex: index });
            }
        });
        return selected;
    }
    async function showPDFPopover(event) {
        try {
            const pages = nbSelected > 0 ? getSelectedPages() : document.pages.map((p) => ({ page: p, document }));
            await showPDFPopoverMenu({ pages, document, anchor: event.object, documents: [document] });
        } catch (err) {
            showError(err);
        }
    }
    async function showImageExportPopover(event) {
        return showImagePopoverMenu(getSelectedPages(), event.object);
    }

    async function importDocument(importPDFs = true) {
        try {
            await importAndScanImage({ document, importPDFs, canGoToView: false });
        } catch (error) {
            showError(error);
        }
    }
    async function deleteDoc() {
        try {
            const result = await confirm({
                title: lc('delete'),
                message: lc('confirm_delete'),
                okButtonText: lc('delete'),
                cancelButtonText: lc('cancel')
            });
            if (result) {
                try {
                    await documentsService.deleteDocuments([document]);
                    items = null;
                    goBack({
                        // null is important to say no transition! (override enter transition)
                        transition: null
                    });
                } catch (err) {
                    console.error(err, err.stack);
                }
            }
        } catch (err) {
            showError(err);
        }
    }

    let nbSelected = 0;
    function selectItem(item: Item) {
        if (!item.selected) {
            items.some((d, index) => {
                if (d === item) {
                    nbSelected++;
                    d.selected = true;
                    items.setItem(index, d);
                    return true;
                }
            });
        }
    }
    function unselectItem(item: Item) {
        if (item.selected) {
            items.some((d, index) => {
                if (d === item) {
                    nbSelected--;
                    d.selected = false;
                    items.setItem(index, d);
                    return true;
                }
            });
        }
    }
    function unselectAll() {
        if (items) {
            nbSelected = 0;
            items.splice(0, items.length, ...items.map((i) => ({ ...i, selected: false })));
        }
    }
    function selectAll() {
        if (items) {
            items.splice(0, items.length, ...items.map((i) => ({ ...i, selected: true })));
            nbSelected = items.length;
        }
    }
    function toggleSelection(item: Item) {
        if (item.selected) {
            unselectItem(item);
        } else {
            selectItem(item);
        }
    }
    let inEditMode = false;
    function switchEditMode() {
        inEditMode = !inEditMode;
        collectionView?.nativeElement?.refreshVisibleItems();
    }
    let longPressTimer: NodeJS.Timeout;
    let currentLongPressItem: Item;
    let dragStarted = false;
    function onItemLongPress(item: Item, event?) {
        if (inEditMode) {
            startDragging(item, event);
            return;
        }
        if (dragStarted) {
            return;
        }
        if (nbSelected > 0) {
            toggleSelection(item);
            return;
        }

        if (longPressTimer) {
            clearTimeout(longPressTimer);
        }

        currentLongPressItem = item;
        dragStarted = false;

        longPressTimer = setTimeout(() => {
            if (!dragStarted && currentLongPressItem) {
                toggleSelection(item);
            }
            currentLongPressItem = null;
        }, 200);
    }
    async function onPan(item: Item, event) {
        if (!currentLongPressItem) {
            return;
        }

        if (event.state === 2 && !dragStarted) {
            dragStarted = true;

            if (longPressTimer) {
                clearTimeout(longPressTimer);
                longPressTimer = null;
            }
            startDragging(item, event);

            currentLongPressItem = null;
        } else if (event.state === 5 || event.state === 3) {
            if (longPressTimer) {
                clearTimeout(longPressTimer);
                longPressTimer = null;
            }
            dragStarted = false;
            currentLongPressItem = null;
        }
    }
    async function onTouch(item: Item, event?) {
        if (!inEditMode) {
            return;
        }
        switch (event.action) {
            case 'down': {
                startDragging(item, event);
                break;
            }
        }
    }
    async function onItemTap(item: Item) {
        try {
            if (nbSelected > 0) {
                toggleSelection(item);
            } else if (item.page.imagePath) {
                const index = items.findIndex((p) => p.page === item.page);
                navigate({
                    page: PdfEdit,
                    transition:
                        __ANDROID__ && !CARD_APP
                            ? SharedTransition.custom(new PageTransition(300, undefined, 10), {
                                  pageStart: {
                                      sharedTransitionTags: {
                                          [`document_${document.id}_${item.page.id}`]: {}
                                      }
                                  }
                              })
                            : undefined,
                    props: {
                        document,
                        startPageIndex: index
                    }
                });
            }
        } catch (error) {
            showError(error);
        }
    }
    function onGoBack() {
        if (editingTitle) {
            editingTitle = false;
        } else {
            goBack(
                transitionOnBack
                    ? undefined
                    : {
                          transition: null
                      }
            );
        }
    }
    const onAndroidBackButton = (data: AndroidActivityBackPressedEventData) =>
        onBackButton(page?.nativeView, () => {
            data.cancel = true;
            if (inEditMode) {
                switchEditMode();
            } else if (nbSelected > 0) {
                unselectAll();
            } else {
                onGoBack();
            }
        });
    async function fullscreenSelectedPages() {
        const component = (await import('~/components/FullScreenImageViewer.svelte')).default;
        navigate({
            page: component,
            // transition: __ANDROID__ ? SharedTransition.custom(new PageTransition(300, undefined, 10), {}) : undefined,
            props: {
                images: getSelectedPages()
                    .filter((p) => p.page.imagePath)
                    .map((p) => ({
                        // sharedTransitionTag: `document_${doc.id}_${page.id}`,
                        name: document.name,
                        image: p.page.imagePath,
                        ...p
                    })),
                startPageIndex: 0
            }
        });
    }
    async function deleteSelectedPages() {
        if (nbSelected > 0) {
            try {
                const result = await confirm({
                    title: lc('delete'),
                    message: lc('confirm_delete_pages', nbSelected),
                    okButtonText: lc('delete'),
                    cancelButtonText: lc('cancel')
                });
                if (result) {
                    const indexes = [];
                    items.forEach((d, index) => {
                        if (d.selected) {
                            indexes.push(index);
                        }
                    });
                    let minDeleteIndex = Number.MAX_SAFE_INTEGER;
                    for (let index = 0; index < indexes.length; index++) {
                        const toRemoveIndex = indexes[index];
                        minDeleteIndex = Math.min(minDeleteIndex, toRemoveIndex);
                        await document.deletePage(toRemoveIndex);

                        items.splice(toRemoveIndex, 1);
                    }
                    refreshCollectionView();
                    nbSelected = 0;
                    return true;
                }
            } catch (error) {
                showError(error);
            }
        }
    }

    function getImageView(index: number) {
        return collectionView?.nativeView?.getViewForItemAtIndex(index)?.getViewById<Img>('imageView');
    }

    function onPagesAdded(event: DocumentPagesAddedEventData) {
        if (event.doc.id !== document.id) {
            return;
        }
        document = event.doc;
        try {
            if (items) {
                const length = items.length;
                items.push(...event.pages.map((page, index) => ({ page, selected: false, index: length })));
            }
            updateQRCodes();
        } catch (error) {
            showError(error, { silent: true });
        }
    }
    function onDocumentPageUpdated(event: DocumentPageUpdatedEventData) {
        if (event.doc.id !== document.id) {
            return;
        }
        document = event.doc;
        const index = event.pageIndex;
        const current = items.getItem(index);
        if (index === 0) {
            topBackgroundColor = computeTopBackgroundColor();
        }
        if (current) {
            const page = document.getObservablePages().getItem(index);
            DEV_LOG && console.log('view onDocumentPageUpdated', index, event.imageUpdated, current.index);
            items.setItem(index, { ...current, page });
            if (!!event.imageUpdated) {
                const imageView = getImageView(index);
                DEV_LOG && console.log('view onDocumentPageUpdated update image', imageView);
                if (imageView) {
                    imageView?.updateImageUri();
                } else if (__IOS__) {
                    collectionView?.nativeElement?.refreshVisibleItems();
                }
            }
        }
    }
    function onDocumentPageDeleted(event: DocumentPageDeletedEventData) {
        if (event.doc.id !== document.id) {
            return;
        }
        document = event.doc;
        const index = event.pageIndex;
        items.splice(index, 1);
        items.forEach((item, index) => (item.index = index + 1));
        updateQRCodes();
    }
    function onDocumentsDeleted(event: DocumentDeletedEventData) {
        if (event.documents.indexOf(document) !== -1) {
            goBack({
                // null is important to say no transition! (override enter transition)
                transition: null
            });
        }
    }
    function onDocumentUpdated(event: DocumentUpdatedEventData) {
        if (document.id === event.doc.id) {
            document = event.doc;
            if (!editing) {
                refreshExtraItems();
            }
        }
    }

    function onSnackMessageAnimation({ animationArgs }: EventData & { animationArgs: AnimationDefinition[] }) {
        if (fabHolder) {
            const snackAnimation = animationArgs[0];
            animationArgs.push({
                target: fabHolder.nativeView,
                translate: { x: 0, y: snackAnimation.translate.y === 0 ? -70 : 0 },
                duration: snackAnimation.duration
            });
        }
    }
    // function onOrientationChanged(event: OrientationChangedEventData) {
    //     orientation = event.newValue;
    //     refreshCollectionView();
    //     // }, 1000);
    // }
    onMount(() => {
        updateQRCodes();
        Application.on('snackMessageAnimation', onSnackMessageAnimation);
        // Application.on('orientationChanged', onOrientationChanged);
        if (__ANDROID__) {
            Application.android.on(Application.android.activityBackPressedEvent, onAndroidBackButton);
        }
        documentsService.on(EVENT_DOCUMENT_UPDATED, onDocumentUpdated);
        documentsService.on(EVENT_DOCUMENT_DELETED, onDocumentsDeleted);
        documentsService.on(EVENT_DOCUMENT_PAGE_DELETED, onDocumentPageDeleted);
        documentsService.on(EVENT_DOCUMENT_PAGE_UPDATED, onDocumentPageUpdated);
        documentsService.on(EVENT_DOCUMENT_PAGES_ADDED, onPagesAdded);

        shortcutService.updateShortcuts(document);
        documentsService.documentRepository.incrementUsage(document).catch(() => {});
        // refresh();
    });
    onDestroy(() => {
        clearQRCodeImages();
        Application.off('snackMessageAnimation', onSnackMessageAnimation);
        // Application.off('orientationChanged', onOrientationChanged);
        if (__ANDROID__) {
            Application.android.off(Application.android.activityBackPressedEvent, onAndroidBackButton);
        }
        documentsService.off(EVENT_DOCUMENT_UPDATED, onDocumentUpdated);
        documentsService.off(EVENT_DOCUMENT_DELETED, onDocumentsDeleted);
        documentsService.off(EVENT_DOCUMENT_PAGE_DELETED, onDocumentPageDeleted);
        documentsService.off(EVENT_DOCUMENT_PAGE_UPDATED, onDocumentPageUpdated);
        documentsService.off(EVENT_DOCUMENT_PAGES_ADDED, onPagesAdded);
    });

    function startDragging(item: Item, event) {
        const index = items.findIndex((p) => p.page === item.page);
        collectionView?.nativeElement.startDragging(index, event.getActivePointers()[0]);
    }
    async function onItemReordered(e) {
        DEV_LOG && console.log('onItemReordered');
        (e.view as ContentView).content.opacity = 1;
        try {
            if (e.index !== e.data.targetIndex) {
                await document.movePage(e.index, e.data.targetIndex);
            }
            topBackgroundColor = computeTopBackgroundColor();
            statusBarStyle = new Color(topBackgroundColor).getBrightness() < 128 ? 'dark' : 'light';
            updateQRCodes();
        } catch (error) {
            showError(error);
        }
    }
    async function onItemReorderStarting(e) {
        (e.view as ContentView).content.opacity = 0.6;
    }

    function refreshCollectionView() {
        collectionView?.nativeView?.refresh();
        collectionViewExtras?.nativeView?.refresh();
    }
    function refreshQRCodePager() {
        // pager?.nativeView.refresh();
    }
    onThemeChanged(refreshCollectionView);
    onFontScaleChanged(refreshCollectionView);

    async function onQRCodeTap() {
        try {
            if (editing) {
                return;
            }
            await qrcodeService.showQRCode([...pages], document, currentQRCodeIndex);
        } catch (error) {
            showError(error);
        }
    }
    async function showOptions(event) {
        if (nbSelected > 0) {
            const pages = getSelectedPagesWithData();
            // Filter options based on PKPass document
            const allOptions = [
                { id: 'share', name: lc('share_images'), icon: 'mdi-share-variant' },
                { id: 'fullscreen', name: lc('show_fullscreen_images'), icon: 'mdi-fullscreen' }
            ] as any[];

            if (!isPKPassDocument) {
                if (pages.some((p) => p.page.imagePath)) {
                    allOptions.push(
                        ...[
                            { id: 'transform', name: lc('transform_images'), icon: 'mdi-auto-fix' },
                            { id: 'ocr', name: lc('ocr_document'), icon: 'mdi-text-recognition' },
                            { id: 'qrcode', name: lc('detect_qrcode'), icon: 'mdi-qrcode-scan' }
                        ]
                    );
                }
            }
            allOptions.push(...[{ id: 'delete', name: lc('delete'), icon: 'mdi-delete', color: colorError }]);

            // For PKPass documents, only allow share and fullscreen
            const options = new ObservableArray(allOptions);
            return showPopoverMenu({
                options,
                anchor: event.object,
                vertPos: VerticalPosition.BELOW,

                onClose: async (item) => {
                    try {
                        let result;
                        DEV_LOG && console.log('item.id', item.id);
                        switch (item.id) {
                            case 'share':
                                result = await showImageExportPopover(event);
                                if (result) {
                                    unselectAll();
                                }
                                break;
                            case 'fullscreen':
                                await fullscreenSelectedPages();
                                unselectAll();
                                break;
                            case 'ocr':
                                result = await detectOCR({ pages: getSelectedPagesWithData() });
                                if (result) {
                                    unselectAll();
                                }
                                break;
                            case 'qrcode':
                                let found = false;
                                await Promise.all(
                                    getSelectedPagesWithData().map((page) =>
                                        qrcodeService.detectQRcode(document, page.pageIndex).then((r) => {
                                            found = found || r?.length > 0;
                                        })
                                    )
                                );
                                if (!found) {
                                    showSnack({ message: lc('no_qrcode_found') });
                                }

                                unselectAll();
                                break;
                            case 'delete':
                                result = await deleteSelectedPages();
                                if (result) {
                                    unselectAll();
                                }
                                break;
                            case 'transform':
                                result = await transformPages({ pages: getSelectedPagesWithData() });
                                if (result) {
                                    unselectAll();
                                }
                                break;
                        }
                    } catch (error) {
                        showError(error);
                    }
                }
            });
        } else {
            // Filter options based on PKPass document

            const allOptions = [
                { id: 'rename', name: lc('rename'), icon: 'mdi-rename' },
                { id: 'favorite', name: lc('toggle_favorite'), icon: document.favorite === 1 ? 'mdi-star' : 'mdi-star-outline' }
            ] as any[];

            if (!isPKPassDocument) {
                if (hasImages) {
                    allOptions.push(
                        ...[
                            { id: 'select_all', name: lc('select_all'), icon: 'mdi-select-all' },
                            { id: 'reorder', name: lc('reorder_pages'), icon: 'mdi-reorder-horizontal' },
                            { id: 'transform', name: lc('transform_images'), icon: 'mdi-auto-fix' },
                            { id: 'ocr', name: lc('ocr_document'), icon: 'mdi-text-recognition' }
                        ]
                    );
                }
            } else {
                allOptions.push({ id: 'share', name: lc('share_images'), icon: 'mdi-share-variant' });
            }
            allOptions.push(...[{ id: 'delete', name: lc('delete'), icon: 'mdi-delete', color: colorError }]);

            // For PKPass documents, only allow delete (of the whole document)
            const options = new ObservableArray(allOptions);
            return showPopoverMenu({
                options,
                anchor: event.object,
                vertPos: VerticalPosition.BELOW,

                onClose: async (item) => {
                    switch (item.id) {
                        case 'rename':
                            editingTitle = true;
                            break;
                        case 'favorite':
                            await document.save({ favorite: document.favorite === 1 ? 0 : 1 }, false, true);
                            break;
                        case 'share':
                            await showImagePopoverMenu(geAllPages(), event.object);
                            break;
                        case 'select_all':
                            selectAll();
                            break;
                        case 'ocr':
                            await detectOCR({ documents: [document] });
                            unselectAll();
                            break;
                        case 'transform':
                            await transformPages({ documents: [document] });
                            unselectAll();
                            break;
                        case 'delete':
                            await deleteDoc();
                            break;
                        case 'reorder':
                            unselectAll();
                            switchEditMode();
                            break;
                    }
                }
            });
        }
    }
    async function onAddButton() {
        try {
            const OptionSelect = (await import('@shared/components/OptionSelect.svelte')).default;
            const rowHeight = 58;
            const options = (
                $hasCamera
                    ? [
                          {
                              id: 'camera',
                              name: lc('add_from_camera'),
                              icon: 'mdi-camera'
                          }
                      ]
                    : []
            )
                .concat([
                    {
                        id: 'import',
                        name: lc('import_from_file'),
                        icon: 'mdi-file-document-plus-outline'
                    },
                    {
                        id: 'import_image',
                        name: lc('import_from_image'),
                        icon: 'mdi-image-plus-outline'
                    }
                ])
                .concat([
                    {
                        id: 'add_qrcode_camera',
                        name: lc('add_qrcode_camera'),
                        icon: 'mdi-qrcode-scan'
                    },
                    {
                        id: 'add_qrcode_file',
                        name: lc('add_qrcode_file'),
                        icon: 'mdi-qrcode-plus'
                    },
                    {
                        id: 'add_qrcode_manual',
                        name: lc('add_qrcode_manual'),
                        icon: 'mdi-qrcode-plus'
                    }
                ]);
            const height = Math.min(rowHeight * options.length, 400);
            const option = await showBottomSheet({
                parent: this,
                view: OptionSelect,
                peekHeight: height,
                ignoreTopSafeArea: true,
                props: {
                    rowHeight,
                    height,
                    options
                }
            });
            DEV_LOG && console.log('on add option', option);
            let found = false;
            if (option) {
                switch (option.id) {
                    case 'camera':
                        await importImageFromCamera({ document, inverseUseSystemCamera: false });
                        break;
                    case 'import':
                        await importDocument();
                        break;
                    case 'import_image':
                        await importAndScanImage({ canGoToView: false, forceGalleryPick: true, document, importPDFs: false });
                        break;
                    case 'add_qrcode_camera':
                        await requestCameraPermission();
                        const Camera = (await import('~/components/camera/Camera.svelte')).default;
                        const qrcodes: QRCodeData = await showModal({
                            page: Camera,
                            fullscreen: true,
                            props: {
                                QRCodeOnly: true
                            }
                        });
                        if (qrcodes?.length) {
                            //we add the qrcode to the first page (less risk of it being removed with the page)
                            await addQRCodes(qrcodes);
                            found = true;
                        } else {
                            showSnack({ message: lc('no_qrcode_found') });
                        }
                        DEV_LOG && console.log('qrcodes', qrcodes);
                        break;
                    case 'add_qrcode_file':
                        try {
                            const data = await createImagePicker({
                                mediaType: 1,
                                mode: 'multiple'
                            }).present();
                            const qrcodes = (
                                await Promise.all(
                                    data.map((d) =>
                                        detectQRCodeFromFile(d.path, {
                                            resizeThreshold: QRCODE_RESIZE_THRESHOLD
                                        })
                                    )
                                )
                            ).filter((q) => !!q);
                            if (qrcodes?.length) {
                                //we add the qrcode to the first page (less risk of it being removed with the page)
                                await addQRCodes(qrcodes.flat());
                                found = true;
                            } else {
                                showSnack({ message: lc('no_qrcode_found') });
                            }
                        } catch (error) {
                            //cancel will throw, we want to ignore it!
                        }
                        break;
                    case 'add_qrcode_manual': {
                        const result = await qrcodeService.createQRCode();
                        if (result?.text?.length) {
                            await addQRCodes([result]);
                        }
                        break;
                    }
                }
                await updateQRCodes();
                if (found) {
                    setTimeout(() => {
                        pager?.nativeElement?.scrollToIndexAnimated(qrcodes?.length - 1, true);
                    }, 10);
                }
            }
        } catch (error) {
            showError(error);
        }
    }

    function addQRCodes(qrcodes: QRCodeData, pageIndex = 0) {
        //we add the qrcode to the first page (less risk of it being removed with the page)
        return document.updatePage(pageIndex, {
            qrcode: (document.pages[pageIndex].qrcode || []).concat(
                qrcodes.map((q) => ({
                    text: q.text,
                    format: q.format,
                    position: q.position
                }))
            )
        });
    }

    async function addOrEditExtraField(item?) {
        try {
            const ExtraFieldPicker = (await import('~/components/widgets/ExtraFieldPicker.svelte')).default;
            const result = await showBottomSheet({
                parent: page,
                skipCollapsedState: true,
                view: ExtraFieldPicker,
                props: { editing: !!item, ...(item?.extra || {}) }
            });
            if (result) {
                DEV_LOG && console.log('addExtraField', result);
                const { name, ...others } = result;
                editingUpdates.extra = editingUpdates.extra || {};
                editingUpdates.extra[name] = others;
                if (item) {
                    const index = extraItems.indexOf(item);
                    extraItems.setItem(index, {
                        name: result.name,
                        extra: result
                    });
                } else {
                    extraItems.splice(extraItems.length - 1, 0, {
                        name: result.name,
                        extra: result
                    });
                }
            }
        } catch (error) {
            showError(error);
        }
    }

    async function showFullscreen() {
        const component = (await import('~/components/FullScreenImageViewer.svelte')).default;
        navigate({
            page: component,
            // transition: __ANDROID__ ? SharedTransition.custom(new PageTransition(300, undefined, 10), {}) : undefined,
            props: {
                images: document.pages.map((page, index) => ({
                    sharedTransitionTag: `document_${document.id}_${page.id}`,
                    name: document.name,
                    image: page.imagePath,
                    ...page
                })),
                startPageIndex: 0
            }
        });
    }
    function refreshExtraItems() {
        extraItems.splice(0, extraItems.length, ...getExtraItems());
    }
    async function startEdit() {
        if (editing === false) {
            editing = true;
            editingTitle = true;
            refreshExtraItems();
        }
    }
    async function cancelEdit() {
        if (editing === true) {
            editingUpdates = {};
            editing = false;
            qrcodesToRemove = [];
            qrcodesToUpdate = [];
            refreshExtraItems();
            topBackgroundColor = computeTopBackgroundColor();
        }
    }

    async function saveEdit() {
        try {
            DEV_LOG && console.log('saveEdit');
            if (editing || editingTitle) {
                let hasChanged = false;
                if (qrcodesToRemove.length) {
                    DEV_LOG && console.log('qrcodesToRemove', qrcodesToRemove);
                    for (let index = 0; index < qrcodesToRemove.length; index++) {
                        const qrcode = qrcodes.find((qr) => qr.id === qrcodesToRemove[index]);
                        const pageIndex = qrcode?.pageIndex;
                        const pageqrcodes = document.pages[pageIndex]?.qrcode;
                        if (pageqrcodes && qrcode?.pageQRCodeIndex >= 0 && qrcode.pageQRCodeIndex < pageqrcodes.length) {
                            pageqrcodes.splice(qrcode.pageQRCodeIndex, 1);
                            await document.updatePage(
                                pageIndex,
                                {
                                    qrcode: pageqrcodes
                                },
                                false,
                                false
                            );
                        }
                    }
                    hasChanged = true;
                    await updateQRCodes(false);
                }
                if (qrcodesToUpdate.length) {
                    DEV_LOG && console.log('qrcodesToUpdate', JSON.stringify(qrcodesToUpdate));
                    for (let index = 0; index < qrcodesToUpdate.length; index++) {
                        const toUpdate = qrcodesToUpdate[index];
                        const qrcode = qrcodes.find((qr) => qr.id === toUpdate.id);
                        const pageIndex = qrcode?.pageIndex;
                        const pageqrcodes = document.pages[pageIndex]?.qrcode;
                        if (pageqrcodes && qrcode?.pageQRCodeIndex >= 0 && qrcode.pageQRCodeIndex < pageqrcodes.length) {
                            const { color, error, id, pageIndex, pageQRCodeIndex, svg, ...toSave } = toUpdate;
                            pageqrcodes.splice(qrcode.pageQRCodeIndex, 1, toSave);
                            await document.updatePage(
                                pageIndex,
                                {
                                    qrcode: pageqrcodes
                                },
                                false,
                                false
                            );
                        }
                    }
                    hasChanged = true;
                    await updateQRCodes(false);
                }
                DEV_LOG && console.log('saveEdit', 'qrcodesToRemove done');
                const { extra, ...updates } = editingUpdates;
                hasChanged = Object.keys(updates).length > 0;
                if (extra) {
                    updates['extra'] = document.extra || {};
                    const keys = Object.keys(extra);
                    if (keys.length) {
                        keys.forEach((k) => {
                            if (extra[k] === null) {
                                delete updates['extra'][k];
                            } else {
                                updates['extra'][k] = extra[k];
                            }
                        });
                        hasChanged = true;
                    }
                }
                if (hasChanged) {
                    await document.save(updates, true);
                }
                DEV_LOG && console.log('saveEdit', 'document saved');
                editingUpdates = {};
                editing = false;
                refreshExtraItems();
            }
        } catch (error) {
            showError(error);
        }
    }

    function selectTemplate(item, index, items) {
        if (item.type) {
            if (item.type === 'prompt' || item.type === 'slider') {
                return 'default';
            }
            return item.type;
        }
        if (item.icon) {
            return 'leftIcon';
        }
        return 'default';
    }

    function updateExtraItem(item, key = 'key') {
        const index = extraItems.findIndex((it) => it[key] === item[key]);
        if (index !== -1) {
            extraItems.setItem(index, item);
        }
    }
    async function changeColor(item, event) {
        try {
            const currentColor = topBackgroundColor;
            const newColor = await pickColor(currentColor, { anchor: event.object });
            if (newColor) {
                editingUpdates.extra = editingUpdates.extra || {};
                topBackgroundColor = editingUpdates.extra.color = newColor.hex;
                updateExtraItem(item, 'type');
            }
        } catch (error) {
            showError(error);
        }
    }
    function onEditActionBarGoBack() {
        cancelEdit();
    }
    function onEditActionBarSave(newTitle) {
        editingUpdates.name = newTitle;
        saveEdit();
    }
    async function onExtraItemTap(item, event) {
        try {
            switch (item.id) {
                case 'add_extra_field':
                    await addOrEditExtraField();
                    break;
            }
        } catch (error) {
            showError(error);
        }
    }
    async function deleteExtraField(item, event) {
        DEV_LOG && console.log('deleteExtraField', item, !!editingUpdates.extra);
        editingUpdates.extra = editingUpdates.extra || {};
        // we set it to null so that we know we have to remove from document on save
        editingUpdates.extra[item.name] = null;
        const index = extraItems.indexOf(item);
        DEV_LOG && console.log('deleteExtraField1', index, item);
        if (index !== -1) {
            extraItems.splice(index, 1);
        }
    }
    let qrcodesToRemove: string[] = [];
    let qrcodesToUpdate: QRCodeItem[] = [];
    async function deleteCurrentQRCode(item: QRCodeItem, event) {
        try {
            const qrcode = item;
            const pageIndex = qrcode?.pageIndex ?? -1;

            DEV_LOG && console.log('deleteCurrentQRCode', pageIndex, qrcode);
            if (pageIndex !== -1) {
                qrcodesToRemove.push(qrcode.id);

                const index = editableQRcodes?.indexOf(item) ?? -1;
                if (index !== -1) {
                    editableQRcodes.splice(index, 1);
                }
            }
        } catch (error) {
            showError(error);
        }
    }

    function formatItemValue(item) {
        if (item.extra) {
            switch (item.extra.type) {
                case ExtraFieldType.Date:
                    return dayjs(item.extra.value).format(item.extra.format || 'LL');
                default:
                    return item.extra.value;
            }
        }
    }
    async function onExtraFieldTap(item, event) {
        DEV_LOG && console.log('onExtraFieldTap', item);
        if (editing) {
            addOrEditExtraField(item);
        }
    }
    function getItemBackgroundColor(item) {
        return isEInk ? null : (item.page?.extra?.color ?? item.page.colors?.[0] ?? document.extra?.color);
    }
    function getItemLabelColor(item) {
        return isEInk ? null : new Color(item.page.colors?.[0] || document.extra?.color).getBrightness() < 145 ? 'white' : 'black';
    }

    const setQRCodeScale = debounce((qrcode: QRCodeItem, scale10: number, event) => {
        const newScale = scale10 / 10;
        if (newScale === qrcode.scale) {
            return;
        }
        qrcode.scale = newScale;
        const index = qrcodesToUpdate.findIndex((qr) => qr.id === qrcode.id) ?? -1;
        if (index !== -1) {
            qrcodesToUpdate.splice(index, 1, qrcode);
        } else {
            qrcodesToUpdate.push(qrcode);
        }

        // we dont want to update the pager item becasue it would loose tf focus and look weird, instead we update manually
        updateQRCodeView(qrcode, event.object.parent.parent.parent);
    }, 0);

    function updateQRCodeView(qrcode: QRCodeItem, parentView: View) {
        const svgView = parentView.getViewById<SVGView>('svgView');
        DEV_LOG && console.log('updateQRCodeView', qrcode.scale, qrcode.svg);
        svgView.src = qrcode.svg;
        svgView.scaleX = qrcode.scale;
        svgView.scaleY = qrcode.scale;

        const textField = parentView.getViewById<TextField>('textField');
        textField.error = qrcode.error;
        const sliderValue = parentView.getViewById<Label>('sliderValue');
        sliderValue.text = (qrcode.scale ?? 1).toFixed(1);
    }
    const setQRCodeText = debounce(async (qrcode: QRCodeItem, event) => {
        const text = event.value;
        if (text === qrcode.text) {
            return;
        }
        try {
            DEV_LOG && console.log('setQRCodeText', text);
            const svg = await getQRCodeImage(qrcode, 'black');
            DEV_LOG && console.log('setQRCodeText1', text, typeof svg);
            qrcode.text = text;
            qrcode.svg = svg?.length
                ? svg
                : '<svg xmlns="http://www.w3.org/2000/svg" height="24px" viewBox="0 -960 960 960" width="24px" fill="#1f1f1f"><path d="M508.5-291.5Q520-303 520-320t-11.5-28.5Q497-360 480-360t-28.5 11.5Q440-337 440-320t11.5 28.5Q463-280 480-280t28.5-11.5ZM440-440h80v-240h-80v240Zm40 360q-83 0-156-31.5T197-197q-54-54-85.5-127T80-480q0-83 31.5-156T197-763q54-54 127-85.5T480-880q83 0 156 31.5T763-763q54 54 85.5 127T880-480q0 83-31.5 156T763-197q-54 54-127 85.5T480-80Zm0-80q134 0 227-93t93-227q0-134-93-227t-227-93q-134 0-227 93t-93 227q0 134 93 227t227 93Zm0-320Z"/></svg>';
            const index = qrcodesToUpdate.findIndex((qr) => qr.id === qrcode.id) ?? -1;
            if (index !== -1) {
                qrcodesToUpdate.splice(index, 1, qrcode);
            } else {
                qrcodesToUpdate.push(qrcode);
            }
        } catch (error) {
            DEV_LOG && console.log('setQRCodeText error', text, error);
            qrcode.text = text;
            qrcode.error = lc('unsupported_barcode_value');

            // we prevent potential update
            const index = qrcodesToUpdate.findIndex((qr) => qr.id === qrcode.id) ?? -1;
            if (index !== -1) {
                qrcodesToUpdate.splice(index, 1);
            }
        }
        // we dont want to update the pager item becasue it would loose tf focus and look weird, instead we update manually
        updateQRCodeView(qrcode, event.object.parent.parent);
    }, 500);

    $: pagerHeight = Math.min(screenHeightDips * (editing ? 0.5 : 0.5), 400);
</script>

<page bind:this={page} id="cardview" actionBarHidden={true} statusBarColor={topBackgroundColor} {statusBarStyle}>
    <gridlayout class="pageContent" backgroundColor={topBackgroundColor} columns={$isLandscape ? 'auto,*' : '*'} rows={$isLandscape ? 'auto,*' : 'auto,auto,*'}>
        {#if CARD_APP && isPKPassDocument}
            <pager bind:this={pager} items={document.pages} row={2} transformers="zoomOut" on:selectedIndexChange={onSelectedIndex}>
                <Template let:item>
                    <stacklayout padding="0">
                        <PKPassView {document} pkpass={item.pkpass} />
                    </stacklayout>
                </Template>
            </pager>
        {:else}
            <collectionview
                bind:this={collectionView}
                id="view"
                ios:autoReloadItemOnLayout={true}
                colWidth={$isLandscape ? '100%' : '50%'}
                height={$isLandscape ? undefined : itemHeight}
                {items}
                orientation={$isLandscape ? 'vertical' : 'horizontal'}
                reorderEnabled={true}
                row={1}
                rowHeight={itemHeight}
                visibility={document.pages.length === 1 && !document.pages[0].imagePath ? 'collapsed' : 'visible'}
                width={$isLandscape ? colWidth : undefined}
                on:itemReordered={onItemReordered}
                on:itemReorderStarting={onItemReorderStarting}>
                <Template let:index let:item>
                    <gridlayout
                        class="cardItemTemplate"
                        backgroundColor={getItemBackgroundColor(item)}
                        elevation={isEInk ? 0 : 6}
                        longPressGestureOptions={(view, tag, rootTag) => ({
                            simultaneousHandlers: [rootTag, view['PAN_HANDLER_TAG']],
                            minDurationMs: 300
                        })}
                        margin={12}
                        panGestureOptions={(view, tag, rootTag) => ({
                            simultaneousHandlers: [rootTag, view['LONGPRESS_HANDLER_TAG']],
                            minDist: 20
                        })}
                        rippleColor={colorSurface}
                        on:tap={() => onItemTap(item)}
                        on:touch={(e) => onTouch(item, e)}
                        on:longPress={(e) => onItemLongPress(item, e)}
                        on:pan={(e) => onPan(item, e)}>
                        <RotableImageView
                            id="imageView"
                            borderRadius={12}
                            decodeWidth={colWidth}
                            item={item.page}
                            sharedTransitionTag={`document_${document.id}_${item.page.id}`}
                            stretch="aspectFit"
                            width="100%" />
                        <label
                            autoFontSize={true}
                            color={getItemLabelColor(item)}
                            fontSize={20}
                            fontWeight="bold"
                            lineBreak="end"
                            margin={16}
                            maxFontSize={20}
                            text={document.name}
                            textAlignment="center"
                            textWrap={true}
                            verticalTextAlignment="center"
                            visibility={item.page.imagePath ? 'hidden' : 'visible'} />
                        <SelectedIndicator rowSpan={2} selected={item.selected} />
                        <label fontFamily={$fonts.mdi} fontSize={24} padding={10} text="mdi-reorder-horizontal" verticalAlignment="bottom" visibility={inEditMode ? 'visible' : 'hidden'} />
                        <PageIndicator horizontalAlignment="right" margin={2} rowSpan={2} scale={$fontScale} text={index + 1} />
                    </gridlayout>
                </Template>
            </collectionview>
            <!-- <gridlayout backgroundColor={colorBackground} borderRadius="10 10 0 0" col={$isLandscape ? 1 : 0} row={$isLandscape ? 1 : 2} rows="auto,auto,*"> -->
            <!-- <stacklayout bind:this={fabHolder} horizontalAlignment="right" orientation="horizontal" rowSpan={3} verticalAlignment="bottom" android:marginBottom={$windowInset.bottom}> -->
            <!-- {#if __IOS__}
                        <mdbutton class="small-fab" text="mdi-image-plus-outline" verticalAlignment="center" on:tap={throttle(() => importDocument(false), 500)} />
                    {/if}
                    <mdbutton class="small-fab" horizontalAlignment="center" text="mdi-file-document-plus-outline" on:tap={throttle(() => importDocument(), 500)} /> -->
            <!-- </stacklayout> -->
            <gridlayout class="cardViewHolder" col={$isLandscape ? 1 : 0} row={$isLandscape ? 1 : 2}>
                <collectionview bind:this={collectionViewExtras} itemTemplateSelector={selectTemplate} items={extraItems} paddingBottom={Math.max($windowInset.bottom + FAB_BUTTON_OFFSET)}>
                    <Template key={QRCODES_TYPE} let:item>
                        <gridlayout height={pagerHeight} margin={16} rows="*,auto">
                            <pager bind:this={pager} id="pager" items={item.qrcodes} margin={16} selectedIndex={currentQRCodeIndex} on:selectedIndexChange={onSelectedIndex}>
                                <Template let:index let:item>
                                    <gridlayout rows="*,auto" on:tap={onQRCodeTap}>
                                        <svgview
                                            id="svgView"
                                            backgroundColor={qrcodeBackgroundColor}
                                            height={pagerHeight - (editing ? 100 : 30)}
                                            marginBottom={10}
                                            scaleX={item.scale ?? 1}
                                            scaleY={item.scale ?? 1}
                                            sharedTransitionTag={'qrcode' + index}
                                            src={item.svg}
                                            stretch="aspectFit"
                                            verticalAlignment="top" />
                                        <gridlayout columns="*,auto" row={1} rows="auto,auto">
                                            <label
                                                fontSize={30}
                                                fontWeight="bold"
                                                ios:linkColor={colorOnBackground}
                                                maxLines={2}
                                                row={1}
                                                selectable={true}
                                                sharedTransitionTag={'qrcodelabel' + index}
                                                text={item?.text}
                                                textAlignment="center"
                                                visibility={editing ? 'collapsed' : 'visible'} />
                                            <textfield
                                                id="textField"
                                                error={item.error}
                                                maxLines={1}
                                                row={1}
                                                text={item?.text}
                                                variant="outline"
                                                verticalAlignment="center"
                                                visibility={editing ? 'visible' : 'collapsed'}
                                                on:textChange={(event) => setQRCodeText(item, event)} />
                                            <IconButton
                                                col={1}
                                                marginTop={5}
                                                row={1}
                                                text="mdi-delete"
                                                verticalAlignment="center"
                                                visibility={editing ? 'visible' : 'collapsed'}
                                                on:tap={(event) => deleteCurrentQRCode(item, event)} />

                                            <gridlayout colSpan={2} columns="auto,*,auto" padding={4} rows="auto" visibility={editing ? 'visible' : 'collapsed'} width="100%">
                                                <label col={0} marginTop={4} text={lc('scale')} verticalAlignment="center" />
                                                <label id="sliderValue" col={2} text={item.scale ?? 1} textAlignment="center" verticalTextAlignment="center" width={100} />
                                                <slider col={1} maxValue={10} minValue={1} value={(item.scale ?? 1) * 10} on:valueChange={(event) => setQRCodeScale(item, event.value, event)} />
                                            </gridlayout>
                                        </gridlayout>
                                    </gridlayout>
                                </Template>
                            </pager>
                            <!-- <label rowSpan={3} text={lc('no_qrcode')} textAlignment="center" verticalTextAlignment="center" visibility={hasQRCodes ? 'hidden' : 'visible'} /> -->
                            <pagerindicator
                                color={colorSurfaceContainerHigh}
                                horizontalAlignment="center"
                                marginBottom={5}
                                pagerViewId="pager"
                                row={1}
                                selectedColor={colorOnSurfaceVariant}
                                type="worm"
                                verticalAlignment="bottom" />
                        </gridlayout>
                    </Template>
                    <Template key="color" let:item>
                        <ListItemAutoSize item={{ title: lc('color') }} on:tap={(event) => changeColor(item, event)}>
                            <absolutelayout backgroundColor={topBackgroundColor} borderColor={colorOutline} borderRadius="50%" borderWidth={2} col={1} height={40} marginLeft={10} width={40} />
                        </ListItemAutoSize>
                    </Template>
                    <Template let:item>
                        <gridlayout padding="4 10 4 10">
                            <textview editable={false} hint={item.name} text={formatItemValue(item)} variant="outline" on:tap={(event) => onExtraFieldTap(item, event)} />
                            <IconButton
                                horizontalAlignment="right"
                                marginTop={5}
                                text="mdi-delete"
                                verticalAlignment="center"
                                visibility={editing ? 'visible' : 'collapsed'}
                                on:tap={(event) => deleteExtraField(item, event)} />
                        </gridlayout>
                        <ListItemAutoSize columns="*,auto,auto" item={{ rightValue: formatItemValue(item), title: item.name }}></ListItemAutoSize>
                    </Template>
                    <Template key="qrcode" let:item>
                        <gridlayout columns="auto,*,auto" height={60} padding="4 0 4 10">
                            <svgview backgroundColor={qrcodeBackgroundColor} src={item.svg} stretch="aspectFit" width={50} />
                            <label col={1} fontSize={17} fontWeight="bold" maxLines={2} paddingLeft={5} text={item?.text} verticalTextAlignment="center" />
                            <IconButton col={2} text="mdi-delete" visibility={editing ? 'visible' : 'hidden'} on:tap={(event) => deleteCurrentQRCode(item, event)} />
                        </gridlayout>
                    </Template>
                    <Template key="button" let:item>
                        <mdbutton margin={10} text={item.text} on:tap={(event) => onExtraItemTap(item, event)} />
                    </Template>
                </collectionview>
            </gridlayout>

            <!-- </gridlayout> -->
            <mdbutton
                col={1}
                margin={10}
                marginBottom={Math.min(60, $windowInset.bottom + 16)}
                row={2}
                text={lc('add_extra_field')}
                verticalAlignment="bottom"
                visibility={editing ? 'visible' : 'collapsed'}
                on:tap={(event) => addOrEditExtraField()} />
        {/if}
        <stacklayout
            bind:this={fabHolder}
            class="fabHolder"
            colSpan={2}
            marginBottom={Math.min(60, $windowInset.bottom)}
            orientation="horizontal"
            row={2}
            visibility={editing ? 'collapsed' : 'visible'}>
            <mdbutton class="small-fab" text="mdi-pencil" verticalAlignment="center" visibility={isPKPassDocument ? 'collapsed' : 'visible'} on:tap={startEdit} />
            <mdbutton
                class="small-fab"
                text="mdi-fullscreen"
                verticalAlignment="center"
                visibility={isPKPassDocument || !hasImages ? 'collapsed' : 'visible'}
                on:tap={throttle(() => showFullscreen(), 500)} />

            <mdbutton
                bind:this={fabHolder}
                id="fab"
                class="fab"
                text={editing ? 'mdi-check' : 'mdi-plus'}
                visibility={isPKPassDocument ? 'collapsed' : 'visible'}
                on:tap={throttle(() => onAddButton(), 500)} />
        </stacklayout>
        <CActionBar
            backgroundColor={topBackgroundColor}
            buttonsDefaultVisualState={statusBarStyle}
            colSpan={2}
            forceCanGoBack={inEditMode || nbSelected > 0}
            labelsDefaultVisualState={statusBarStyle}
            onGoBack={nbSelected ? unselectAll : inEditMode ? switchEditMode : null}
            onTitleTap={() => (editingTitle = true)}
            title={inEditMode ? lc('reorder_pages') : nbSelected ? lc('selected', nbSelected) : document.name}
            titleProps={{ padding: 0, color: statusBarStyle === 'dark' ? 'white' : 'black' }}>
            <!-- {#if editing}
                <mdbutton class="actionBarButton" defaultVisualState={statusBarStyle} text="mdi-close" variant="text" on:tap={cancelEdit} />
                <mdbutton class="actionBarButton" defaultVisualState={statusBarStyle} text="mdi-content-save" variant="text" on:tap={saveEdit} />
            {:else} -->
            <mdbutton
                class="actionBarButton"
                defaultVisualState={statusBarStyle}
                text="mdi-file-pdf-box"
                variant="text"
                visibility={canBeExportedToPDF ? 'visible' : 'collapse'}
                on:tap={showPDFPopover} />
            <mdbutton class="actionBarButton" defaultVisualState={statusBarStyle} text="mdi-dots-vertical" variant="text" on:tap={showOptions} />
            <!-- {/if} -->
        </CActionBar>
        {#if editingTitle}
            <EditNameActionBar
                autoFocus={!editing}
                backgroundColor={topBackgroundColor}
                buttonsDefaultVisualState={statusBarStyle}
                colSpan={2}
                {document}
                labelsDefaultVisualState={statusBarStyle}
                onGoBack={onEditActionBarGoBack}
                onSave={onEditActionBarSave}
                bind:editingTitle>
            </EditNameActionBar>
        {/if}
    </gridlayout>
</page>
