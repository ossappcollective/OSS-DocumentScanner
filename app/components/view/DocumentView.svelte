<script context="module" lang="ts">
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { Img, getImagePipeline } from '@nativescript-community/ui-image';
    import { confirm } from '@nativescript-community/ui-material-dialogs';
    import { VerticalPosition } from '@nativescript-community/ui-popover';
    import { AnimationDefinition, Application, ApplicationSettings, ContentView, EventData, ObservableArray, Page, PageTransition, SharedTransition, StackLayout } from '@nativescript/core';
    import { AndroidActivityBackPressedEventData } from '@nativescript/core/application';
    import { throttle } from '@nativescript/core/utils';
    import { filesize } from 'filesize';
    import { onDestroy, onMount } from 'svelte';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import CActionBar from '~/components/common/CActionBar.svelte';
    import PageIndicator from '~/components/common/PageIndicator.svelte';
    import RotableImageView from '~/components/common/RotableImageView.svelte';
    import SelectedIndicator from '~/components/common/SelectedIndicator.svelte';
    import SelectionToolbar from '~/components/common/SelectionToolbar.svelte';
    import PdfEdit from '~/components/edit/DocumentEdit.svelte';
    import { l, lc } from '~/helpers/locale';
    import { onThemeChanged } from '~/helpers/theme';
    import { OCRDocument, OCRPage } from '~/models/OCRDocument';
    import {
        DocumentDeletedEventData,
        DocumentPageDeletedEventData,
        DocumentPageUpdatedEventData,
        DocumentPagesAddedEventData,
        DocumentUpdatedEventData,
        documentsService
    } from '~/services/documents';
    import {
        BOTTOM_BUTTON_OFFSET,
        DEFAULT_NB_COLUMNS_VIEW,
        DEFAULT_NB_COLUMNS_VIEW_LANDSCAPE,
        EVENT_DOCUMENT_DELETED,
        EVENT_DOCUMENT_PAGES_ADDED,
        EVENT_DOCUMENT_PAGE_DELETED,
        EVENT_DOCUMENT_PAGE_UPDATED,
        EVENT_DOCUMENT_UPDATED,
        SETTINGS_NB_COLUMNS_VIEW,
        SETTINGS_NB_COLUMNS_VIEW_LANDSCAPE
    } from '~/utils/constants';
    import { showError } from '@shared/utils/showError';
    import { goBack, navigate, showModal } from '@shared/utils/svelte/ui';
    import {
        detectOCR,
        hideLoading,
        importAndScanImage,
        importImageFromCamera,
        onBackButton,
        showImagePopoverMenu,
        showLoading,
        showPDFPopoverMenu,
        showPopoverMenu,
        transformPages,
        tryCatchFunction
    } from '~/utils/ui';
    import { colors, fontScale, fonts, hasCamera, isLandscape, screenHeightDips, screenWidthDips, windowInset } from '~/variables';
    import EditNameActionBar from '../common/EditNameActionBar.svelte';
    import { prefs } from '@shared/services/preferences';
    import { ROOT_GESTURE_HANDLER_TAG } from '@nativescript-community/gesturehandler/gesturehandler.common';
    import { showBottomSheet } from '@nativescript-community/ui-material-bottomsheet/svelte';
    const rowMargin = 8;
    interface Item {
        page: OCRPage;
        selected: boolean;
        index: number;
    }
</script>

