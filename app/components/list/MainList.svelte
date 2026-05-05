<script context="module" lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { Canvas, CanvasView, LayoutAlignment, Paint, StaticLayout } from '@nativescript-community/ui-canvas';
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { Img } from '@nativescript-community/ui-image';
    import { createNativeAttributedString } from '@nativescript-community/ui-label';
    import { confirm, prompt } from '@nativescript-community/ui-material-dialogs';
    import { VerticalPosition } from '@nativescript-community/ui-popover';
    import { AnimationDefinition, Application, ApplicationSettings, Color, EventData, Frame, NavigatedData, ObservableArray, Page, StackLayout } from '@nativescript/core';
    import { AndroidActivityBackPressedEventData } from '@nativescript/core/application/application-interfaces';
    import { debounce, throttle } from '@nativescript/core/utils';
    import { OptionType } from '@shared/components/OptionSelect.svelte';
    import { prefs } from '@shared/services/preferences';
    import { showError } from '@shared/utils/showError';
    import { closeModal, fade, goBack, navigate } from '@shared/utils/svelte/ui';
    import { onDestroy, onMount } from 'svelte';
    import { writable } from 'svelte/store';
    import CActionBar from '~/components/common/CActionBar.svelte';
    import EditNameActionBar from '~/components/common/EditNameActionBar.svelte';
    import ListItemAutoSize from '@shared/components/ListItemAutoSize.svelte';
    import SelectedIndicator from '~/components/common/SelectedIndicator.svelte';
    import SelectionToolbar from '~/components/common/SelectionToolbar.svelte';
    import ActionBarSearch from '~/components/widgets/ActionBarSearch.svelte';
    import { l, lc } from '~/helpers/locale';
    import { getRealTheme, isEInk, onThemeChanged } from '~/helpers/theme';
    import { DocFolder, OCRDocument, OCRPage } from '~/models/OCRDocument';
    import {
        DocumentAddedEventData,
        DocumentDeletedEventData,
        DocumentFolderAddedEventData,
        DocumentMovedFolderEventData,
        DocumentPageUpdatedEventData,
        DocumentUpdatedEventData,
        FolderUpdatedEventData,
        documentsService
    } from '~/services/documents';
    import { syncService, syncServicesStore } from '~/services/sync';
    import { SERVICES_SYNC_COLOR, SERVICES_SYNC_MASK } from '~/services/sync/types';
    import {
        BOTTOM_BUTTON_OFFSET,
        DEFAULT_FOLDER_VIEW_STYLE,
        DEFAULT_NB_COLUMNS,
        DEFAULT_NB_COLUMNS_LANDSCAPE,
        DEFAULT_SORT_ORDER,
        DEFAULT_VIEW_STYLE,
        EVENT_DOCUMENT_ADDED,
        EVENT_DOCUMENT_DELETED,
        EVENT_DOCUMENT_MOVED_FOLDER,
        EVENT_DOCUMENT_PAGE_DELETED,
        EVENT_DOCUMENT_PAGE_UPDATED,
        EVENT_DOCUMENT_UPDATED,
        EVENT_FOLDER_ADDED,
        EVENT_FOLDER_UPDATED,
        EVENT_STATE,
        EVENT_SYNC_STATE,
        SETTINGS_FOLDER_VIEW_STYLE,
        SETTINGS_NB_COLUMNS,
        SETTINGS_NB_COLUMNS_LANDSCAPE,
        SETTINGS_SORT_ORDER,
        SETTINGS_VIEW_STYLE
    } from '~/utils/constants';
    import {
        detectOCR,
        goToDocumentView,
        goToFolderView,
        importAndScanImage,
        onAndroidNewItent,
        onBackButton,
        pickFolderColor,
        promptForFolderName,
        showImagePopoverMenu,
        showPDFPopoverMenu,
        showPopoverMenu,
        showSettings,
        transformPages,
        tryCatchFunction
    } from '~/utils/ui';
    import { sortByKey } from '~/utils/utils.common';
    import { colors, folderBackgroundColor, fontScale, fonts, isLandscape, onFolderBackgroundColorChanged, onFontScaleChanged, startOnCam, windowInset } from '~/variables';

    const textPaint = new Paint();

    export interface Item {
        type?: string;
        doc?: OCRDocument;
        folder?: DocFolder;
        selected: boolean;
        startingSide?: string;
    }
</script>

