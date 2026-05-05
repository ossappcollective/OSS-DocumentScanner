<svelte:options accessors />

<script lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { CheckBox } from '@nativescript-community/ui-checkbox';
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { closeBottomSheet } from '@nativescript-community/ui-material-bottomsheet/svelte';
    import { VerticalPosition } from '@nativescript-community/ui-popover';
    import { View } from '@nativescript/core';
    import ListItemAutoSize from '@shared/components/ListItemAutoSize.svelte';
    import SettingsSlider from '@shared/components/SettingsSlider.svelte';
    import { showError } from '@shared/utils/showError';
    import { lc } from '~/helpers/locale';
    import { MatricesTypes } from '~/utils/color_matrix';
    import { DEFAULT_BRIGHTNESS, DEFAULT_COLORMATRIX, DEFAULT_COLORTYPE, DEFAULT_CONTRAST, FILTER_COL_WIDTH, FILTER_ROW_HEIGHT } from '~/utils/constants';
    import { TRANSFORMS } from '~/utils/localized_constant';
    import { ColorMatricesTypes, getColorMatrix, showMatrixLevelPopover, showSlidersPopover } from '~/utils/ui';
    import { colors, screenHeightDips, windowInset } from '~/variables';

    // technique for only specific properties to get updated on store change
    $: ({ colorPrimary, colorSurfaceContainer } = $colors);

    export let transforms = [];
    export let colorType = DEFAULT_COLORTYPE as MatricesTypes;
    export let colorMatrix: number[] = DEFAULT_COLORMATRIX;
    export let contrast = DEFAULT_CONTRAST;
    export let brightness = DEFAULT_BRIGHTNESS;

    let collectionView: NativeViewElementNode<CollectionView>;

    const filters = ColorMatricesTypes.map((k) => ({
        ...k,
        text: lc(k.id),
        colorType: k.id
    }));

    function addOrRemoveTransform(transform: string) {
        const index = transforms.indexOf(transform);
        if (index !== -1) {
            transforms.splice(index, 1);
        } else {
            transforms.push(transform);
        }
        transforms = transforms.filter((s) => s?.length);
    }

    function refreshCollectionView() {
        collectionView?.nativeView?.refreshVisibleItems();
    }

    function setColorType(type) {
        colorType = type;
        colorMatrix = getColorMatrix(type);
        refreshCollectionView();
    }
    function onColorMatrixChange(type, value, refreshCV = false) {
        colorType = type;
        colorMatrix = getColorMatrix(colorType, value);
        if (refreshCV) {
            refreshCollectionView();
        }
    }
    async function setColorMatrixLevels(item, event) {
        if (!item.range) {
            return;
        }

        try {
            const currentValue = 1;
            DEV_LOG && console.log('setColorMatrixLevels', currentValue, colorMatrix);
            onColorMatrixChange(item.colorType, currentValue, true);
            await showMatrixLevelPopover({
                item,
                anchor: event.object,
                currentValue,
                onChange(value) {
                    if (colorMatrix?.[0] || 1 !== value) {
                        onColorMatrixChange(item.colorType, value);
                    }
                }
            });
        } catch (err) {
            showError(err);
        }
    }
    function isCurrentColorType(i) {
        return colorType === i.colorType;
    }

    // function toggleAspectRatio() {
    //     updateOption('aspectRatio', aspectRatio === '4:3' ? '16:9' : '4:3');
    // }

    let checkboxTapTimer;
    function clearCheckboxTimer() {
        if (checkboxTapTimer) {
            clearTimeout(checkboxTapTimer);
            checkboxTapTimer = null;
        }
    }
    function onCheckedChanged(item, event) {
        clearCheckboxTimer();
        addOrRemoveTransform(item.id);
    }
    function onTransformTap(item, event) {
        const checkboxView: CheckBox = (event.object as View).getViewById('checkbox');
        clearCheckboxTimer();
        checkboxTapTimer = setTimeout(() => {
            checkboxView.checked = !checkboxView.checked;
        }, 10);
    }
    function startUpdate() {
        console.log('startUpdate', colorType, colorMatrix);
        closeBottomSheet({
            brightness,
            contrast,
            transforms,
            colorType,
            colorMatrix
        });
    }
    function resetAll() {
        console.log('resetAll');
        closeBottomSheet({
            brightness: DEFAULT_BRIGHTNESS,
            contrast: DEFAULT_CONTRAST,
            transforms: [],
            colorType: DEFAULT_COLORTYPE,
            colorMatrix: null
        });
    }
    const maxHeight = 40 + Object.keys(TRANSFORMS).length * 70 + 80;
    const maxScreenHeight = screenHeightDips - $windowInset.top - $windowInset.bottom;

    async function editBrightnessContrast(event) {
        try {
            await showSlidersPopover({
                debounceDuration: 0,
                anchor: event.object,
                vertPos: VerticalPosition.BELOW,
                items: [
                    {
                        title: lc('brightness'),
                        icon: 'mdi-brightness-5',
                        min: -100,
                        max: 500,
                        resetValue: 0,
                        step: __IOS__ ? 1 : undefined,
                        // value: 0,
                        formatter: (v) => (v / 100).toFixed(2),
                        value: Math.round(Math.max(-1, Math.min(brightness ?? DEFAULT_BRIGHTNESS, 5)) * 100),
                        onChange: (value) => {
                            brightness = value / 100;
                        }
                    },
                    {
                        title: lc('contrast'),
                        icon: 'mdi-contrast-box',
                        min: 0,
                        max: 400,
                        resetValue: 100,
                        formatter: (v) => (v / 100).toFixed(2),
                        step: __IOS__ ? 1 : undefined,
                        // value: 10,
                        value: Math.round(Math.max(0, Math.min(contrast ?? DEFAULT_CONTRAST, 4)) * 100),
                        onChange: (value) => {
                            contrast = value / 100;
                        }
                    }
                ]
            });
        } catch (error) {
            showError(error);
        }
    }
