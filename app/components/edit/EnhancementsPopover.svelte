<script context="module" lang="ts">
    import { TRANSFORMS } from '~/utils/localized_constant';
    import OptionSelect from '@shared/components/OptionSelect.svelte';
    import { get } from 'svelte/store';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { FILTER_COL_WIDTH, FILTER_ROW_HEIGHT } from '~/utils/constants';
    import { ColorMatricesTypes, getColorMatrix } from '~/utils/matrix';
    import { lc } from '@nativescript-community/l';
    import { colors, fontScale } from '~/variables';
    const filters = ColorMatricesTypes.map((k) => ({
        ...k,
        text: lc(k.id),
        colorType: k.id
    }));
</script>

<script lang="ts">
    export let transforms;
    export let item;
    export let imagePath;
    export let imageRotation;
    export let applyImageColorMatrix;
    export let setColorMatrixLevels;
    export let onLoaded;

    $: ({ colorOutline, colorPrimary } = $colors);

    function getData(transformId) {
        const value = transforms.indexOf(transformId) !== -1;
        return { type: 'checkbox', id: transformId, value, data: value };
    }
    const options = (
        TRANSFORMS.map((i) => ({
            ...getData(i.id),
            ...i
        })) as any[]
    ).concat({
        type: 'filters'
    });

    function isCurrentColorType(i) {
        return item?.colorType === i.colorType;
    }
</script>

<OptionSelect height={FILTER_ROW_HEIGHT + options.length * ($$restProps?.rowHeight || 70) * get(fontScale)} {options} {...$$restProps}>
    <Template slot="templates" key="filters">
        <collectionview id="filters" colWidth={FILTER_COL_WIDTH} height={FILTER_ROW_HEIGHT} items={filters} orientation="horizontal" row={4} on:loaded={(e) => onLoaded(e.object)}>
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
                        {imageRotation}
                        src={imagePath}
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
        </collectionview>
    </Template>
</OptionSelect>