<script lang="ts">
    // technique for only specific properties to get updated on store change
    let {
        colorError,
        colorOnBackground,
        colorOnSurfaceVariant,
        colorOutline,
        colorPrimary,
        colorPrimaryContainer,
        colorSurface,
        colorSurfaceContainerHigh,
        colorSurfaceContainerLow,
        colorSurfaceContainerLowest
    } = $colors;
    $: ({
        colorError,
        colorOnBackground,
        colorOnSurfaceVariant,
        colorOutline,
        colorPrimary,
        colorPrimaryContainer,
        colorSurface,
        colorSurfaceContainerHigh,
        colorSurfaceContainerLow,
        colorSurfaceContainerLowest
    } = $colors);

    let folders: DocFolder[] = [];
    export let collectionViewOptions = {};
    export let documents: ObservableArray<Item> = null;
    export let folderItems: ObservableArray<Item> = null;
    let nbDocuments: number = 0;
    let showNoDocument = false;
    let mounted = false;
    let page: NativeViewElementNode<Page>;
    export let collectionView: NativeViewElementNode<CollectionView>;
    let foldersCollectionView: NativeViewElementNode<CollectionView>;
    // let lottieView: NativeViewElementNode<LottieView>;
    export let fabHolder: NativeViewElementNode<StackLayout> = null;
    let search: ActionBarSearch;

    export let folder: DocFolder = null;
    export let title: string;
    export let modal: boolean = false;
    export let itemTemplateSelector = (viewStyle, item?: Item) => item?.type || 'default';
    export let viewStyles: { [k: string]: { name: string; icon?: string; type?: string; boxType?: string } };
    export let folderViewStyles: { [k: string]: { name: string; icon?: string; type?: string; boxType?: string } } = {};
    export let sortKeys: { [k: string]: { name: string; icon?: string; type?: string; key?: string } } = {
        name: { name: lc('name') },
        createdDate: { name: lc('date') },
        useCount: { name: lc('most_used') }
    };
    export let onlyForImport = false;
    export let viewStyleChanged = (oldValue, newValue) => newValue !== oldValue;
    export let folderViewStyleChanged = (oldValue, newValue) => newValue !== oldValue;
    export let sortOrder = ApplicationSettings.getString(SETTINGS_SORT_ORDER, DEFAULT_SORT_ORDER);
    export let syncEnabled = syncService.enabled;
    export let viewStyle: string = ApplicationSettings.getString(SETTINGS_VIEW_STYLE, DEFAULT_VIEW_STYLE);
    export let folderViewStyle: string = ApplicationSettings.getString(SETTINGS_FOLDER_VIEW_STYLE, DEFAULT_FOLDER_VIEW_STYLE);

    export const getSyncColors = (item: Item) => {
        const synced = item.doc._synced;
        const result = [];
        $syncServicesStore.forEach((d) => {
            const mask = SERVICES_SYNC_MASK[d.type];
            const color = d.color || SERVICES_SYNC_COLOR[d.type];
            if (synced === 1 || (synced & mask) !== 0) {
                result.push(color);
            }
        });
        return result;
    };

    $: if ($syncServicesStore) {
        syncEnabled = syncService.enabled;
    }
    $: updateTitle(folder);

    export let updateTitle = (folder) => {
        if (folder) {
            DEV_LOG && console.log('updateTitle', $fontScale, folder);
            title = createNativeAttributedString(
                {
                    spans: [
                        {
                            fontFamily: $fonts.mdi,
                            color: folder.color || colorOutline,
                            fontSize: 24 * $fontScale,
                            text: 'mdi-folder'
                        },
                        {
                            text: '  ' + folder.name
                        }
                    ]
                },
                undefined,
                undefined,
                undefined,
                undefined,
                1,
                true // needed as will be used in a label
            );
            DEV_LOG && console.log('updating folder title', folder, $fonts.mdi, title);
        }
    };

    let syncRunning = false;
    $: DEV_LOG && console.log('syncEnabled', syncEnabled);

    let lastRefreshFilter = null;
    let showSearch = false;
    let loading = false;
    export let nbSelected = 0;
    let editingTitle = false;
    export let nbColumns = writable(1);
    export let updateColumns = function (isLandscape, orientationChanged: boolean = false) {
        $nbColumns = isLandscape ? ApplicationSettings.getNumber(SETTINGS_NB_COLUMNS_LANDSCAPE, DEFAULT_NB_COLUMNS_LANDSCAPE) : ApplicationSettings.getNumber(SETTINGS_NB_COLUMNS, DEFAULT_NB_COLUMNS);
    };
    $: if (mounted) updateColumns($isLandscape, true);
    $: colWidth = 100 / $nbColumns + '%';
    // $: DEV_LOG && console.log('nbColumns', nbColumns);
    // $: DEV_LOG && console.log('colWidth', colWidth);

    prefs.on(`key:${SETTINGS_NB_COLUMNS}`, () => updateColumns($isLandscape));
    prefs.on(`key:${SETTINGS_NB_COLUMNS_LANDSCAPE}`, () => updateColumns($isLandscape));

    $: if (nbSelected > 0) search.unfocusSearch();

    // Animate FAB visibility when selection changes
    $: if (fabHolder?.nativeView) {
        fabHolder.nativeView.animate({
            opacity: nbSelected > 0 ? 0 : 1,
            duration: 200
        });
    }

    export async function refresh(force = true, filter?: string) {
        // if (!shown) {
        //     needsRefreshOnBack = true;
        //     return;
        // }
        DEV_LOG && console.log('refresh', force, filter);
        if (loading || (!force && lastRefreshFilter === filter) || !documentsService.started) {
            return;
        }
        DEV_LOG && console.log('refresh1', force, filter);
        lastRefreshFilter = filter;
        nbSelected = 0;
        loading = true;
        try {
            DEV_LOG && console.log('MainList', 'refresh', folder, filter, sortOrder);
            const r = await documentsService.documentRepository.findDocuments({ filter, folder, omitThoseWithFolders: true, order: sortOrder });

            await refreshFolders(filter);
            documents = new ObservableArray(
                (folderItems.length ? (folderViewStyle === 'vertical' ? [...folderItems] : [{ type: 'folders', selected: false }]) : []).concat(
                    r.map(
                        (doc) =>
                            ({
                                doc,
                                selected: false
                            }) as any
                    )
                )
            );
            DEV_LOG &&
                console.log(
                    'documents',
                    documents.map((d) => d.doc?.id)
                );
            updateNoDocument();
        } catch (error) {
            showError(error);
        } finally {
            loading = false;
        }
    }

    const refreshFolders = tryCatchFunction(async (filter: string = lastRefreshFilter) => {
        try {
            folders = filter?.length ? [] : await documentsService.folderRepository.findFolders(folder);
            // DEV_LOG && console.log('folders', JSON.stringify(folders));
            folderItems = new ObservableArray(
                folders
                    .filter((f) => f.count > 0)
                    .map(
                        (folder) =>
                            ({
                                folder,
                                type: 'folder',
                                selected: false
                            }) as any
                    )
            );
            if (documents) {
                if (folderViewStyle === 'horizontal') {
                    if (folderItems.length) {
                        if (documents.getItem(0)?.type !== 'folders') {
                            documents.splice(0, 0, { type: 'folders', selected: false });
                        } else {
                            documents.splice(0, 1, { type: 'folders', selected: false });
                        }
                    } else {
                        if (documents.getItem(0)?.type === 'folders') {
                            documents.splice(0, 1);
                        }
                    }
                } else {
                    const firstDocIndex = documents.findIndex((d) => !!d.doc);
                    documents.splice(0, firstDocIndex, ...folderItems);
                }
            }
        } catch (error) {
            showError(error);
        }
    });
    const refreshSimple = () => refresh();
    const refreshThrottle = debounce(refresh, 300);

    function updateNoDocument() {
        nbDocuments = documents?.length ?? 0;
        showNoDocument = nbDocuments === 0;
    }
    function onDocumentAdded(event: DocumentAddedEventData) {
        // DEV_LOG && console.log('onDocumentAdded', event.doc.id, event.doc.folders, event.folder);
        if ((!event.folder && !folder) || folder?.name === event.folder?.name) {
            // DEV_LOG && console.log('onDocumentAdded', nbDocuments);
            // find the first document index to add the new doc just before
            const index = documents?.findIndex((d) => !!d.doc);
            if (index !== -1) {
                documents?.splice(index, 0, {
                    doc: event.doc,
                    selected: false
                } as Item);
                collectionView?.nativeElement.scrollToIndex(index, false);
            } else {
                refresh();
            }
            updateNoDocument();
        } else if (!folder && event.folder) {
            refreshFolders();
        }
    }

    function onDocumentMovedFolder(event: DocumentMovedFolderEventData) {
        // TODO: for now we refresh otherwise the order might be lost
        // DEV_LOG && console.log('onDocumentMovedFolder', folder?.id, event.folder?.id, event.oldFolderId, !!folder, folder?.id === event.oldFolderId, typeof folder?.id, typeof event.oldFolderId);
        if (!folder && (!event.folder || !event.oldFolderId)) {
            refresh();
        } else if (!!folder && (folder.id === event.folder?.id || folder.id === event.oldFolderId)) {
            refresh();
        }
    }

    function onDocumentUpdated(event: DocumentUpdatedEventData) {
        let index = -1;
        const doc = event.doc;
        documents?.some((d, i) => {
            if (d.doc && d.doc.id === doc.id) {
                index = i;
                return true;
            }
        });
        DEV_LOG && console.log('onDocumentUpdated', doc._synced, doc.id, index, doc.folders, doc.pages.length);
        if (index >= 0) {
            if (folder && doc.folders && doc.folders.indexOf(folder.id) === -1) {
                documents.splice(index, 1);
            } else {
                const item = documents?.getItem(index);
                if (item) {
                    // DEV_LOG && console.log('onDocumentUpdated1', [...event.changedProps], sortOrder, event.changedProps.has('name'), sortOrder.startsWith('name'));
                    if (event.changedProps.has('favorite') || (event.changedProps.has('name') && sortOrder.startsWith('name'))) {
                        refresh();
                    } else {
                        item.doc = event.doc;
                        documents.setItem(index, item);
                    }
                }
            }
            refreshFolders();
        }
    }
    function onFolderAdded(event: DocumentFolderAddedEventData) {
        // DEV_LOG && console.log('onFolderAdded', event.folder);
        refreshFolders();
    }
    function onFolderUpdated(event: FolderUpdatedEventData) {
        // DEV_LOG && console.log('onFolderUpdated', event.folder);
        refreshFolders();
    }
    async function onDocumentsDeleted(event: DocumentDeletedEventData) {
        // DEV_LOG &&
        //     console.log(
        //         'onDocumentsDeleted',
        //         event.documents.map((d) => d.id),
        //         event.folders
        //     );
        for (let i = 0; i < event.documents.length; i++) {
            const id = event.documents[i].id;
            const index = documents.findIndex((item) => item.doc && item.doc.id === id);
            if (index !== -1) {
                documents.splice(index, 1);
                if (nbSelected > 0) {
                    nbSelected = Math.max(nbSelected - 1, 0);
                }
            }
        }
        refreshFolders();
        updateNoDocument();
    }
    function getImageView(index: number) {
        const view = collectionView?.nativeView?.getViewForItemAtIndex(index);
        return view?.getViewById<Img>('imageView');
    }

    function onDocumentPageUpdated(event: DocumentPageUpdatedEventData) {
        // let index = -1;
        const document = event.doc;
        const index = documents?.findIndex((d) => d.doc && d.doc.id === document.id);
        if (index >= 0) {
            if (event.pageIndex === 0) {
                if (!!event.imageUpdated) {
                    const imageView = getImageView(index);
                    // DEV_LOG && console.log('list onDocumentPageUpdated image clean', index, imageView);
                    // const page = document.pages[event.pageIndex];
                    if (imageView) {
                        imageView?.updateImageUri();
                    } else if (__IOS__) {
                        collectionView?.nativeElement?.refreshVisibleItems();
                    }
                }
            }
            const item = documents?.getItem(index);
            if (item) {
                item.doc = document;
                documents.setItem(index, item);
            }
        }
    }
    function onSyncState(event: EventData & { state: 'running' | 'finished' }) {
        syncRunning = event.state === 'running';
    }

    function onSnackMessageAnimation({ animationArgs }: EventData & { animationArgs: AnimationDefinition[] }) {
        if (fabHolder) {
            const snackAnimation = animationArgs[0];
            animationArgs.push({
                duration: snackAnimation.duration,
                target: fabHolder.nativeView,
                translate: { x: 0, y: snackAnimation.translate.y === 0 ? -70 : 0 }
            });
        }
    }

    onMount(() => {
        mounted = true;
        // DEV_LOG && console.log('MainList', 'onMount', documentsService.id, viewStyle);
        updateColumns($isLandscape);
        Application.on('snackMessageAnimation', onSnackMessageAnimation);
        if (__ANDROID__) {
            Application.android.on(Application.android.activityBackPressedEvent, onAndroidBackButton);
            Application.android.on(Application.android.activityNewIntentEvent, onAndroidNewItent);
            const intent = Application.android['startIntent'];
            if (intent) {
                onAndroidNewItent({ intent } as any);
            }
        }
        documentsService.on(EVENT_DOCUMENT_PAGE_UPDATED, onDocumentPageUpdated);
        documentsService.on(EVENT_DOCUMENT_PAGE_DELETED, onDocumentPageUpdated);
        documentsService.on(EVENT_DOCUMENT_ADDED, onDocumentAdded);
        documentsService.on(EVENT_DOCUMENT_UPDATED, onDocumentUpdated);
        documentsService.on(EVENT_DOCUMENT_DELETED, onDocumentsDeleted);
        documentsService.on(EVENT_DOCUMENT_MOVED_FOLDER, onDocumentMovedFolder);
        documentsService.on(EVENT_FOLDER_ADDED, onFolderAdded);
        documentsService.on(EVENT_FOLDER_UPDATED, onFolderUpdated);
        syncService.on(EVENT_SYNC_STATE, onSyncState);
        syncService.on(EVENT_STATE, refreshSimple);
    });
    onDestroy(() => {
        DEV_LOG && console.log('MainList', 'onDestroy');
        Application.off('snackMessageAnimation', onSnackMessageAnimation);
        if (__ANDROID__) {
            Application.android.off(Application.android.activityBackPressedEvent, onAndroidBackButton);
            Application.android.off(Application.android.activityNewIntentEvent, onAndroidNewItent);
        }
        documentsService.off(EVENT_DOCUMENT_PAGE_UPDATED, onDocumentPageUpdated);
        documentsService.off(EVENT_DOCUMENT_PAGE_DELETED, onDocumentPageUpdated);
        documentsService.off(EVENT_DOCUMENT_UPDATED, onDocumentUpdated);
        documentsService.off(EVENT_DOCUMENT_ADDED, onDocumentAdded);
        documentsService.off(EVENT_DOCUMENT_DELETED, onDocumentsDeleted);
        documentsService.off(EVENT_DOCUMENT_MOVED_FOLDER, onDocumentMovedFolder);
        documentsService.off(EVENT_FOLDER_ADDED, onFolderAdded);
        documentsService.off(EVENT_FOLDER_UPDATED, onFolderUpdated);
        syncService.off(EVENT_SYNC_STATE, onSyncState);
        syncService.off(EVENT_STATE, refreshSimple);
    });

    export let showActionButton = !startOnCam;

    export async function importDocument(importPDFs = true) {
        DEV_LOG && console.log('importDocument', importPDFs);
        try {
            await importAndScanImage({ folder, importPDFs });
        } catch (error) {
            showError(error);
        }
    }
    export async function importImages() {
        DEV_LOG && console.log('importImages');
        try {
            await importAndScanImage({ folder, importPDFs: false, forceGalleryPick: true });
        } catch (error) {
            showError(error);
        }
    }
    // let needsRefreshOnBack = false;
    // let shown = false;
    async function onNavigatedTo(e: NavigatedData) {
        // shown = true;
        DEV_LOG && console.log('onNavigatedTo', e.isBackNavigation, sortOrder);
        if (!e.isBackNavigation) {
            if (documentsService.started) {
                refresh();
            } else {
                documentsService.once('started', refreshSimple);
            }
        } else if (sortOrder.startsWith('useCount')) {
            refresh();
        }
    }
    function onNavigatingFrom() {
        // shown = false;
        search.unfocusSearch();
    }
    function selectItem(item: Item) {
        if (!item.selected) {
            if (item.folder) {
                if (folderViewStyle === 'horizontal') {
                    folderItems?.some((d, index) => {
                        if (d.folder === item.folder) {
                            nbSelected += d.folder.count;
                            d.selected = true;
                            folderItems.setItem(index, d);
                            return true;
                        }
                    });
                } else {
                    documents?.some((d, index) => {
                        if (d.folder === item.folder) {
                            nbSelected += 1;
                            d.selected = true;
                            documents.setItem(index, d);
                            return true;
                        }
                    });
                }
            } else {
                documents?.some((d, index) => {
                    if (d === item) {
                        nbSelected += 1;
                        d.selected = true;
                        documents.setItem(index, d);
                        return true;
                    }
                });
            }
        }
    }

    function unselectItem(item: Item) {
        // DEV_LOG && console.log('unselectItem', item);
        if (item.selected) {
            if (item.folder) {
                if (folderViewStyle === 'horizontal') {
                    folderItems?.some((d, index) => {
                        if (d.folder === item.folder) {
                            nbSelected -= d.folder.count;
                            d.selected = false;
                            folderItems.setItem(index, d);
                            return true;
                        }
                    });
                } else {
                    documents?.some((d, index) => {
                        if (d.folder === item.folder) {
                            nbSelected -= 1;
                            d.selected = false;
                            documents.setItem(index, d);
                            return true;
                        }
                    });
                }
            } else {
                documents?.some((d, index) => {
                    if (d === item) {
                        nbSelected -= 1;
                        d.selected = false;
                        documents.setItem(index, d);
                        return true;
                    }
                });
            }
        }
    }

    function getTotalNbDocuments() {
        if (folderViewStyle === 'horizontal') {
            return documents.length - 1; // -1 because the first the first item is actually the folders horizontal collectionview
        }
        return documents.length;
    }
    function getTotalNbDocumentsWithFolders() {
        if (folderViewStyle === 'horizontal') {
            return getTotalNbDocuments() + folderItems.length;
        }
        return getTotalNbDocuments();
    }
    function getDocumentsStartIndex() {
        if (folderViewStyle === 'horizontal') {
            return folderItems.length > 0 ? 1 : 0; // 1 because the first the first item is actually the folders horizontal collectionview
        }
        return 0;
    }
    function unselectAll() {
        nbSelected = 0;
        const startIndex = getDocumentsStartIndex();
        if (documents) {
            documents.splice(startIndex, documents.length - startIndex, ...documents.slice(startIndex).map((i) => ({ ...i, selected: false })));
        }
        if (folderItems) {
            folderItems.splice(0, folderItems.length, ...folderItems.map((i) => ({ ...i, selected: false })));
        }
    }
    function selectAll() {
        let newCount = getTotalNbDocuments(); // -1 because the first
        const startIndex = getDocumentsStartIndex();
        DEV_LOG && console.log('selectAll', documents.length);
        if (documents) {
            documents.splice(startIndex, documents.length - startIndex, ...documents.slice(startIndex).map((i) => ({ ...i, selected: true })));
        }
        if (folderItems) {
            folderItems.splice(
                0,
                folderItems.length,
                ...folderItems.map((i) => {
                    newCount += i.folder.count;
                    return { ...i, selected: true };
                })
            );
        }
        nbSelected = newCount;
    }
    export function onItemLongPress(item: Item, event?) {
        DEV_LOG && console.log('onItemLongPress', !!item.selected, item.folder);
        if (item.selected) {
            unselectItem(item);
        } else {
            selectItem(item);
        }
    }

    export let handleDocumentTap = async (doc: OCRDocument) => {
        await goToDocumentView(doc);
    };
    export let handleFolderTap = async (folder: DocFolder) => {
        await goToFolderView(folder);
    };
    export const onItemTapFast = async function (item: Item) {
        DEV_LOG && console.log('onItemTapFast');
        try {
            if (nbSelected > 0) {
                onItemLongPress(item);
            } else if (item.doc) {
                await handleDocumentTap(item.doc);
            } else if (item.folder) {
                await handleFolderTap(item.folder);
            }
        } catch (error) {
            showError(error);
        }
    };
    export const onItemTapThrottled = throttle(onItemTapFast, 500);

    export const onItemTap = async function (item: Item) {
        if (nbSelected > 0) {
            return onItemTapFast(item);
        }
        return onItemTapThrottled(item);
    };

    function onGoBack(data) {
        if (editingTitle) {
            if (data) {
                data.cancel = true;
            }
            editingTitle = false;
        } else if (showSearch) {
            if (data) {
                data.cancel = true;
            }
            search.hideSearch();
        }
    }
    function actionBarOnGoBack() {
        if (showSearch) {
            search.hideSearch();
        } else if (modal) {
            closeModal();
        } else if (Frame.topmost().canGoBack()) {
            goBack();
        }
    }
    const onAndroidBackButton = (data: AndroidActivityBackPressedEventData) =>
        onBackButton(page?.nativeView, () => {
            DEV_LOG && console.log('onBackButton');
            if (nbSelected > 0) {
                data.cancel = true;
                unselectAll();
            } else {
                onGoBack(data);
            }
        });

    async function fullscreenSelectedDocuments() {
        const component = (await import('~/components/FullScreenImageViewer.svelte')).default;
        navigate({
            page: component,
            // transition: __ANDROID__ ? SharedTransition.custom(new PageTransition(300, undefined, 10), {}) : undefined,
            props: {
                images: (await getSelectedDocuments()).reduce((acc, doc) => {
                    doc.pages.forEach((page) =>
                        acc.push({
                            image: page.imagePath,
                            // sharedTransitionTag: `document_${doc.id}_${page.id}`,
                            name: doc.name,
                            ...page
                        })
                    );
                    return acc;
                }, []),
                startPageIndex: 0
            }
        });
    }
    function getSelectedItems() {
        const selected: Item[] = [];
        for (let index = 0; index < folderItems.length; index++) {
            const d = folderItems.getItem(index);
            if (d.selected) {
                selected.push(d);
            }
        }
        for (let index = getDocumentsStartIndex(); index < documents.length; index++) {
            const d = documents.getItem(index);
            if (d.selected) {
                selected.push(d);
            }
        }
        return selected;
    }

    export async function getSelectedDocuments() {
        const selected: OCRDocument[] = [];
        if (!documentsService.started) {
            return selected;
        }
        for (let index = getDocumentsStartIndex(); index < documents.length; index++) {
            const d = documents.getItem(index);
            if (d.doc && d.selected) {
                selected.push(d.doc);
            } else if (d.folder && d.selected) {
                selected.push(...(await documentsService.documentRepository.findDocuments({ folder: d.folder })));
            }
        }
        for (let index = 0; index < folderItems.length; index++) {
            const d = folderItems.getItem(index);
            if (d.folder && d.selected) {
                selected.push(...(await documentsService.documentRepository.findDocuments({ folder: d.folder })));
            }
        }
        return sortByKey(selected, sortOrder);
    }

    // async function loopSelectedDocuments(callback: (d: OCRDocument) => void) {
    //     if (!documentsService.started) {
    //         return;
    //     }
    //     for (let index = getDocumentsStartIndex(); index < documents.length; index++) {
    //         const d = documents.getItem(index);
    //         if (d.doc && d.selected) {
    //             callback(d.doc);
    //         }
    //     }
    //     for (let index = 0; index < folderItems.length; index++) {
    //         const d = folderItems.getItem(index);
    //         if (d.folder && d.selected) {
    //             const docs = await documentsService.documentRepository.findDocuments({ folder: d.folder });
    //             for (let i = 0; i < docs.length; i++) {
    //                 callback(docs[i]);
    //             }
    //         }
    //     }
    // }
    async function getSelectedPagesAndPossibleSingleDocument(): Promise<[{ page: OCRPage; document: OCRDocument }[], OCRDocument, OCRDocument[]]> {
        const selected: { page: OCRPage; document: OCRDocument }[] = [];
        const docs = await getSelectedDocuments();
        docs.forEach((doc) => {
            selected.push(...doc.pages.map((page) => ({ page, document: doc })));
        });

        return [selected, docs.length === 1 ? docs[0] : undefined, docs];
    }
    async function toggleFavoriteSelectedDocuments() {
        const selectedDocs = await getSelectedDocuments();
        const allFavorite = selectedDocs.every((d) => d.favorite === 1);
        const newFavorite = allFavorite ? 0 : 1;
        for (const doc of selectedDocs) {
            await doc.save({ favorite: newFavorite }, false, false);
        }
        unselectAll();
        refresh();
    }
    async function deleteSelectedDocuments() {
        if (nbSelected > 0) {
            try {
                const result = await confirm({
                    cancelButtonText: lc('cancel'),
                    message: lc('confirm_delete_documents', nbSelected),
                    okButtonText: lc('delete'),
                    title: lc('delete')
                });
                if (result) {
                    await documentsService.deleteDocuments(await getSelectedDocuments());
                    return true;
                }
                return false;
            } catch (error) {
                showError(error);
            }
        }
    }

    async function showViewOptions(event) {
        try {
            const options = new ObservableArray(
                ([{ type: 'header', title: lc('view_style') }] as any)
                    .concat(
                        Object.keys(viewStyles).map((k) => ({
                            id: k,
                            ...viewStyles[k],
                            boxType: 'circle',
                            type: 'checkbox',
                            value: viewStyle === k,
                            group: 'viewStyle',
                            color: (item) => (item.value ? colorPrimary : undefined)
                        }))
                    )
                    .concat(
                        Object.keys(folderViewStyles).length > 1
                            ? [
                                  { type: 'header', title: lc('folder_view_style') },
                                  ...Object.keys(folderViewStyles).map((k) => ({
                                      id: k,
                                      ...folderViewStyles[k],
                                      boxType: 'circle',
                                      type: 'checkbox',
                                      value: folderViewStyle === k,
                                      group: 'folderViewStyle',
                                      color: (item) => (item.value ? colorPrimary : undefined)
                                  }))
                              ]
                            : []
                    )
                    .concat([{ type: 'header', title: lc('sort') }])
                    .concat(
                        Object.keys(sortKeys).map((k) => ({
                            id: k,
                            ...sortKeys[k],
                            boxType: 'circle',
                            type: 'checkbox',
                            value: sortOrder.includes(k),
                            group: 'sort'
                            // color: (item) => (!!item.value ? colorPrimary : undefined)
                        }))
                    )
                    .concat([{ type: 'checkbox', title: lc('ascending'), id: 'ascending', value: sortOrder.indexOf('ASC') !== -1 }])
            );
            // const options = Object.keys(OPTIONS[option]).map((k) => ({ ...OPTIONS[option][k], id: k }));
            await showPopoverMenu({
                anchor: event.object,
                options,
                vertPos: VerticalPosition.BELOW,
                props: {
                    width: 200 * $fontScale,
                    autoSizeListItem: true,
                    onCheckBox: (item, value) => {
                        if (item.group === 'viewStyle') {
                            const changed = viewStyleChanged(item.id, viewStyle);

                            viewStyle = item.id;
                            ApplicationSettings.setString(SETTINGS_VIEW_STYLE, viewStyle);
                            updateColumns($isLandscape);
                            if (changed) {
                                collectionView?.nativeView.refresh();
                            }
                        } else if (item.group === 'folderViewStyle') {
                            const changed = folderViewStyleChanged(item.id, folderViewStyle);

                            folderViewStyle = item.id;
                            ApplicationSettings.setString(SETTINGS_FOLDER_VIEW_STYLE, folderViewStyle);
                            if (changed) {
                                refresh();
                            }
                        } else if (item.group === 'sort') {
                            if (item.id === 'useCount') {
                                // Most-used is always descending (most used first)
                                sortOrder = 'useCount DESC';
                            } else {
                                const currentAscending = sortOrder.indexOf('ASC') !== -1;
                                sortOrder = `${item.id} ${currentAscending ? 'ASC' : 'DESC'}`;
                            }
                            ApplicationSettings.setString(SETTINGS_SORT_ORDER, sortOrder);
                            refreshThrottle();
                        } else if (item.id === 'ascending') {
                            const currentKey = sortOrder.split(' ')[0];
                            if (currentKey !== 'useCount') {
                                sortOrder = `${currentKey} ${value ? 'ASC' : 'DESC'}`;
                                ApplicationSettings.setString(SETTINGS_SORT_ORDER, sortOrder);
                                refreshThrottle();
                            }
                        }
                    }
                }
            });
        } catch (error) {
            showError(error);
        }
    }

    async function syncDocuments() {
        try {
            if (syncEnabled) {
                await syncService.syncDocuments({ bothWays: true, force: true });
            }
        } catch (error) {
            showError(error);
        }
    }
    export const refreshCollectionView = debounce(() => {
        foldersCollectionView?.nativeView?.refresh();
        collectionView?.nativeView?.refresh();
    }, 10);
    export const refreshVisibleItems = () => {
        foldersCollectionView?.nativeView?.refreshVisibleItems();
        collectionView?.nativeView?.refreshVisibleItems();
    };
    onThemeChanged(() => {
        foldersCollectionView?.nativeView?.refresh();
        collectionView?.nativeView?.refresh();
    });
    onFontScaleChanged(refreshVisibleItems);
    onFolderBackgroundColorChanged(refreshCollectionView);
    syncServicesStore.subscribe(refreshVisibleItems);

    let lottieDarkFColor;
    let lottieLightColor;
    $: {
        if (colorPrimaryContainer) {
            lottieDarkFColor = new Color(colorPrimaryContainer);
            const realTheme = getRealTheme();
            if (realTheme === 'light') {
                lottieLightColor = new Color(colorPrimaryContainer).darken(10);
            } else {
                lottieLightColor = new Color(colorPrimaryContainer).lighten(10);
            }
        }
    }

    function onFolderCanvasDraw(item: Item, { canvas, object }: { canvas: Canvas; object: CanvasView }) {
        const w = canvas.getWidth();
        const h = canvas.getHeight();
        const dx = 10;
        const { folder: itemFolder } = item;
        textPaint.color = colorOnBackground;
        const topText = createNativeAttributedString({
            spans: [
                {
                    fontFamily: $fonts.mdi,
                    fontSize: 20 * $fontScale,
                    color: !$folderBackgroundColor && itemFolder.color ? itemFolder.color : colorOutline,
                    lineHeight: 20 * $fontScale,
                    text: 'mdi-folder '
                },
                {
                    fontSize: 16 * $fontScale,
                    fontWeight: 'bold',
                    lineBreak: 'end',
                    lineHeight: 16 * $fontScale,
                    text: folder ? itemFolder.name.replace(folder.name + '/', '') : itemFolder.name
                },
                {
                    fontSize: 14 * $fontScale,
                    color: colorOutline,
                    lineHeight: 20 * $fontScale,
                    text: '\n' + lc('documents_count', itemFolder.count)
                }
            ]
        });
        canvas.save();
        const staticLayout = new StaticLayout(topText, textPaint, Math.max(0, w - dx), LayoutAlignment.ALIGN_NORMAL, 1, 0, true);
        canvas.translate(dx, h / 2 - staticLayout.getHeight() / 2);
        staticLayout.draw(canvas);
        canvas.restore();
    }

    async function showImageExportPopover(event) {
        const selection = await getSelectedPagesAndPossibleSingleDocument();
        return showImagePopoverMenu(selection[0], event.object, { vertPos: VerticalPosition.ABOVE });
    }

    function getSelectionToolbarOptions() {
        // Main actions that appear in the toolbar (configurable, default 4)
        return [
            { icon: 'mdi-file-pdf-box', id: 'pdf', name: lc('export_pdf') },
            { icon: 'mdi-share-variant', id: 'share', name: lc('share_images') },
            { icon: 'mdi-auto-fix', id: 'transform', name: lc('transform_images') },
            { icon: 'mdi-text-recognition', id: 'ocr', name: lc('ocr_document') },
            { id: 'select_all', name: lc('select_all'), icon: 'mdi-select-all' },
            { color: colorError, icon: 'mdi-delete', id: 'delete', name: lc('delete') },
            ...(nbSelected === 1 ? [{ icon: 'mdi-rename', id: 'rename', name: lc('rename') }] : []),
            { icon: 'mdi-star', id: 'favorite', name: lc('toggle_favorite') },
            { icon: 'mdi-folder-swap', id: 'move_folder', name: lc('move_folder') },
            { icon: 'mdi-fullscreen', id: 'fullscreen', name: lc('show_fullscreen_images') }
        ];
    }

    async function handleSelectionAction(event, option: OptionType) {
        try {
            let result;
            switch (option.id) {
                case 'pdf':
                    const data = await getSelectedPagesAndPossibleSingleDocument();
                    await showPDFPopoverMenu({ pages: data[0], document: data[1], documents: data[2], anchor: event.object, popoverOptions: { vertPos: VerticalPosition.ABOVE } });
                    break;
                case 'select_all':
                    if (nbSelected === getTotalNbDocumentsWithFolders()) {
                        unselectAll();
                    } else {
                        selectAll();
                    }
                    break;
                case 'rename':
                    const item = getSelectedItems()[0];
                    result = await prompt({
                        title: lc('rename'),
                        defaultText: (item.doc || item.folder).name
                    });
                    if (result.result && result.text?.length) {
                        await (item.doc || item.folder).save({
                            name: result.text
                        });
                    }
                    break;
                case 'share':
                    const selection = await getSelectedPagesAndPossibleSingleDocument();
                    result = await showImagePopoverMenu(selection[0], event.object, { vertPos: VerticalPosition.ABOVE });
                    if (result) {
                        unselectAll();
                    }
                    break;
                case 'fullscreen':
                    await fullscreenSelectedDocuments();
                    unselectAll();
                    break;
                case 'ocr':
                    result = await detectOCR({ documents: await getSelectedDocuments() });
                    if (result) {
                        unselectAll();
                    }
                    break;
                case 'transform':
                    result = await transformPages({ documents: await getSelectedDocuments() });
                    if (result) {
                        unselectAll();
                    }
                    break;
                case 'delete':
                    result = await deleteSelectedDocuments();
                    if (result) {
                        unselectAll();
                    }
                    break;
                case 'favorite':
                    await toggleFavoriteSelectedDocuments();
                    break;
                case 'move_folder':
                    const selected = await getSelectedDocuments();
                    let defaultFolder;
                    DEV_LOG && console.log('move_folder', folders);
                    const folderName = await promptForFolderName(
                        defaultFolder,
                        Object.values(folders).filter((g) => g.name !== 'none')
                    );
                    if (typeof folderName === 'string') {
                        for (let index = 0; index < selected.length; index++) {
                            const doc = selected[index];
                            await doc.setFolder({ folderName: folderName === 'none' ? undefined : folderName });
                        }
                        unselectAll();
                    }
                    break;
            }
        } catch (error) {
            showError(error);
        }
    }

    async function showOptions(event) {
        const options = new ObservableArray(
            [{ id: 'select_all', name: lc('select_all'), icon: 'mdi-select-all' }].concat(nbSelected === 1 ? [{ icon: 'mdi-rename', id: 'rename', name: lc('rename') }] : []).concat([
                { icon: 'mdi-star', id: 'favorite', name: lc('toggle_favorite') },
                { icon: 'mdi-folder-swap', id: 'move_folder', name: lc('move_folder') },
                { icon: 'mdi-share-variant', id: 'share', name: lc('share_images') },
                { icon: 'mdi-fullscreen', id: 'fullscreen', name: lc('show_fullscreen_images') },
                { icon: 'mdi-auto-fix', id: 'transform', name: lc('transform_images') },
                { icon: 'mdi-text-recognition', id: 'ocr', name: lc('ocr_document') },
                { color: colorError, icon: 'mdi-delete', id: 'delete', name: lc('delete') }
            ] as any)
        );
        return showPopoverMenu({
            anchor: event.object,
            onClose: async (item) => {
                try {
                    let result;
                    switch (item.id) {
                        case 'select_all':
                            selectAll();
                            break;
                        case 'rename':
                            const item = getSelectedItems()[0];
                            result = await prompt({
                                title: lc('rename'),
                                defaultText: (item.doc || item.folder).name
                            });
                            if (result.result && result.text?.length) {
                                await (item.doc || item.folder).save({
                                    name: result.text
                                });
                            }
                            break;
                        case 'share':
                            result = await showImageExportPopover(event);
                            if (result) {
                                unselectAll();
                            }
                            break;
                        case 'fullscreen':
                            await fullscreenSelectedDocuments();
                            unselectAll();
                            break;
                        case 'ocr':
                            result = await detectOCR({ documents: await getSelectedDocuments() });
                            if (result) {
                                unselectAll();
                            }
                            break;
                        case 'transform':
                            result = await transformPages({ documents: await getSelectedDocuments() });
                            if (result) {
                                unselectAll();
                            }
                            break;
                        case 'delete':
                            result = await deleteSelectedDocuments();
                            if (result) {
                                unselectAll();
                            }
                            break;
                        case 'favorite':
                            await toggleFavoriteSelectedDocuments();
                            break;
                        case 'move_folder':
                            const selected = await getSelectedDocuments();
                            let defaultFolder;
                            DEV_LOG && console.log('move_folder', folders);
                            const folderName = await promptForFolderName(
                                defaultFolder,
                                Object.values(folders).filter((g) => g.name !== 'none')
                            );
                            if (typeof folderName === 'string') {
                                for (let index = 0; index < selected.length; index++) {
                                    const doc = selected[index];
                                    await doc.setFolder({ folderName: folderName === 'none' ? undefined : folderName });
                                }
                                unselectAll();
                            }
                            break;
                    }
                } catch (error) {
                    showError(error);
                }
            },
            options,
            vertPos: VerticalPosition.BELOW
        });
    }
    async function setFolderColor(event) {
        try {
            const color: string = await pickFolderColor(folder, event);
            if (color) {
                folder = folder; //for svelte to pick up change
            }
        } catch (error) {
            showError(error);
        }
    }
    function itemTemplateSpanSize(item: Item) {
        if (item.type === 'folders') {
            return $nbColumns;
        }
        return 1;
    }
    const openSyncSettings = tryCatchFunction(async () =>
        showSettings({
            subSettingsOptions: 'sync'
        })
    );