</script>

<gesturerootview id="transformBottomSheet" columns="*,*" padding="10 10 0 10" rows={maxHeight < maxScreenHeight ? 'auto,auto' : '*,auto'}>
    <scrollview colSpan={2}>
        <stacklayout>
            <label class="sectionBigHeader" text={lc('transformations')} />
            <stacklayout>
                {#each TRANSFORMS as item (item.id)}
                    <ListItemAutoSize {item} on:tap={(e) => onTransformTap(item, e)}>
                        <checkbox id="checkbox" checked={transforms.indexOf(item.id) !== -1} col={1} verticalAlignment="center" on:checkedChange={(e) => onCheckedChanged(item, e)} />
                    </ListItemAutoSize>
                {/each}

                <SettingsSlider
                    formatter={(v) => (v / 100).toFixed(2)}
                    max={500}
                    min={-100}
                    onChange={(value) => (brightness = value / 100)}
                    step={1}
                    title={lc('brightness')}
                    value={Math.round(Math.max(-1, Math.min(brightness ?? DEFAULT_BRIGHTNESS, 5)) * 100)} />

                <SettingsSlider
                    formatter={(v) => (v / 100).toFixed(2)}
                    max={500}
                    min={-100}
                    onChange={(value) => (contrast = value / 100)}
                    step={1}
                    title={lc('contrast')}
                    value={Math.round(Math.max(-1, Math.min(contrast ?? DEFAULT_CONTRAST, 4)) * 100)} />
            </stacklayout>
            <label class="sectionBigHeader" text={lc('filters')} />
            <collectionview bind:this={collectionView} colWidth={FILTER_COL_WIDTH} height={FILTER_ROW_HEIGHT} items={filters} orientation="horizontal">
                <Template let:item>
                    <gridlayout padding={2} on:tap={() => setColorType(item.colorType)} on:longPress={(event) => setColorMatrixLevels(item, event)}>
                        <image
                            borderColor={colorPrimary}
                            borderRadius={4}
                            borderWidth={isCurrentColorType(item) ? 3 : 0}
                            colorMatrix={getColorMatrix(item.colorType)}
                            src="~/assets/images/filter_color.png" />
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
            </collectionview>
        </stacklayout>
    </scrollview>
    <mdbutton row={1} text={lc('apply')} on:tap={startUpdate} />
    <mdbutton col={1} margin="12 10 12 10" row={1} text={lc('reset')} variant="outline" on:tap={resetAll} />
</gesturerootview>
