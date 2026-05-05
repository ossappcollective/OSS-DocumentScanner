<script context="module" lang="ts">
    import { lc } from '@nativescript-community/l';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { Paint } from '@nativescript-community/ui-canvas';
    import { CollectionViewWithSwipeMenu } from '@nativescript-community/ui-collectionview-swipemenu';
    import { showBottomSheet } from '@nativescript-community/ui-material-bottomsheet/svelte';
    import { ApplicationSettings, ObservableArray, StackLayout } from '@nativescript/core';
    import { throttle } from '@nativescript/core/utils';
    import { prefs } from '@shared/services/preferences';
    import { showError } from '@shared/utils/showError';
    import { onMount } from 'svelte';
    import { Writable, writable } from 'svelte/store';
    import CardListCell from '~/components/list/CardListCell.svelte';
    import { DocFolder, OCRDocument } from '~/models/OCRDocument';
    import {
        CARD_RATIO,
        DEFAULT_CARD_ALWAYS_SHOW_NAME,
        DEFAULT_NB_COLUMNS,
        DEFAULT_NB_COLUMNS_LANDSCAPE,
        SETTINGS_CARD_ALWAYS_SHOW_NAME,
        SETTINGS_NB_COLUMNS,
        SETTINGS_NB_COLUMNS_LANDSCAPE
    } from '~/utils/constants';
    import { documentHasPKPassData } from '~/utils/pkpass';
    import { goToDocumentAfterScan, importImageFromCamera } from '~/utils/ui';
    import { colors, fontScale, hasCamera, isLandscape, screenHeightDips, screenWidthDips, windowInset } from '~/variables';
    import MainList, { Item } from './MainList.svelte';

    const textPaint = new Paint();
    const rowMargin = 8;
</script>

