<script lang="ts">
    import { VerticalPosition } from '@nativescript-community/ui-popover';
    import { ObservableArray, Utils, View } from '@nativescript/core';
    import { showPopoverMenu } from '~/utils/ui';
    import { actionBarButtonHeight, colors, windowInset } from '~/variables';
    import { OptionType } from '@shared/components/OptionSelect.svelte';
    import { slide } from '@shared/utils/svelte/ui';

    export let options: OptionType[] | ObservableArray<OptionType> = [];
    export let onAction: (event, option: OptionType) => void | Promise<void> = null;
    let visibleOptions: OptionType[] | ObservableArray<OptionType> = [];
    let overflowOptions: OptionType[] | ObservableArray<OptionType> = [];
    let hasOverflow = false;
    $: ({ colorOnSurface, colorSurfaceContainer, colorSurfaceContainerHigh } = $colors);

    // Split options into visible and overflow
    // $: visibleOptions = options.slice(0, maxVisibleActions);
    // $: overflowOptions = options.slice(maxVisibleActions);
    // $: hasOverflow = overflowOptions.length > 0;

    async function handleAction(event, option: OptionType) {
        if (onAction) {
            await onAction(event, option);
        }
    }

    async function showMoreOptions(event) {
        const result = await showPopoverMenu({
            anchor: event.object,
            options: new ObservableArray(overflowOptions),
            vertPos: VerticalPosition.ABOVE,
            onClose: async (item) => {
                await handleAction(event, item);
            }
        });
    }
    function onLayoutChanged(event: EventData) {
        const width = Utils.layout.toDeviceIndependentPixels((event.object as View).getMeasuredWidth());
        const nbVisibleButtons = (width - 32) / $actionBarButtonHeight;
        if (options.length > nbVisibleButtons) {
            visibleOptions = options.slice(0, nbVisibleButtons - 1);
            overflowOptions = options.slice(nbVisibleButtons - 1);
            hasOverflow = true;
        } else {
            hasOverflow = false;
            visibleOptions = options;
            overflowOptions = [];
        }
    }
</script>

<gridlayout
    class="selectionToolbar"
    backgroundColor={colorSurfaceContainerHigh}
    columns={hasOverflow ? 'auto,*,auto' : '*'}
    marginBottom={$windowInset.bottom + 8}
    marginLeft={8}
    marginRight={8}
    marginTop={8}
    {...$$restProps}
    on:layoutChanged={onLayoutChanged}
    transition:slide={{ duration: 200 }}>
    <stacklayout col={hasOverflow ? 1 : 0} horizontalAlignment="center" orientation="horizontal">
        {#each visibleOptions as option, index}
            {@const color = typeof option.color === 'function' ? option['color'](options) : option.color}
            <mdbutton class="selectionToolbarButton" color={color || colorOnSurface} text={option.icon} variant="text" on:tap={(event) => handleAction(event, option)} />
        {/each}
    </stacklayout>
    {#if hasOverflow}
        <mdbutton class="selectionToolbarButton" col={2} color={colorOnSurface} text="mdi-dots-vertical" variant="text" on:tap={showMoreOptions} />
    {/if}
</gridlayout>