</script>

<page bind:this={page} id="documentList" actionBarHidden={true} on:navigatedTo={onNavigatedTo} on:navigatingFrom={onNavigatingFrom}>
    <gridlayout class="pageContent" rows="auto,auto,*">
        <slot name="abovecollectionview" />
        <collectionView
            bind:this={collectionView}
            {colWidth}
            ios:autoReloadItemOnLayout={true}
            itemTemplateSelector={(item) => itemTemplateSelector(viewStyle, item)}
            items={documents}
            paddingBottom={Math.max($windowInset.bottom, BOTTOM_BUTTON_OFFSET)}
            row={2}
            spanSize={itemTemplateSpanSize}
            {...collectionViewOptions}>
            <Template key="folder" let:item>
                <ListItemAutoSize
                    class="card"
                    columns="auto,*"
                    fontSize={17}
                    fontWeight="600"
                    item={{ ...item, title: folder ? item.folder.name.replace(folder.name + '/', '') : item.folder.name, subtitle: lc('documents_count', item.folder.count) }}
                    mainCol={1}
                    margin="4 8 4 8"
                    padding="0 10 0 10"
                    subtitleFontSize={12}
                    useExtraPadding={false}
                    on:longPress={(e) => onItemLongPress(item, e)}
                    on:tap={() => onItemTap(item)}>
                    <label
                        col={0}
                        color={!$folderBackgroundColor && item.folder.color ? item.folder.color : colorOutline}
                        fontFamily={$fonts.mdi}
                        fontSize={24}
                        padding="0 10 0 0"
                        text="mdi-folder "
                        verticalAlignment="center" />
                    <SelectedIndicator col={0} horizontalAlignment="left" margin="10 0 10 0" selected={item.selected} verticalAlignment="top" />
                </ListItemAutoSize>
            </Template>
            <Template key="folders" let:item>
                <collectionView
                    bind:this={foldersCollectionView}
                    colWidth={150}
                    height={70}
                    items={folderItems}
                    orientation="horizontal"
                    row={1}
                    visibility={folders?.length ? 'visible' : 'collapsed'}>
                    <Template let:item>
                        <canvasview
                            backgroundColor={($folderBackgroundColor && item.folder.color) || isEInk ? 'transparent' : colorSurfaceContainerLow}
                            borderColor={colorOutline}
                            borderRadius={12}
                            borderWidth={1}
                            margin="4 8 0 8"
                            rippleColor={colorSurface}
                            on:tap={() => onItemTap(item)}
                            on:longPress={(e) => onItemLongPress(item, e)}
                            on:draw={(e) => onFolderCanvasDraw(item, e)}>
                            <SelectedIndicator horizontalAlignment="right" margin={10} selected={item.selected} verticalAlignment="top" />
                            <!-- <SyncIndicator synced={item.doc._synced} visible={syncEnabled} /> -->
                            <!-- <PageIndicator horizontalAlignment="right" margin={10} text={item.doc.pages.length} /> -->
                        </canvasview>
                    </Template>
                </collectionView>
            </Template>
            <slot />
        </collectionView>
        <progress backgroundColor="transparent" busy={true} indeterminate={true} row={2} verticalAlignment="top" visibility={loading ? 'visible' : 'hidden'} />
        {#if showNoDocument}
            <flexlayout
                flexDirection="column"
                horizontalAlignment="center"
                marginBottom="30%"
                paddingLeft={16}
                paddingRight={16}
                row={2}
                verticalAlignment="center"
                width="80%"
                transition:fade={{ duration: 200 }}>
                <lottie
                    async={true}
                    autoPlay={PRODUCTION}
                    flexShrink={2}
                    keyPathColors={{
                        'background|**': lottieDarkFColor,
                        'full|**': lottieLightColor,
                        'lines|**': lottieDarkFColor,
                        'scanner|**': lottieLightColor
                    }}
                    loop={true}
                    src="~/assets/lottie/scanning.lottie" />
                <label
                    color={colorOnSurfaceVariant}
                    flexShrink={0}
                    fontSize={19}
                    text={lastRefreshFilter && showSearch ? lc('no_document_found') : lc('no_document_yet')}
                    textAlignment="center"
                    textWrap={true} />
            </flexlayout>
        {/if}
        {#if showActionButton}
            <slot name="fab" />
        {/if}

        <CActionBar modalWindow={modal || showSearch} onGoBack={actionBarOnGoBack} onTitleTap={folder ? () => (editingTitle = true) : null} {title}>
            {#if !onlyForImport}
                <mdbutton
                    class="actionBarButton"
                    class:infinite-rotate={syncRunning}
                    isEnabled={!syncRunning}
                    text="mdi-autorenew"
                    variant="text"
                    visibility={!folder && syncEnabled ? 'visible' : 'collapse'}
                    on:tap={syncDocuments}
                    on:longPress={openSyncSettings} />
            {/if}
            <mdbutton class="actionBarButton" text="mdi-magnify" variant="text" on:tap={() => search.showSearch()} />
            {#if !onlyForImport}
                {#if folder}
                    <mdbutton class="actionBarButton" testID="settingsBtn" text="mdi-palette" variant="text" on:tap={setFolderColor} />
                {:else}
                    <mdbutton class="actionBarButton" text="mdi-view-dashboard" variant="text" on:tap={showViewOptions} />
                    <mdbutton class="actionBarButton" testID="settingsBtn" text="mdi-cogs" variant="text" on:tap={() => showSettings()} />
                {/if}
            {/if}
            <ActionBarSearch bind:this={search} slot="center" {refresh} bind:visible={showSearch} />
        </CActionBar>
        {#if nbSelected > 0}
            <CActionBar forceCanGoBack={true} onGoBack={unselectAll} title={l('selected', nbSelected)} titleProps={{ autoFontSize: true, maxLines: 1 }} />
        {/if}
        {#if !onlyForImport && nbSelected > 0}
            <SelectionToolbar onAction={handleSelectionAction} options={getSelectionToolbarOptions()} row={2} />
        {/if}
        {#if !onlyForImport && editingTitle}
            <EditNameActionBar {folder} bind:editingTitle />
        {/if}
    </gridlayout>
</page>
