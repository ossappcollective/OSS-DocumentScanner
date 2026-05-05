<script lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { Img } from '@nativescript-community/ui-image';
    import { showBottomSheet } from '@nativescript-community/ui-material-bottomsheet/svelte';
    import { confirm } from '@nativescript-community/ui-material-dialogs';
    import { Pager } from '@nativescript-community/ui-pager';
    import { HorizontalPosition, VerticalPosition } from '@nativescript-community/ui-popover';
    import { showPopover } from '@nativescript-community/ui-popover/svelte';
    import { AndroidActivityBackPressedEventData, Application, Frame, ObservableArray, Page, PageTransition, SharedTransition, View } from '@nativescript/core';
    import { debounce, throttle } from '@nativescript/core/utils';
    import { showError } from '@shared/utils/showError';
    import { goBack, showModal } from '@shared/utils/svelte/ui';
    import { OCRData, QRCodeData, Quad, getImageSize } from 'plugin-nativeprocessor';
    import { onDestroy, onMount } from 'svelte';
    import { Writable } from 'svelte/store';
    import CActionBar from '~/components/common/CActionBar.svelte';
    import RotableImageView from '~/components/common/RotableImageView.svelte';
    import { lc } from '~/helpers/locale';
    import { onThemeChanged } from '~/helpers/theme';
    import { ImportImageData, OCRDocument, OCRPage } from '~/models/OCRDocument';
    import { DocumentDeletedEventData, DocumentPageUpdatedEventData, DocumentUpdatedEventData, documentsService } from '~/services/documents';
    import { qrcodeService } from '~/services/qrcode';
    import { shortcutService } from '~/services/shortcuts';
    import { EVENT_DOCUMENT_DELETED, EVENT_DOCUMENT_PAGE_UPDATED, EVENT_DOCUMENT_UPDATED, TRANSFORMS_SPLIT } from '~/utils/constants';
    import {
        ColorMatricesTypes,
        copyOCRToClipboard,
        copyTextToClipboard,
        detectOCROnPage,
        getColorMatrix,
        hideLoading,
        onBackButton,
        pickColor,
        showImagePopoverMenu,
        showLoading,
        showMatrixLevelPopover,
        showPDFPopoverMenu,
        showSlidersPopover,
        showSnack
    } from '~/utils/ui';
    import { colors, fontScale, screenWidthDips, windowInset } from '~/variables';
    import EditNameActionBar from '../common/EditNameActionBar.svelte';
    import IconButton from '@shared/components/IconButton.svelte';
    import PageIndicator from '../common/PageIndicator.svelte';

    // technique for only specific properties to get updated on store change
    $: ({ colorOutline, colorPrimary, colorSurfaceContainer } = $colors);

    export let startPageIndex: number = 0;
    DEV_LOG && console.log('startPageIndex', startPageIndex);
    export let document: OCRDocument;
    export let transitionOnBack = true;
    let pager: NativeViewElementNode<Pager>;
    let enhanceFiltersCollectionView: CollectionView;
    let page: NativeViewElementNode<Page>;

    const items: ObservableArray<OCRPage> = document.getObservablePages();
    let currentIndex = startPageIndex;
    let currentItem: OCRPage;
    let currentItemOCRData: OCRData;
    let currentItemQRCodeData: QRCodeData;
    let currentItemSubtitle: string;
    let currentSelectedImagePath: string;
    let currentSelectedImageRotation: number;
    let transforms: string[];
    updateCurrentItem(items.getItem(currentIndex));
    const filters = ColorMatricesTypes.map((k) => ({
        ...k,
        text: lc(k.id),
        colorType: k.id
    }));
    let updatingTransform = false;
    let editingTitle = false;
    // const whitepaper = writable(transforms.indexOf('whitepaper') !== -1);
    // const enhanced = writable(transforms.indexOf('enhance') !== -1);

    async function showPDFPopover(event) {
        try {
            await showPDFPopoverMenu({ pages: document.pages.map((page) => ({ page, document })), document, documents: [document], anchor: event.object });
        } catch (err) {
            showError(err);
        }
    }
    async function detectOCR() {
        // let ocrImage;
        try {
            const ocrData = await detectOCROnPage(document, currentIndex);
            if (ocrData) {
                currentItemOCRData = ocrData;
            } else {
                showSnack({ message: lc('no_text_found_in_page') });
            }
        } catch (err) {
            showError(err);
        }
    }

    async function showOCRSettings() {
        try {
            const OCRSettingsBottomSheet = (await import('~/components/ocr/OCRSettingsBottomSheet.svelte')).default;
            const shouldStart = await showBottomSheet({
                parent: page,
                skipCollapsedState: true,
                view: OCRSettingsBottomSheet,
                props: {}
            });
            if (shouldStart) {
                detectOCR();
            }
        } catch (error) {
            showError(error);
        }
    }
    async function detectQRCode() {
        if (CARD_APP) {
            try {
                const qrcode = await qrcodeService.detectQRcode(document, currentIndex);
                if (!qrcode?.length) {
                    showSnack({ message: lc('no_qrcode_found') });
                } else {
                    currentItemQRCodeData = qrcode;
                    showSnack({ message: lc('qrcode_found') });
                }
            } catch (error) {
                showError(error);
            }
        }
    }
    function updateCurrentItem(item) {
        currentItem = item;
        currentItemSubtitle = currentItem ? `${currentItem.width} x ${currentItem.height}` : null;
        currentSelectedImagePath = currentItem?.imagePath;
        currentSelectedImageRotation = currentItem?.rotation || 0;
        currentItemOCRData = currentItem?.ocrData;
        currentItemQRCodeData = currentItem?.qrcode;
        transforms = currentItem?.transforms?.split(TRANSFORMS_SPLIT) || [];
    }
    function onSelectedIndex(event) {
        currentIndex = event.object.selectedIndex;
        // DEV_LOG && console.log('onSelectedIndex', currentIndex, items.length, currentSelectedImagePath, currentSelectedImageRotation, currentItemQRCodeData);

        updateCurrentItem(items.getItem(currentIndex));
        // $whitepaper = transforms.indexOf('whitepaper') !== -1;
        // $enhanced = transforms.indexOf('enhance') !== -1;
        refreshCollectionView();
    }
    // function onFirstLayout(item, e) {
    //     console.log('onFirstLayout');
    //     if (item.rotation % 180 === 90) {
    //         const currentWidth = layout.toDeviceIndependentPixels(e.object.getMeasuredWidth());
    //         const currentHeight = layout.toDeviceIndependentPixels(e.object.getMeasuredHeight());
    //         const delta = item.rotation % 180 === 0 ? 0 : (currentWidth - currentHeight) / 2;
    //         Object.assign(e.object, {
    //             translateX: delta,
    //             translateY: -delta,
    //             width: currentHeight,
    //             height: currentWidth
    //         });
    //     }
    // }

    async function onImageRotated(item, event) {
        try {
            const newRotation = event.newRotation;
            if (newRotation === undefined) {
                return;
            }
            DEV_LOG && console.log('onImageRotated', newRotation);
            await document.updatePage(
                currentIndex,
                {
                    rotation: newRotation % 360
                },
                false
            );
            if (CARD_APP) {
                shortcutService.updateShortcuts(document);
            }

            currentSelectedImageRotation = item.rotation;
            // items.setItem(currentIndex, item);
        } catch (error) {
            showError(error);
        }
    }
    // $: {
    //     colorType = document.pages[currentIndex].colorType || 0;
    // }
    // async function setColorType(type: number) {
    //     colorType = type;
    //     try {
    //         await document.updatePage(currentIndex, {
    //             colorType: type
    //             // colorMatrix: getColorMatrix(type)
    //         });
    //         // pages.setItem(currentIndex, current);
    //     } catch (err) {
    //         showError(err);
    //     }
    // }

    function getCurrentImageView() {
        return pager?.nativeView.getChildView(currentIndex)?.getViewById<Img>('imageView');
    }

    function applyRotation(newRotation) {
        getCurrentImageView()?.notify({ eventName: 'rotateAnimated', rotation: newRotation });
    }
    function getCurrentRotation() {
        const current = items.getItem(currentIndex);
        return current.rotation && !isNaN(current.rotation) ? current.rotation : 0;
    }
    async function rotateImageLeft() {
        try {
            applyRotation(getCurrentRotation() - 90);
        } catch (error) {
            showError(error);
        }
    }
    async function rotateImageRight() {
        try {
            applyRotation(getCurrentRotation() + 90);
        } catch (error) {
            showError(error);
        }
    }

    async function showEnhancements(event) {
        try {
            const EnhancementsPopover = (await import('~/components/edit/EnhancementsPopover.svelte')).default;
            await showPopover({
                backgroundColor: colorSurfaceContainer,
                view: EnhancementsPopover,
                anchor: event.object,
                horizPos: HorizontalPosition.ALIGN_LEFT,
                vertPos: VerticalPosition.ABOVE,
                props: {
                    transforms,
                    applyImageColorMatrix,
                    setColorMatrixLevels,
                    item: currentItem,
                    imageRotation: currentSelectedImageRotation,
                    imagePath: currentSelectedImagePath,
                    borderRadius: 10,
                    autoSizeListItem: true,
                    elevation: __ANDROID__ ? 3 : 0,
                    margin: 4,
                    fontWeight: 500,
                    backgroundColor: colorSurfaceContainer,
                    containerColumns: 'auto',
                    width: screenWidthDips - 50,
                    onLoaded(view) {
                        enhanceFiltersCollectionView = view;
                    },
                    onClose() {
                        enhanceFiltersCollectionView = null;
                    },
                    onCheckBox(item, value, e) {
                        if (updatingTransform) {
                            e.object.checked = !value;
                            return;
                        }
                        updateTransform(value, null, item.id);
                    }
                }
            });
        } catch (error) {
            DEV_LOG && console.log(error, error.stack);
            throw error;
        }
    }
    // async function showEnhancements(event) {
    //     try {
    //         // const OptionSelect = (await import('~/components/OptionSelect.svelte')).default;

    //         function getData(transformId) {
    //             const value = transforms.indexOf(transformId) !== -1;
    //             return { type: 'checkbox', id: transformId, value, data: value };
    //         }
    //         await showPopoverMenu({
    //             options: TRANSFORMS.map((i) => ({
    //                 ...getData(i.id),
    //                 ...i
    //             })),
    //             vertPos: VerticalPosition.ABOVE,
    //             anchor: event.object,
    //             props: {
    //                 rowHeight: 70,
    //                 width: screenWidthDips - 50,
    //                 onCheckBox(item, value, e) {
    //                     if (updatingTransform) {
    //                         e.object.checked = !value;
    //                         return;
    //                     }
    //                     updateTransform(value, null, item.id);
    //                 }
    //             }
    //         });
    //     } catch (error) {
    //         showError(error);
    //     }
    // }
    async function showImageExportPopover(event) {
        try {
            await showImagePopoverMenu([{ page: items.getItem(currentIndex), document }], event.object);
        } catch (err) {
            showError(err);
        }
    }

    async function showCurrentOCRData() {
        try {
            const item = items.getItem(currentIndex);

            const OCRDataBottomSheet = (await import('~/components/ocr/OCRDataView.svelte')).default;
            await showModal({
                page: OCRDataBottomSheet,
                fullscreen: true,
                props: {
                    pageIndex: currentIndex,
                    document,
                    ocrData: items.getItem(currentIndex).ocrData,
                    imagePath: item.imagePath,
                    imageWidth: item.width,
                    imageHeight: item.height,
                    rotation: item.rotation
                    // colorMatrix: getColorMatrix(item.colorType)
                }
            });
        } catch (error) {
            showError(error);
        }
    }

    async function showCurrentQRCodeData() {
        await qrcodeService.showQRCode([document.pages[currentIndex]], document, currentIndex);
    }
    let ignoreNextCollectionViewRefresh = false;
    const saveCurrentItemColorType = debounce(function (index, colorMatrix) {
        ignoreNextCollectionViewRefresh = true;
        DEV_LOG && console.log('colorMatrix changed', colorMatrix);
        try {
            document.updatePage(index, {
                colorMatrix
            });
        } catch (error) {
            showError(error);
        }
    }, 500);
    function onColorMatrixChange(colorType, value) {
        const current = items.getItem(currentIndex);
        current.colorType = colorType;
        current.colorMatrix = getColorMatrix(colorType, value);
        items.setItem(currentIndex, current);
        saveCurrentItemColorType(currentIndex, current.colorMatrix);
    }

    async function setColorMatrixLevels(item, event) {
        if (!item.range) {
            return;
        }

        try {
            const current = items.getItem(currentIndex);
            const currentValue = current.colorMatrix?.[0] || 1;
            DEV_LOG && console.log('setColorMatrixLevels', currentValue, current.colorMatrix);
            onColorMatrixChange(item.colorType, currentValue);

            await showMatrixLevelPopover({
                item,
                anchor: event.object,
                currentValue,
                onChange(value) {
                    if (current.colorMatrix?.[0] || 1 !== value) {
                        onColorMatrixChange(item.colorType, value);
                    }
                }
            });
        } catch (err) {
            showError(err);
        }
    }

    async function deleteCurrentPage() {
        try {
            DEV_LOG && console.log('deleteCurrentPage', currentIndex, document.pages.length);
            let result;
            const deleteDocument = document.pages.length === 1;
            if (deleteDocument) {
                result = await confirm({
                    title: lc('delete'),
                    message: lc('confirm_delete_document'),
                    okButtonText: lc('delete'),
                    cancelButtonText: lc('cancel')
                });
            } else {
                result = await confirm({
                    title: lc('delete_page', currentIndex),
                    message: lc('confirm_delete_page', currentIndex + 1),
                    okButtonText: lc('delete'),
                    cancelButtonText: lc('cancel')
                });
            }
            if (result) {
                if (deleteDocument) {
                    await documentsService.deleteDocuments([document]);
                } else {
                    await document.deletePage(currentIndex);
                    currentIndex = Math.min(currentIndex, items.length - 1);
                    pager.nativeView.scrollToIndexAnimated(currentIndex, true);
                }
            }
        } catch (err) {
            showError(err);
        }
    }

    function isCurrentColorType(i) {
        return currentItem?.colorType === i.colorType;
    }

    async function applyImageColorMatrix(i) {
        try {
            const current = items.getItem(currentIndex);
            current.colorMatrix = null;
            current.colorType = i.colorType;
            enhanceFiltersCollectionView?.refreshVisibleItems();
            ignoreNextCollectionViewRefresh = true;
            DEV_LOG && console.log('applyImageColorMatrix', i.colorType);
            await document.updatePage(currentIndex, {
                colorType: i.colorType,
                colorMatrix: getColorMatrix(i.colorType)
            });
            if (CARD_APP) {
                shortcutService.updateShortcuts(document);
            }
        } catch (error) {
            showError(error);
        }
    }

    const saveBrightnessContrast = debounce(
        (brightness: number, contrast: number) => {
            document.updatePage(currentIndex, {
                brightness,
                contrast
            });
        },
        500,
        { leading: false }
    );
    async function applyBrightnessContrast(brightness: number, contrast: number) {
        try {
            const current = items.getItem(currentIndex);
            current.colorMatrix = null;
            current.brightness = brightness;
            current.contrast = contrast;
            items.setItem(currentIndex, current);
            saveBrightnessContrast(brightness, contrast);
        } catch (error) {
            showError(error);
        }
    }

    async function editBrightnessContrast(event) {
        try {
            function getCurrentBrightness() {
                const current = items.getItem(currentIndex);
                return typeof current.brightness === 'string' ? null : current.brightness;
            }
            function getCurrentContrast() {
                const current = items.getItem(currentIndex);
                return typeof current.contrast === 'string' ? null : current.contrast;
            }
            await showSlidersPopover({
                debounceDuration: 0,
                anchor: event.object,
                vertPos: VerticalPosition.BELOW,
                items: [
                    {
                        title: lc('brightness'),
                        icon: 'mdi-brightness-5',
                        min: -100,
                        max: 100,
                        resetValue: 0,
                        step: __IOS__ ? 1 : undefined,
                        // value: 0,
                        formatter: (v) => (v / 100).toFixed(2),
                        value: Math.round(Math.max(-1, Math.min(getCurrentBrightness() ?? 0, 5)) * 100),
                        onChange: debounce((value) => {
                            applyBrightnessContrast(value / 100, getCurrentContrast());
                        }, 10)
                    },
                    {
                        title: lc('contrast'),
                        icon: 'mdi-contrast-box',
                        min: 0,
                        max: 200,
                        resetValue: 100,
                        formatter: (v) => (v / 100).toFixed(2),
                        step: __IOS__ ? 1 : undefined,
                        // value: 10,
                        value: Math.round(Math.max(0, Math.min(getCurrentContrast() ?? 1, 4)) * 100),
                        onChange: debounce((value) => {
                            applyBrightnessContrast(getCurrentBrightness(), value / 100);
                        }, 10)
                    }
                ]
            });
        } catch (error) {
            showError(error);
        }
    }

    async function updateImageUris() {
        await getCurrentImageView()?.updateImageUri();
        enhanceFiltersCollectionView?.eachChildAsync((c: View) => {
            c.getViewById<Img>('imageView')?.updateImageUri();
        });
        // refreshCollectionView();
    }

    async function updateTransform(value: boolean, store: Writable<boolean>, type: string) {
        if (updatingTransform) {
            store.set(!value);
            return;
        }
        updatingTransform = true;
        try {
            const page = items.getItem(currentIndex);
            const currentTransforms = (page.transforms?.split(TRANSFORMS_SPLIT) || []).filter((s) => s?.length);
            if (value) {
                if (currentTransforms.indexOf(type) === -1) {
                    await showLoading(lc('computing'));
                    currentTransforms.push(type);
                    await document.updatePageTransforms(currentIndex, currentTransforms.join(TRANSFORMS_SPLIT));
                }
            } else {
                const index = currentTransforms.indexOf(type);
                if (index !== -1) {
                    await showLoading(lc('computing'));
                    currentTransforms.splice(index, 1);
                    await document.updatePageTransforms(currentIndex, currentTransforms.join(TRANSFORMS_SPLIT));
                }
            }
            transforms = currentTransforms;
        } catch (error) {
            showError(error);
        } finally {
            updatingTransform = false;
            hideLoading();
        }
    }

    async function onDocumentPageUpdated(event: DocumentPageUpdatedEventData) {
        if (event.doc !== document) {
            return;
        }
        const index = event.pageIndex;
        if (index === currentIndex) {
            DEV_LOG && console.log('edit onDocumentPageUpdated', index, event.imageUpdated);
            const item = items.getItem(index);
            updateCurrentItem(item);
            // TODO: fix the pager something is wrong if we
            items.setItem(index, item);
            if (!!event.imageUpdated) {
                await updateImageUris();
            } else {
                if (ignoreNextCollectionViewRefresh) {
                    ignoreNextCollectionViewRefresh = false;
                    return;
                }
                refreshCollectionView();
            }
        }
    }

    function onDocumentUpdated(event: DocumentUpdatedEventData) {
        if (document === event.doc) {
            document = event.doc;
        }
    }
    function onDocumentsDeleted(event: DocumentDeletedEventData) {
        if (event.documents.indexOf(document) !== -1) {
            const frame = Frame.topmost();
            goBack({
                frame,
                backStackEntry: frame.backStack[0],
                // null is important to say no transition! (override enter transition)
                transition: null
            });
        }
    }
    onMount(() => {
        if (__ANDROID__) {
            Application.android.on(Application.android.activityBackPressedEvent, onAndroidBackButton);
        }
        documentsService.on(EVENT_DOCUMENT_UPDATED, onDocumentUpdated);
        documentsService.on(EVENT_DOCUMENT_DELETED, onDocumentsDeleted);
        documentsService.on(EVENT_DOCUMENT_PAGE_UPDATED, onDocumentPageUpdated);
    });
    onDestroy(() => {
        DEV_LOG && console.log('DocumentEdit onDestroy');
        if (__ANDROID__) {
            Application.android.off(Application.android.activityBackPressedEvent, onAndroidBackButton);
        }
        documentsService.off(EVENT_DOCUMENT_UPDATED, onDocumentUpdated);
        documentsService.off(EVENT_DOCUMENT_DELETED, onDocumentsDeleted);
        documentsService.off(EVENT_DOCUMENT_PAGE_UPDATED, onDocumentPageUpdated);
    });

    function refreshCollectionView() {
        enhanceFiltersCollectionView?.refreshVisibleItems();
    }

    let cropItem: ImportImageData = {};
    async function cropEdit() {
        //TODO: recrop into modal window
        try {
            const item = items.getItem(currentIndex);
            if (!item) {
                return;
            }
            if (!item.sourceImageWidth) {
                const size = await getImageSize(item.sourceImagePath);
                item.sourceImageWidth = size.width;
                item.sourceImageHeight = size.height;
                item.sourceImageRotation = 0;
            }
            cropItem = {
                imagePath: item.sourceImagePath,
                imageWidth: item.sourceImageWidth,
                imageHeight: item.sourceImageHeight,
                imageRotation: item.sourceImageRotation,
                undos: [],
                redos: []
            };
            const quad = JSON.parse(JSON.stringify(item.crop));
            const component = (await import('~/components/edit/CropEditView.svelte')).default;
            const result: Quad = await showModal({
                page: component,
                fullscreen: true,
                props: {
                    cropItem,
                    quad
                }
            });
            if (result) {
                showLoading(lc('computing'));
                await document.updatePageCrop(currentIndex, result);
            }
        } catch (error) {
            showError(error);
        } finally {
            hideLoading();
        }
    }
    function refreshPager() {
        pager?.nativeView?.refresh();
    }
    onThemeChanged(refreshPager);

    function onGoBack() {
        if (editingTitle) {
            editingTitle = false;
        } else {
            const item = items.getItem(currentIndex);
            DEV_LOG && console.log('onGoBack', currentIndex, !!items, !!item);
            //we use a new transition to transition the selected item
            goBack({
                transition:
                    __ANDROID__ && !CARD_APP && transitionOnBack && item
                        ? SharedTransition.custom(new PageTransition(300, undefined, 10), {
                              pageStart: {
                                  sharedTransitionTags: {
                                      [`document_${document.id}_${item.id}`]: {}
                                  }
                              }
                          })
                        : CARD_APP
                          ? null
                          : undefined
            } as any);
        }
    }
    const onAndroidBackButton = (data: AndroidActivityBackPressedEventData) =>
        onBackButton(page?.nativeView, () => {
            data.cancel = true;
            onGoBack();
        });

    function copyText() {
        try {
            if (currentItemOCRData) {
                copyOCRToClipboard(currentItemOCRData.text);
            }
        } catch (error) {
            showError(error);
        }
    }
    function copyQRCodeText() {
        try {
            if (currentItemQRCodeData) {
                copyTextToClipboard(currentItemQRCodeData[0].text);
            }
        } catch (error) {
            showError(error);
        }
    }

    async function changeColor(item: OCRPage, event) {
        try {
            const currentColor = item?.extra?.color ?? 'black';
            const newColor = await pickColor(currentColor, { anchor: event.object });
            if (newColor) {
                // editingUpdates.extra = editingUpdates.extra || {};
                // topBackgroundColor = editingUpdates.extra.color = newColor.hex;
                // updateExtraItem(item, 'type');

                await document.updatePage(
                    currentIndex,
                    {
                        extra: { color: newColor.hex }
                    },
                    false
                );
            }
        } catch (error) {
            showError(error);
        }
    }