<script lang="ts">
    let { colorOnBackground, colorOnPrimary, colorOnSurfaceVariant, colorSurface } = $colors;
    $: ({ colorOnBackground, colorOnPrimary, colorOnSurfaceVariant, colorSurface } = $colors);
    let fabHolder: NativeViewElementNode<StackLayout>;
    let collectionView: NativeViewElementNode<CollectionViewWithSwipeMenu>;
    let viewStyle: string;
    let nbColumns: Writable<number>;
    let syncEnabled: boolean;

    export let alwaysShowNames = ApplicationSettings.getBoolean(SETTINGS_CARD_ALWAYS_SHOW_NAME, DEFAULT_CARD_ALWAYS_SHOW_NAME);
    prefs.on(`key:${SETTINGS_CARD_ALWAYS_SHOW_NAME}`, () => (alwaysShowNames = ApplicationSettings.getBoolean(SETTINGS_CARD_ALWAYS_SHOW_NAME, DEFAULT_CARD_ALWAYS_SHOW_NAME)));

    export let folder: DocFolder;
    let getSyncColors: (item: Item) => string[];
    let onItemLongPress: (item: Item, event?) => Promise<void>;
    let onItemTap: (item: Item) => Promise<void>;
    let importDocument: (importPDFs?: boolean) => Promise<void>;
    let importImages: () => Promise<void>;
    let refreshCollectionView: () => void;

    $: condensed = viewStyle === 'condensed';

    const title = lc('cards');
    let itemWidth = (($isLandscape ? screenHeightDips : screenWidthDips) - $windowInset.left - $windowInset.right) / $nbColumns - 2 * rowMargin;
    const itemHeight = writable(itemWidth * CARD_RATIO + 2 * rowMargin);

    let folderItems: ObservableArray<Item>;
    let documents: ObservableArray<Item>;
    $: itemWidth = (($isLandscape ? screenHeightDips : screenWidthDips) - $windowInset.left - $windowInset.right) / $nbColumns - 2 * rowMargin;
    $: {
        $itemHeight = itemWidth * CARD_RATIO + 2 * rowMargin;
        refreshCollectionView?.();
    }

    $: textPaint.color = colorOnBackground || 'black';
    $: textPaint.textSize = (condensed ? 11 : 14) * $fontScale;
    $: itemRowHeight = getItemRowHeight(viewStyle, $itemHeight, $nbColumns, $isLandscape);

    async function onAddButton() {
        DEV_LOG && console.log('onAddButton');
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
                        id: 'create',
                        name: lc('create'),
                        icon: 'mdi-plus'
                    }
                ]);
            const height = Math.min(rowHeight * options.length, 400);
            const option = await showBottomSheet({
                parent: this,
                view: OptionSelect,
                ignoreTopSafeArea: true,
                peekHeight: height,
                props: {
                    rowHeight,
                    height,
                    options
                }
            });
            DEV_LOG && console.log('on add option', option);
            if (option) {
                switch (option.id) {
                    case 'camera':
                        await importImageFromCamera({ inverseUseSystemCamera: false });
                        break;
                    case 'import':
                        await importDocument();
                        break;
                    case 'import_image':
                        await importImages();
                        break;
                    case 'create':
                        const CreateCard = (await import('~/components/widgets/CreateCard.svelte')).default;
                        const result = await showBottomSheet({
                            parent: this,
                            view: CreateCard,
                            ignoreTopSafeArea: true
                        });
                        DEV_LOG && console.log('CreateCard', result);
                        if (result) {
                            const document = await OCRDocument.createDocument(
                                [
                                    {
                                        qrcode: result.code?.length
                                            ? [
                                                  {
                                                      text: result.code,
                                                      format: result.codeFormat
                                                  }
                                              ]
                                            : undefined
                                    }
                                ],
                                folder,
                                {
                                    name: result.name,
                                    ...(result.color ? { extra: { color: result.color } } : {})
                                }
                            );
                            await goToDocumentAfterScan(document, -1, true);
                        }
                        break;
                }
            }
        } catch (error) {
            showError(error);
        }
    }
    function getItemOverlap(viewStyle, itemHeight) {
        // DEV_LOG && console.log('getItemOverlap', viewStyle, itemHeight);
        switch (viewStyle) {
            case 'full':
                return (item, position) => {
                    if (position < $nbColumns + (folderItems.length ? 1 : 0)) {
                        return [0, 0, 0, 0];
                    }
                    return [-0.71 * itemHeight, 0, 0, 0];
                };
            // case 'cardholder':
            //     return (item, position) => {
            //         if (position < $nbColumns + (folderItems.length ? 1 : 0)) {
            //             return [0, 0, 0, 0];
            //         }
            //         return [-0.11 * itemHeight, 0, 0, 0];
            //     };
            default:
                return null;
        }
    }
    function getItemRowHeight(viewStyle, itemHeight, nbColumns, isLandscape) {
        switch (viewStyle) {
            case 'cardholder':
                return itemHeight * (isLandscape ? 0.6 : 0.4);
            default:
                return itemHeight;
        }
    }
    function itemTemplateSelector(viewStyle, item?: Item) {
        if (item?.type) {
            return item.type;
        }
        const prefix = item?.doc && documentHasPKPassData(item.doc) ? 'pkpass_' : '';
        switch (viewStyle) {
            case 'list':
            case 'columns':
                if ($isLandscape) {
                    return prefix + 'columns';
                }
                return prefix + viewStyle;
            default:
                return prefix + viewStyle;
        }
    }
    function onFullCardItemTouch(item: Item, event) {
        const index = documents.findIndex((d) => d.doc && d.doc.id === item.doc.id);
        handleTouchAction(index, event);
    }
    function handleTouchAction(index, event) {
        // console.info('handleTouchAction', index, event.action);
        // switch (event.action) {
        //     case 'move':
        //         return;
        //     case 'down':
        //         if (menuShowingIndex !== -1) {
        //             // there is going to be a close event from the opened menu let s ignore it
        //             ignoreNextClose = true;
        //         }
        //         if (menuShowingIndex === -1) {
        //             translateCards(index);
        //         } else if (menuShowingIndex < index) {
        //             hideCards(menuShowingIndex, index - 1);
        //             translateCards(index);
        //         } else if (menuShowingIndex > index) {
        //             translateCards(index, menuShowingIndex);
        //         }
        //         menuShowingIndex = index;
        //         break;
        //     case 'up':
        //     case 'cancel':
        //         if (ignoreNextClose) {
        //             ignoreNextClose = false;
        //             return;
        //         }
        //         hideCards(index);
        //         menuShowingIndex = -1;
        //         break;
        // }
    }
    function updateColumns(isLandscape, orientationChanged: boolean = false) {
        $nbColumns = isLandscape
            ? ApplicationSettings.getNumber(SETTINGS_NB_COLUMNS_LANDSCAPE, DEFAULT_NB_COLUMNS_LANDSCAPE)
            : ApplicationSettings.getNumber(SETTINGS_NB_COLUMNS, viewStyle === 'columns' ? 2 : DEFAULT_NB_COLUMNS);
        if (orientationChanged) {
            refreshCollectionView?.();
        }
    }
    onMount(() => {
        // DEV_LOG && console.log('CardsList', 'onMount', viewStyle);
        updateColumns($isLandscape);
    });

    // let canvastest: NativeViewElementNode<CanvasView>;
    // let pkPassTestImage;
    // $: pkpass = documents?.getItem(0)?.doc?.pages?.[0]?.pkpass;
    // $: if (pkpass) {
    //     pkpassToImage(pkpass, {
    //         lang: getActualLanguage(),
    //         width: screenWidthDips,
    //         layout: 'full',
    //         includeBackFields: true
    //     })
    //         .then((img) => (pkPassTestImage = img))
    //         .catch((err) => console.error(err, err.stack));
    // }
