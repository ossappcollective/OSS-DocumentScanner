<script context="module" lang="ts">
    import { lc } from '@nativescript-community/l';
    import { createNativeAttributedString } from '@nativescript-community/text';
    import { LayoutAlignment, Paint, StaticLayout } from '@nativescript-community/ui-canvas';
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { AndroidActivityResultEventData, Application, ObservableArray, StackLayout, Utils } from '@nativescript/core';
    import { openFile, openUrl, throttle } from '@nativescript/core/utils';
    import { showError } from '@shared/utils/showError';
    import dayjs from 'dayjs';
    import { filesize } from 'filesize';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { isEInk } from '~/helpers/theme';
    import { DocFolder } from '~/models/OCRDocument';
    import { importImageFromCamera } from '~/utils/ui';
    import { colors, fontScale, hasCamera, windowInset } from '~/variables';
    import PageIndicator from '../common/PageIndicator.svelte';
    import RotableImageView from '../common/RotableImageView.svelte';
    import SelectedIndicator from '../common/SelectedIndicator.svelte';
    import SyncIndicator from '../common/SyncIndicator.svelte';
    import MainList, { Item } from './MainList.svelte';
    import { ellipsize } from '~/utils/utils.common';
    import FavoriteIndicator from '~/components/common/FavoriteIndicator.svelte';

    const textPaint = new Paint();
    const IMAGE_DECODE_WIDTH = Utils.layout.toDevicePixels(200);
</script>

<script lang="ts">
    let { colorOnBackground, colorOnSurfaceVariant } = $colors;
    $: ({ colorOnBackground, colorOnSurfaceVariant } = $colors);
    export let title = lc('documents');
    export let folder: DocFolder;
    let fabHolder: NativeViewElementNode<StackLayout>;
    let collectionView: NativeViewElementNode<CollectionView>;
    let viewStyle: string;
    let syncEnabled: boolean;
    let nbSelected: number = 0;
    let folderItems: ObservableArray<Item>;
    let documents: ObservableArray<Item>;
    let getSyncColors: (item: Item) => string[];
    let onItemLongPress: (item: Item, event?) => Promise<void>;
    let onItemTap: (item: Item) => Promise<void>;
    let importImages: () => Promise<void>;
    let importDocument: (importPDFs?: boolean) => Promise<void>;
    let refreshCollectionView: () => void;

    $: condensed = viewStyle === 'condensed';
    function getItemRowHeight(viewStyle) {
        return condensed ? 80 : 150;
    }
    function getImageMargin(viewStyle) {
        return 10;
    }
    function getItemImageHeight(viewStyle) {
        return condensed ? 44 : 94;
    }

    $: textPaint.color = colorOnBackground || 'black';

    function onCanvasDraw(item: Item, { canvas, object }: { canvas: Canvas; object: CanvasView }) {
        const w = canvas.getWidth();
        const h = canvas.getHeight();
        const dx = 10 + getItemImageHeight(viewStyle) * $fontScale + 16;
        textPaint.color = colorOnSurfaceVariant;
        const { doc } = item;
        textPaint.textSize = condensed ? 11 : 14 * $fontScale;
        canvas.drawText(
            filesize(
                doc.pages.reduce((acc, v) => acc + v.size, 0),
                { output: 'string' }
            ),
            dx,
            h - (condensed ? 0 : 16) - 10,
            textPaint
        );
        textPaint.color = colorOnBackground;
        const topText = createNativeAttributedString({
            spans: [
                {
                    fontSize: 16 * $fontScale,
                    fontWeight: 'bold',
                    lineBreak: 'end',
                    lineHeight: 18 * $fontScale,
                    text: ellipsize(doc.name, 50)
                },
                {
                    color: colorOnSurfaceVariant,
                    fontSize: 14 * $fontScale,
                    lineHeight: condensed ? 14 : 20 * $fontScale,
                    text: '\n' + dayjs(doc.createdDate).format('L LT')
                }
            ]
        });
        const staticLayout = new StaticLayout(topText, textPaint, Math.max(0, w - dx), LayoutAlignment.ALIGN_NORMAL, 1, 0, true);
        canvas.translate(dx, (condensed ? 0 : 10) + 10);
        staticLayout.draw(canvas);
    }

    async function onStartCam(inverseUseSystemCamera = false) {
        try {
            await importImageFromCamera({ folder, inverseUseSystemCamera });
        } catch (error) {
            showError(error);
        }
    }
</script>

<MainList
    folderViewStyles={{
        horizontal: { name: lc('horizontal') },
        vertical: { name: lc('vertical') }
    }}
    {title}
    viewStyles={{
        default: { name: lc('expanded') },
        condensed: { name: lc('condensed') }
    }}
    bind:fabHolder
    bind:viewStyle
    bind:onItemTap
    bind:onItemLongPress
    bind:syncEnabled
    bind:folder
    bind:nbSelected
    bind:importDocument
    bind:importImages
    bind:refreshCollectionView
    bind:getSyncColors
    bind:documents
    bind:folderItems
    bind:collectionView>
    <Template let:item>
        <canvasview
            class="card"
            borderWidth={isEInk ? 1 : 0}
            height={getItemRowHeight(viewStyle) * $fontScale}
            on:tap={() => onItemTap(item)}
            on:longPress={(e) => onItemLongPress(item, e)}
            on:draw={(e) => onCanvasDraw(item, e)}>
            <RotableImageView
                id="imageView"
                borderRadius={12}
                decodeWidth={IMAGE_DECODE_WIDTH}
                horizontalAlignment="left"
                item={item.doc.pages[0]}
                marginBottom={getImageMargin(viewStyle)}
                marginLeft={10}
                marginTop={getImageMargin(viewStyle)}
                sharedTransitionTag={`document_${item.doc.id}_${item.doc.pages[0]?.id}`}
                stretch="aspectFill"
                width={getItemImageHeight(viewStyle) * $fontScale} />
            <SelectedIndicator horizontalAlignment="left" margin={10} selected={item.selected} />
            <SyncIndicator syncColors={getSyncColors(item)} visible={syncEnabled} />
            <PageIndicator horizontalAlignment="right" margin={10} scale={$fontScale} text={item.doc.pages.length} />
            <FavoriteIndicator favorite={item.doc.favorite === 1} />
        </canvasview>
    </Template>

    <stacklayout bind:this={fabHolder} slot="fab" class="fabHolder" marginBottom={Math.min(60, $windowInset.bottom)} orientation="horizontal" row={2}>
        <mdbutton class="small-fab" horizontalAlignment="center" text="mdi-image-plus-outline" verticalAlignment="center" on:tap={throttle(() => importImages(), 500)} />
        <mdbutton
            class={$hasCamera ? 'small-fab' : 'fab'}
            horizontalAlignment="center"
            text="mdi-file-document-plus-outline"
            verticalAlignment="center"
            on:tap={throttle(() => importDocument(), 500)} />
        {#if $hasCamera}
            <mdbutton id="fab" class="fab" text="mdi-camera" verticalAlignment="center" on:tap={throttle(() => onStartCam(), 500)} on:longPress={() => onStartCam(true)} />
        {/if}
    </stacklayout>
</MainList>