</script>

<page bind:this={page} id="pdfEdit" actionBarHidden={true}>
    <gridlayout class="pageContent" rows="auto,*,auto,auto,auto" android:paddingBottom={$windowInset.bottom}>
        <pager bind:this={pager} {items} row={1} selectedIndex={startPageIndex} transformers="zoomOut" on:selectedIndexChange={onSelectedIndex}>
            <Template let:item>
                <gridlayout width="100%">
                    <RotableImageView id="imageView" {item} sharedTransitionTag={`document_${document.id}_${item.id}`} zoomable={true} on:rotated={(e) => onImageRotated(item, e)} />
                </gridlayout>
            </Template>
        </pager>
        <PageIndicator horizontalAlignment="right" margin={10} row={2} scale={$fontScale} text={`${currentIndex + 1}/${items.length}`} verticalAlignment="bottom" />

        <label fontSize={14} horizontalAlignment="left" padding={10} row={2} text={currentItemSubtitle} verticalTextAlignment="center" />

        {#if CARD_APP}
            <absolutelayout
                backgroundColor={currentItem?.extra?.color ?? 'black'}
                borderColor={colorOutline}
                borderRadius="50%"
                borderWidth={2}
                height={40}
                horizontalAlignment="right"
                margin={10}
                row={1}
                verticalAlignment="top"
                width={40}
                on:tap={(event) => changeColor(currentItem, event)} />
        {/if}

        <stacklayout backgroundColor="#00000055" borderRadius={10} horizontalAlignment="right" marginRight={5} row={1} verticalAlignment="center">
            <IconButton color="white" text="mdi-share-variant" tooltip={lc('share')} on:tap={showImageExportPopover} />
            <IconButton color="white" isVisible={!!currentItemOCRData} onLongPress={copyText} text="mdi-format-textbox" on:tap={showCurrentOCRData} />
            {#if CARD_APP}
                <IconButton color="white" isVisible={currentItemQRCodeData?.length > 0} onLongPress={copyQRCodeText} text="mdi-qrcode" on:tap={showCurrentQRCodeData} />
            {/if}
        </stacklayout>

        <stacklayout borderTopColor={colorOutline} borderTopWidth={1} orientation="horizontal" row={4}>
            <mdbutton class="icon-btn" text="mdi-crop" variant="text" on:tap={() => cropEdit()} />
            <mdbutton class="icon-btn" text="mdi-rotate-left" variant="text" on:tap={() => rotateImageLeft()} />
            <mdbutton class="icon-btn" text="mdi-rotate-right" variant="text" on:tap={() => rotateImageRight()} />
            <mdbutton class="icon-btn" text="mdi-auto-fix" variant="text" on:tap={showEnhancements} />
            <mdbutton class="icon-btn" text="mdi-brightness-6" variant="text" on:tap={editBrightnessContrast} />
            {#if CARD_APP}
                <mdbutton class="actionBarButton" text="mdi-qrcode-scan" variant="text" on:tap={detectQRCode} />
            {/if}
            <mdbutton class="actionBarButton" text="mdi-text-recognition" variant="text" on:tap={showOCRSettings} />
        </stacklayout>
        <!-- <collectionview bind:this={collectionView} id="filters" colWidth={FILTER_COL_WIDTH} height={FILTER_ROW_HEIGHT} items={filters} orientation="horizontal" row={4}>
            <Template let:item>
                <gridlayout id={item.text} padding={2} on:tap={() => applyImageColorMatrix(item)} on:longPress={(event) => setColorMatrixLevels(item, event)}>
                    <image
                        id="imageView"
                        borderColor={colorPrimary}
                        borderRadius={4}
                        borderWidth={isCurrentColorType(item) ? 3 : 0}
                        colorMatrix={getColorMatrix(item.colorType)}
                        decodeHeight={120}
                        decodeWidth={120}
                        imageRotation={currentSelectedImageRotation}
                        src={currentSelectedImagePath}
                        stretch="aspectFill" />
                    <label
                        backgroundImage="linear-gradient(0deg, rgba(0,0,0,1) 0%, rgba(0,0,0,0.4) 90%, rgba(0,0,0,0) 100%)"
                        ios:selectable={true}
                        borderRadius="0 0 4 4"
                        color="white"
                        fontSize={11}
                        fontWeight="500"
                        text={item.text}
                        textAlignment="center"
                        verticalAlignment="bottom" />
                </gridlayout>
            </Template>
        </collectionview> -->
        <CActionBar {onGoBack} onTitleTap={() => (editingTitle = true)} title={document.name} titleProps={{ autoFontSize: true, padding: 0 }}>
            <mdbutton class="actionBarButton" text="mdi-file-pdf-box" variant="text" on:tap={showPDFPopover} />
            <mdbutton class="actionBarButton" text="mdi-delete" variant="text" on:tap={deleteCurrentPage} />
        </CActionBar>
        {#if editingTitle}
            <EditNameActionBar {document} bind:editingTitle />
        {/if}
    </gridlayout>
</page>
