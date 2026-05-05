<svelte:options accessors />

<script lang="ts">
    import { CheckBox } from '@nativescript-community/ui-checkbox';
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { VerticalPosition } from '@nativescript-community/ui-popover';
    import { ApplicationSettings, View } from '@nativescript/core';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { Writable } from 'svelte/store';
    import { lc } from '~/helpers/locale';
    import { MatricesTypes, Matrix } from '~/utils/color_matrix';
    import {
        DEFAULT_BRIGHTNESS,
        DEFAULT_COLORMATRIX,
        DEFAULT_COLORTYPE,
        DEFAULT_CONTRAST,
        DEFAULT_TRANSFORM,
        FILTER_COL_WIDTH,
        FILTER_ROW_HEIGHT,
        SETTINGS_DEFAULT_BRIGHTNESS,
        SETTINGS_DEFAULT_COLORMATRIX,
        SETTINGS_DEFAULT_COLORTYPE,
        SETTINGS_DEFAULT_CONTRAST,
        SETTINGS_DEFAULT_TRANSFORM,
        TRANSFORMS_SPLIT
    } from '~/utils/constants';
    import { TRANSFORMS } from '~/utils/localized_constant';
    import { showError } from '@shared/utils/showError';
    import { ColorMatricesTypes, getColorMatrix, showMatrixLevelPopover, showPopoverMenu, showSlidersPopover } from '~/utils/ui';
    import { colors, screenHeightDips, screenWidthDips, windowInset } from '~/variables';
    import ListItem from '@shared/components/ListItem.svelte';
    import ListItemAutoSize from '@shared/components/ListItemAutoSize.svelte';

    // technique for only specific properties to get updated on store change
    $: ({ colorPrimary } = $colors);

    const textFieldWidth = (screenWidthDips - 20 - 22 - 16) / 2;

    export let showCameraSettings = false;

    export let transforms = ApplicationSettings.getString(SETTINGS_DEFAULT_TRANSFORM, DEFAULT_TRANSFORM).split(TRANSFORMS_SPLIT);
    export let colorType = ApplicationSettings.getString(SETTINGS_DEFAULT_COLORTYPE, DEFAULT_COLORTYPE) as MatricesTypes;
    export let contrast = ApplicationSettings.getNumber(SETTINGS_DEFAULT_CONTRAST, DEFAULT_CONTRAST);
    export let brightness = ApplicationSettings.getNumber(SETTINGS_DEFAULT_BRIGHTNESS, DEFAULT_BRIGHTNESS);
    // export let colorMatrix = JSON.parse(ApplicationSettings.getString(SETTINGS_DEFAULT_COLORMATRIX, DEFAULT_COLORMATRIX));

    export let colorMatrix: Matrix;
    try {
        colorMatrix = JSON.parse(ApplicationSettings.getString(SETTINGS_DEFAULT_COLORMATRIX, DEFAULT_COLORMATRIX));
    } catch (error) {
        colorMatrix = null;
        ApplicationSettings.remove(SETTINGS_DEFAULT_COLORMATRIX);
    }

    export let cameraOptionsStore: Writable<{ aspectRatio: string; stretch: string; viewsize: string; pictureSize: string }>;
    export let resolutions: { pictureSize: string; aspectRatio: string }[] = null;
    export let currentResolution: { pictureSize: string; aspectRatio: string } = null;

    $: DEV_LOG && showCameraSettings && console.log('cameraOptions', JSON.stringify($cameraOptionsStore));
    if (showCameraSettings) {
        DEV_LOG && console.log('CameraSettingsBottomSheet', JSON.stringify(currentResolution), JSON.stringify(resolutions));
    }

    // $: ({ aspectRatio, stretch, viewsize, pictureSize } = $cameraOptionsStore);

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
        transforms = transforms.filter((s) => !!s && s.length);
        DEV_LOG && console.log('addOrRemoveTransform', transforms);
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

    function updateCameraOption(option: string, value, item) {
        if (!showCameraSettings) {
            return;
        }
        DEV_LOG && console.log('updateOption', option, value, item);
        cameraOptionsStore.update((state) => {
            state[option] = value;
            if (option === 'pictureSize') {
                state['aspectRatio'] = item.aspectRatio;
            } else if (option === 'aspectRatio') {
                state['pictureSize'] = null;
            }

            return state;
        });
    }

    const OPTIONS: any[] = showCameraSettings
        ? [
              {
                  id: 'stretch',
                  title: lc('camera_preview_stretch'),
                  default: 'aspectFit',
                  options: {
                      aspectFit: { name: lc('aspect_fit') },
                      aspectFill: { name: lc('aspect_fill') }
                  }
              },
              {
                  id: 'viewsize',
                  title: lc('camera_view_size'),
                  default: 'limited',
                  options: {
                      full: { name: lc('full') },
                      limited: { name: lc('limited') }
                  }
              }
          ]
              .concat(
                  __ANDROID__
                      ? [
                            {
                                id: 'aspectRatio',
                                title: lc('aspect_ratio'),
                                default: '4:3',
                                options: {
                                    '4:3': { name: '4:3' },
                                    '16:9': { name: '16:9' }
                                }
                            }
                        ]
                      : ([] as any)
              )
              .concat(
                  __ANDROID__ && currentResolution && resolutions
                      ? [
                            {
                                id: 'pictureSize',
                                title: lc('picture_size'),
                                default: currentResolution.pictureSize,
                                options: resolutions.reduce((acc, current) => {
                                    acc[current.pictureSize] = { ...current, name: `${current.pictureSize} (${current.aspectRatio})` };
                                    return acc;
                                }, {})
                            }
                        ]
                      : ([] as any)
              )
        : [];

    async function selectOption(item, event, valueTransformer?, fullRefresh = true) {
        try {
            const options = Object.keys(item.options).map((k) => ({ ...item.options[k], id: k }));
            await showPopoverMenu({
                options,
                anchor: event.object,
                onClose: (value) => {
                    updateCameraOption(item.id, valueTransformer ? valueTransformer(value.id) : value.id, value);
                }
            });
        } catch (error) {
            showError(error);
        }
    }

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

    const maxHeight = 40 + (Object.keys(OPTIONS).length / 2) * 50 + 40 + Object.keys(TRANSFORMS).length * 70 + 80;
    const maxScreenHeight = screenHeightDips - $windowInset.top - $windowInset.bottom;

    function onCloseBottomSheet() {
        ApplicationSettings.setString(SETTINGS_DEFAULT_COLORTYPE, colorType);
        if (contrast !== DEFAULT_BRIGHTNESS) {
            ApplicationSettings.setNumber(SETTINGS_DEFAULT_CONTRAST, contrast);
        } else {
            ApplicationSettings.remove(SETTINGS_DEFAULT_CONTRAST);
        }
        if (brightness !== DEFAULT_BRIGHTNESS) {
            ApplicationSettings.setNumber(SETTINGS_DEFAULT_BRIGHTNESS, brightness);
        } else {
            ApplicationSettings.remove(SETTINGS_DEFAULT_BRIGHTNESS);
        }
        if (colorMatrix) {
            ApplicationSettings.setString(SETTINGS_DEFAULT_COLORMATRIX, JSON.stringify(colorMatrix));
        } else {
            ApplicationSettings.remove(SETTINGS_DEFAULT_COLORMATRIX);
        }
        ApplicationSettings.setString(SETTINGS_DEFAULT_TRANSFORM, transforms.join(TRANSFORMS_SPLIT));
    }

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