</script>

<MainList
    collectionViewOptions={{
        // spanSize: (item) => itemTemplateSpanSize(viewStyle, item),
        swipeMenuId: 'swipeMenu',
        itemOverlap: getItemOverlap(viewStyle, $itemHeight),
        'on:swipeMenuClose': (e) => handleTouchAction(e.index, { action: 'up' })
    }}
    folderViewStyles={{
        horizontal: { name: lc('horizontal') },
        vertical: { name: lc('vertical') }
    }}
    {itemTemplateSelector}
    {title}
    {updateColumns}
    viewStyleChanged={(oldValue, newValue) => itemTemplateSelector(oldValue) !== itemTemplateSelector(newValue)}
    viewStyles={{
        cardholder: { name: lc('cardholder'), icon: 'mdi-view-agenda' },
        full: { name: lc('full'), icon: 'mdi-view-split-horizontal' },
        columns: { name: lc('columns'), icon: 'mdi-view-grid' },
        list: { name: lc('list'), icon: 'mdi-view-day' }
    }}
    bind:fabHolder
    bind:nbColumns
    bind:viewStyle
    bind:onItemTap
    bind:onItemLongPress
    bind:syncEnabled
    bind:folder
    bind:importDocument
    bind:importImages
    bind:refreshCollectionView
    bind:getSyncColors
    bind:documents
    bind:folderItems
    bind:collectionView>
    <Template key="cardholder" let:item>
        <absolutelayout height={itemRowHeight}>
            <CardListCell
                {collectionView}
                height={$itemHeight}
                {item}
                itemHeight={itemRowHeight}
                {itemWidth}
                layout="cardholder"
                {nbColumns}
                {onFullCardItemTouch}
                syncColors={getSyncColors(item)}
                {syncEnabled}
                on:tap={() => onItemTap(item)}
                on:longPress={(e) => onItemLongPress(item, e)} />
            <absolutelayout boxShadow="0 0 8 rgba(1, 0, 0, 1)" height={3} top={itemRowHeight} width="100%" />
        </absolutelayout>
    </Template>
    <Template key="full" let:item>
        <CardListCell
            {alwaysShowNames}
            {collectionView}
            height={itemRowHeight}
            {item}
            itemHeight={itemRowHeight}
            {itemWidth}
            layout="full"
            {nbColumns}
            {onFullCardItemTouch}
            syncColors={getSyncColors(item)}
            {syncEnabled}
            on:tap={() => onItemTap(item)}
            on:longPress={(e) => onItemLongPress(item, e)} />
    </Template>
    <Template key="list" let:item>
        <CardListCell
            {alwaysShowNames}
            {collectionView}
            height={itemRowHeight}
            {item}
            itemHeight={itemRowHeight}
            {itemWidth}
            layout="list"
            {nbColumns}
            {onFullCardItemTouch}
            syncColors={getSyncColors(item)}
            {syncEnabled}
            on:tap={() => onItemTap(item)}
            on:longPress={(e) => onItemLongPress(item, e)} />
    </Template>
    <Template key="columns" let:item>
        <CardListCell
            {alwaysShowNames}
            {collectionView}
            height={itemRowHeight}
            {item}
            itemHeight={itemRowHeight}
            {itemWidth}
            layout="columns"
            {nbColumns}
            {onFullCardItemTouch}
            syncColors={getSyncColors(item)}
            {syncEnabled}
            on:tap={() => onItemTap(item)}
            on:longPress={(e) => onItemLongPress(item, e)} />
    </Template>
    <Template key="pkpass_cardholder" let:item>
        <absolutelayout height={itemRowHeight}>
            <CardListCell
                {alwaysShowNames}
                {collectionView}
                height={$itemHeight}
                {item}
                itemHeight={itemRowHeight}
                {itemWidth}
                layout="cardholder"
                {nbColumns}
                {onFullCardItemTouch}
                pkPassCell={true}
                syncColors={getSyncColors(item)}
                {syncEnabled}
                on:tap={() => onItemTap(item)}
                on:longPress={(e) => onItemLongPress(item, e)} />
            <absolutelayout boxShadow="0 0 8 rgba(1, 0, 0, 1)" height={3} top={itemRowHeight} width="100%" />
        </absolutelayout>
    </Template>
    <Template key="pkpass_full" let:item>
        <CardListCell
            {alwaysShowNames}
            {collectionView}
            height={itemRowHeight}
            {item}
            itemHeight={itemRowHeight}
            {itemWidth}
            layout="full"
            {nbColumns}
            {onFullCardItemTouch}
            pkPassCell={true}
            syncColors={getSyncColors(item)}
            {syncEnabled}
            on:tap={() => onItemTap(item)}
            on:longPress={(e) => onItemLongPress(item, e)} />
    </Template>
    <Template key="pkpass_list" let:item>
        <CardListCell
            {alwaysShowNames}
            {collectionView}
            height={itemRowHeight}
            {item}
            itemHeight={itemRowHeight}
            {itemWidth}
            layout="list"
            {nbColumns}
            {onFullCardItemTouch}
            pkPassCell={true}
            syncColors={getSyncColors(item)}
            {syncEnabled}
            on:tap={() => onItemTap(item)}
            on:longPress={(e) => onItemLongPress(item, e)} />
    </Template>
    <Template key="pkpass_columns" let:item>
        <CardListCell
            {alwaysShowNames}
            {collectionView}
            height={itemRowHeight}
            {item}
            itemHeight={itemRowHeight}
            {itemWidth}
            layout="columns"
            {nbColumns}
            {onFullCardItemTouch}
            pkPassCell={true}
            syncColors={getSyncColors(item)}
            {syncEnabled}
            on:tap={() => onItemTap(item)}
            on:longPress={(e) => onItemLongPress(item, e)} />
    </Template>

    <stacklayout bind:this={fabHolder} slot="fab" class="fabHolder" marginBottom={Math.min(60, $windowInset.bottom)} orientation="horizontal" row={2}>
        <mdbutton bind:this={fabHolder} class="fab" text="mdi-plus" on:tap={throttle(() => onAddButton(), 500)} />
    </stacklayout>

    <!-- <canvasview bind:this={canvastest} slot="test" row={2} on:draw={onDrawCanvasTest} on:tap={() => (canvastest.nativeView.visibility = 'hidden')}> </canvasview> -->
    <!-- <image
        bind:this={canvastest}
        slot="test"
        backgroundColor="red"
        margin={40}
        row={2}
        src={pkPassTestImage}
        stretch="aspectFit"
        verticalAlignment="top"
        on:tap={() => (canvastest.nativeView.visibility = 'hidden')} /> -->
</MainList>