<script lang="ts">
    // technique for only specific properties to get updated on store change
    $: ({ colorBackground, colorError, colorOnSurfaceVariant, colorOutline, colorPrimary, colorSurface, colorSurfaceContainer, colorSurfaceContainerHigh } = $colors);

    export let document: OCRDocument;
    export let transitionOnBack = true;
    let collectionView: NativeViewElementNode<CollectionView>;
    let page: NativeViewElementNode<Page>;
    let fabHolder: NativeViewElementNode<StackLayout>;
    let nbSelected = 0;
    let editingTitle = false;

    let nbColumns = updateColumns($isLandscape);

    function updateColumns(isLandscape) {
        return isLandscape
            ? ApplicationSettings.getNumber(SETTINGS_NB_COLUMNS_VIEW_LANDSCAPE, DEFAULT_NB_COLUMNS_VIEW_LANDSCAPE)
            : ApplicationSettings.getNumber(SETTINGS_NB_COLUMNS_VIEW, DEFAULT_NB_COLUMNS_VIEW);
    }
    $: nbColumns = updateColumns($isLandscape);
    $: colWidth = 100 / nbColumns + '%';
    $: itemHeight = ($isLandscape ? screenHeightDips : screenWidthDips) / nbColumns - rowMargin * nbColumns + 140;

    prefs.on(`key:${SETTINGS_NB_COLUMNS_VIEW}`, () => (nbColumns = updateColumns($isLandscape)));
    prefs.on(`key:${SETTINGS_NB_COLUMNS_VIEW_LANDSCAPE}`, () => (nbColumns = updateColumns($isLandscape)));

    // Animate FAB visibility when selection changes
    $: if (fabHolder?.nativeView) {
        fabHolder.nativeView.animate({
            opacity: nbSelected > 0 ? 0 : 1,
            duration: 200
        });
    }

    // $: {
    const pages = document.getObservablePages();
    let items = pages.map((page, index) => ({ selected: false, page, index })) as any as ObservableArray<Item>;
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
    async function saveDocument() {
        try {
            showLoading(l('saving'));
            await document.save({}, true);
            hideLoading();
        } catch (err) {
            showError(err);
        }
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
        try {
            await showImagePopoverMenu(getSelectedPages(), event.object);
        } catch (err) {
            showError(err);
        }
    }

    async function addPages(inverseUseSystemCamera = false) {
        try {
            await importImageFromCamera({ document, inverseUseSystemCamera });
        } catch (error) {
            showError(error);
        }
    }

    async function importPages(importPDFs = true) {
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

    function startDragging(item: Item, event) {
        const index = items.findIndex((p) => p.page === item.page);
        collectionView?.nativeElement.startDragging(index, event.getActivePointers()[0]);
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
        // DEV_LOG && console.log('onItemLongPress', event.state);
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
        // DEV_LOG && console.log('onPan', dragStarted, event.state);
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
    async function onItemTap(item: Item, event?) {
        try {
            if (nbSelected > 0) {
                toggleSelection(item);
            } else {
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
                    // transition: { name: 'slideLeft', duration: 300, curve: 'easeOut' },
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
                    DEV_LOG && console.log('deleting pages', indexes, items.length);
                    for (let index = indexes.length - 1; index >= 0; index--) {
                        const toRemoveIndex = indexes[index];
                        await document.deletePage(toRemoveIndex);
                    }
                    // refreshCollectionView();
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
        if (current) {
            DEV_LOG && console.log('view onDocumentPageUpdated', index, current.index, event.imageUpdated);
            const page = document.getObservablePages().getItem(index);
            if (!!event.imageUpdated) {
                const imageView = getImageView(index);
                if (imageView) {
                    imageView?.updateImageUri();
                } else if (__IOS__) {
                    collectionView?.nativeElement?.refreshVisibleItems();
                }
            }
            items.setItem(index, { ...current, page });
        }
    }
    function onDocumentPageDeleted(event: DocumentPageDeletedEventData) {
        if (event.doc.id !== document.id) {
            return;
        }
        document = event.doc;
        const index = event.pageIndex;
        items.splice(index, 1);
        for (let i = index; i < items.length; i++) {
            const item = items.getItem(i);
            item.index -= 1;
            items.setItem(i, item);
        }
    }
    function onDocumentUpdated(event: DocumentUpdatedEventData) {
        // DEV_LOG && console.log('onDocumentUpdated', document);
        if (document.id === event.doc.id) {
            document = event.doc;
        }
    }
    function onDocumentsDeleted(event: DocumentDeletedEventData) {
        DEV_LOG &&
            console.log(
                'DocumentView onDocumentsDeleted',
                event.documents.map((d) => d.id),
                document.id
            );
        if (event.documents.findIndex((d) => d.id === document.id) !== -1) {
            DEV_LOG && console.log('DocumentView onDocumentsDeleted closing', document.id);
            goBack({
                // null is important to say no transition! (override enter transition)
                transition: null
            });
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

    onMount(() => {
        // DEV_LOG && console.log('DocumentView', 'onMount', VIEW_ID++);
        Application.on('snackMessageAnimation', onSnackMessageAnimation);
        if (__ANDROID__) {
            Application.android.on(Application.android.activityBackPressedEvent, onAndroidBackButton);
        }
        documentsService.on(EVENT_DOCUMENT_UPDATED, onDocumentUpdated);
        documentsService.on(EVENT_DOCUMENT_DELETED, onDocumentsDeleted);
        documentsService.on(EVENT_DOCUMENT_PAGE_DELETED, onDocumentPageDeleted);
        documentsService.on(EVENT_DOCUMENT_PAGE_UPDATED, onDocumentPageUpdated);
        documentsService.on(EVENT_DOCUMENT_PAGES_ADDED, onPagesAdded);
        documentsService.documentRepository.incrementUsage(document).catch(() => {});
    });
    onDestroy(() => {
        DEV_LOG && console.log('DocumentView', 'onDestroy', !!document);
        items = null;
        Application.off('snackMessageAnimation', onSnackMessageAnimation);
        if (__ANDROID__) {
            Application.android.off(Application.android.activityBackPressedEvent, onAndroidBackButton);
        }
        documentsService.off(EVENT_DOCUMENT_UPDATED, onDocumentUpdated);
        documentsService.off(EVENT_DOCUMENT_DELETED, onDocumentsDeleted);
        documentsService.off(EVENT_DOCUMENT_PAGE_DELETED, onDocumentPageDeleted);
        documentsService.off(EVENT_DOCUMENT_PAGE_UPDATED, onDocumentPageUpdated);
        documentsService.off(EVENT_DOCUMENT_PAGES_ADDED, onPagesAdded);
    });
    // onThemeChanged(refreshCollectionView);

    async function onItemReordered(e) {
        dragStarted = false;
        const view = (e.view as ContentView).content;
        view.animate({ duration: 100, opacity: 1, scale: { x: 1, y: 1 } });
        try {
            if (e.index !== e.data.targetIndex) {
                await document.movePage(e.index, e.data.targetIndex);
            }
            collectionView?.nativeView?.refreshVisibleItems();
        } catch (error) {
            showError(error);
        }
    }
    async function onItemReorderStarting(e) {
        const view = (e.view as ContentView).content;
        view.animate({ duration: 100, opacity: 0.8, scale: { x: 1.05, y: 1.05 } });
    }

    function refreshCollectionView() {
        collectionView?.nativeView?.refresh();
    }
    onThemeChanged(refreshCollectionView);

    function getSelectionToolbarOptions() {
        return [
            { icon: 'mdi-file-pdf-box', id: 'pdf', name: lc('export_pdf') },
            { icon: 'mdi-share-variant', id: 'share', name: lc('share_images') },
            { icon: 'mdi-auto-fix', id: 'transform', name: lc('transform_images') },
            { icon: 'mdi-text-recognition', id: 'ocr', name: lc('ocr_document') },
            { id: 'select_all', name: lc('select_all'), icon: 'mdi-select-all' },
            { color: colorError, icon: 'mdi-delete', id: 'delete', name: lc('delete') }
        ];
    }

    async function handleSelectionAction(event, item) {
        try {
            let result;
            switch (item.id) {
                case 'pdf':
                    const pages = nbSelected > 0 ? getSelectedPages() : document.pages.map((p) => ({ page: p, document }));
                    await showPDFPopoverMenu({ pages, document, anchor: event.object, documents: [document], popoverOptions: { vertPos: VerticalPosition.ABOVE } });
                    break;
                case 'select_all':
                    selectAll();
                    break;
                case 'share':
                    result = await showImagePopoverMenu(getSelectedPages(), event.object, { vertPos: VerticalPosition.ABOVE });
                    if (result) {
                        unselectAll();
                    }
                    break;
                case 'ocr':
                    result = await detectOCR({ pages: getSelectedPagesWithData() });
                    if (result) {
                        unselectAll();
                    }
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

    async function showOptions(event) {
        const options = new ObservableArray([
            { id: 'rename', name: lc('rename'), icon: 'mdi-rename' },
            { id: 'favorite', name: lc('toggle_favorite'), icon: document.favorite === 1 ? 'mdi-star' : 'mdi-star-outline' },
            { id: 'select_all', name: lc('select_all'), icon: 'mdi-select-all' },
            { id: 'reorder', name: lc('reorder_pages'), icon: 'mdi-reorder-horizontal' },
            { id: 'transform', name: lc('transform_images'), icon: 'mdi-auto-fix' },
            { id: 'ocr', name: lc('ocr_document'), icon: 'mdi-text-recognition' },
            { id: 'delete', name: lc('delete'), icon: 'mdi-delete', color: colorError }
        ] as any);
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

    const onAddButton = tryCatchFunction(
        async () => {
            const OptionSelect = (await import('@shared/components/OptionSelect.svelte')).default;
            const rowHeight = 58;
            const options = [
                {
                    id: 'files',
                    name: lc('import_from_file'),
                    icon: 'mdi-file-document-plus-outline'
                },
                {
                    id: 'import_image',
                    name: lc('import_from_image'),
                    icon: 'mdi-image-plus-outline'
                },
                {
                    id: 'import_document',
                    name: lc('import_from_other_document'),
                    icon: 'mdi-file-document-arrow-right'
                }
            ];
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
            if (option) {
                switch (option.id) {
                    case 'files':
                        await importAndScanImage({ document, importPDFs: true, canGoToView: false });
                        break;
                    case 'import_image':
                        await importAndScanImage({ canGoToView: false, forceGalleryPick: true, document, importPDFs: false });
                        break;
                    case 'import_document':
                        const component = (await import('~/components/common/DocumentPicker.svelte')).default;
                        const result: OCRDocument[] = await showModal({
                            page: component,
                            fullscreen: true,
                            props: {}
                        });
                        if (result?.length) {
                            await showLoading(lc('importing'));
                            const pages = result
                                .reduce((acc, v) => acc.concat(v.pages), [])
                                .map((page: OCRPage) => {
                                    const { id, ...others } = page.toJSON();
                                    return { ...others };
                                });
                            await document.addPages(pages);
                        }
                        break;
                }
            }
        },
        null,
        hideLoading
    );
</script>

<page bind:this={page} id="documentView" actionBarHidden={true}>
    <gridlayout class="pageContent" rows="auto,*">
        <collectionview
            bind:this={collectionView}
            id="view"
            {colWidth}
            {items}
            ios:autoReloadItemOnLayout={true}
            paddingBottom={Math.max($windowInset.bottom, BOTTOM_BUTTON_OFFSET)}
            reorderEnabled={true}
            row={1}
            rowHeight={itemHeight}
            on:itemReordered={onItemReordered}
            on:itemReorderStarting={onItemReorderStarting}>
            <Template let:index let:item>
                <!-- <gridlayout
                    backgroundColor={colorSurfaceContainer}
                    borderColor={colorOutline}
                    borderRadius={12}
                    borderWidth={1}
                    clipToBounds={true}
                    margin={8}
                    rippleColor={colorPrimary}
                    rows="*,auto"
                    on:tap={() => onItemTap(item)}
                    on:longPress={(e) => onItemLongPress(item, e)}>
                    <RotableImageView id="imageView" item={item.page} rowSpan={2} sharedTransitionTag={`document_${document.id}_${item.page.id}`} />
                    <SelectedIndicator selected={item.selected} />
                    <canvaslabel backgroundColor="#00000088" color="white" height={30} padding="5" row={1}>
                        <cspan fontSize={12} text={`${item.page.width} x ${item.page.height}`} />
                        <cspan fontFamily={$fonts.mdi} fontSize={20} text="mdi-text-recognition" textAlignment="right" visibility={item.page.ocrData ? 'visible' : 'hidden'} />
                    </canvaslabel>
                </gridlayout> -->

                <gridlayout
                    backgroundColor={colorSurfaceContainerHigh}
                    borderColor={colorOutline}
                    borderRadius={12}
                    borderWidth={0}
                    longPressGestureOptions={(view, tag, rootTag) => ({
                        simultaneousHandlers: [rootTag, view['PAN_HANDLER_TAG']],
                        minDurationMs: 300
                    })}
                    margin={8}
                    padding={10}
                    panGestureOptions={(view, tag, rootTag) => ({
                        simultaneousHandlers: [rootTag, view['LONGPRESS_HANDLER_TAG']],
                        minDist: 20
                    })}
                    rippleColor={colorSurface}
                    rows={`*,${40 * $fontScale}`}
                    on:tap={(e) => onItemTap(item, e)}
                    on:touch={(e) => onTouch(item, e)}
                    on:pan={(e) => onPan(item, e)}
                    on:longPress={(e) => onItemLongPress(item, e)}>
                    <RotableImageView
                        id="imageView"
                        borderRadius={12}
                        decodeWidth={itemHeight}
                        horizontalAlignment="center"
                        item={item.page}
                        sharedTransitionTag={`document_${document.id}_${item.page.id}`}
                        stretch="aspectFit"
                        verticalAlignment="center" />
                    <canvaslabel color={colorOnSurfaceVariant} fontSize={14 * $fontScale} padding="10 0 0 0" row={1}>
                        <cspan fontFamily={$fonts.mdi} fontSize={24} text="mdi-reorder-horizontal" visibility={inEditMode ? 'visible' : 'hidden'} />
                        <cspan
                            paddingLeft={inEditMode ? 30 : 0}
                            text={`${item.page.width} x ${item.page.height}\n${filesize(item.page.size, { output: 'string' })}`}
                            textAlignment="left"
                            verticalAlignment="bottom" />
                        <!-- <cspan color={colorOnSurfaceVariant} fontSize={12} paddingTop={36} text={dayjs(item.doc.createdDate).format('L LT')} /> -->
                        <!-- <cspan color={colorOnSurfaceVariant} fontSize={12} paddingTop={50} text={lc('nb_pages', item.doc.pages.length)} /> -->
                    </canvaslabel>
                    <SelectedIndicator rowSpan={2} selected={item.selected} />
                    <PageIndicator rowSpan={2} scale={$fontScale} text={index + 1} on:longPress={(event) => startDragging(item, event)} />
                </gridlayout>
            </Template>
        </collectionview>

        <stacklayout bind:this={fabHolder} class="fabHolder" marginBottom={Math.min(60, $windowInset.bottom)} orientation="horizontal" row={1}>
            {#if __IOS__}
                <mdbutton
                    class="small-fab"
                    text="mdi-image-plus-outline"
                    verticalAlignment="center"
                    on:tap={throttle(() => importAndScanImage({ document, importPDFs: false, canGoToView: false, forceGalleryPick: true }), 500)} />
            {/if}
            <mdbutton class={$hasCamera ? 'small-fab' : 'fab'} text="mdi-file-document-plus-outline" verticalAlignment="center" on:tap={throttle(() => onAddButton(), 500)} />
            {#if $hasCamera}
                <mdbutton class="fab" text="mdi-camera" verticalAlignment="center" on:tap={throttle(() => addPages(), 500)} on:longPress={() => addPages(true)} />
            {/if}
        </stacklayout>
        <CActionBar
            forceCanGoBack={inEditMode || nbSelected > 0}
            onGoBack={nbSelected ? unselectAll : inEditMode ? switchEditMode : null}
            onTitleTap={() => (editingTitle = true)}
            title={inEditMode ? lc('reorder_pages') : nbSelected ? lc('selected', nbSelected) : document.name}
            titleProps={{ padding: 0 }}>
            {#if !nbSelected}
                <mdbutton class="actionBarButton" text="mdi-file-pdf-box" variant="text" on:tap={showPDFPopover} />
                <mdbutton class="actionBarButton" text="mdi-dots-vertical" variant="text" on:tap={showOptions} />
            {/if}
        </CActionBar>

        {#if nbSelected > 0}
            <SelectionToolbar onAction={handleSelectionAction} options={getSelectionToolbarOptions()} row={1} />
        {/if}
        {#if editingTitle}
            <EditNameActionBar {document} bind:editingTitle />
        {/if}
    </gridlayout>
</page>