<gesturerootview padding="10 10 0 10" rows={maxHeight < maxScreenHeight ? 'auto' : '*'} on:closedBottomSheet={onCloseBottomSheet}>
    <scrollview>
        <stacklayout>
            {#if showCameraSettings}
                <label class="sectionHeader" text={lc('camera_settings')} />
                <wraplayout padding="10 0 10 0">
                    {#each OPTIONS as item (item.id)}
                        <textfield
                            editable={false}
                            hint={item.title}
                            margin="4 8 4 8"
                            text={item.options[$cameraOptionsStore[item.id] || item.default]?.name || $cameraOptionsStore[item.id]}
                            variant="outline"
                            width={textFieldWidth}
                            on:tap={(e) => selectOption(item, e)} />
                    {/each}
                </wraplayout>
            {/if}
            <label class="sectionHeader" text={lc('transformations')} />
            <stacklayout>
                {#each TRANSFORMS as item (item.id)}
                    <ListItem columns="*,auto" height={70} {item} on:tap={(e) => onTransformTap(item, e)}>
                        <checkbox
                            id="checkbox"
                            checked={transforms.indexOf(item.id) !== -1}
                            col={2}
                            ios:marginRight={10}
                            verticalAlignment="center"
                            on:checkedChange={(e) => onCheckedChanged(item, e)} />
                    </ListItem>
                {/each}
                <ListItemAutoSize item={{ title: lc('brightness'), rightValue: brightness.toFixed(2) }} on:tap={editBrightnessContrast} />
                <ListItemAutoSize item={{ title: lc('contrast'), rightValue: contrast.toFixed(2) }} on:tap={editBrightnessContrast} />
            </stacklayout>
            <label class="sectionHeader" text={lc('filters')} />
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
</gesturerootview>
