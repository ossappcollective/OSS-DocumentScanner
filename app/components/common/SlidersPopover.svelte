<script context="module" lang="ts">
    import PopoverBackgroundView from '@shared/components/PopoverBackgroundView.svelte';
    import IconButton from '@shared/components/IconButton.svelte';
    import { colors } from '~/variables';
</script>

<script lang="ts">
    $: ({ colorPrimary } = $colors);
    export let items: { icon?: string; title?: string; min: number; max: number; step: number; value?: number; resetValue?: number; onChange: Function; formatter?: Function }[] = [];

    function onValueChange(item, event) {
        item.value = event.value; // needed for resetValue to update the slider
        if (item.onChange) {
            item.onChange(event.value);
        }
        items = items; // needed for resetValue to update the slider
    }
    function resetValue(item) {
        item.value = item.resetValue;
        if (item.onChange) {
            item.onChange(item.resetValue);
        }
        items = items; // needed to update the slider
    }
</script>

<PopoverBackgroundView rows={Array(items.length).fill('auto').join(',')} {...$$restProps}>
    {#each items as item, i (item.title)}
        <gridlayout columns="auto,*,auto,auto" row={i} rows="auto,auto">
            {#if item.icon}
                <label class="icon-label" marginTop={0} text={item.icon} />
            {/if}
            {#if item.title}
                <label col={1} marginTop={4} text={item.title} verticalTextAlignment="center" />
            {/if}
            <!-- <label row={1} text={(item.formatter?.(item.min) || item.min) + ''} textAlignment="center" verticalTextAlignment="center" /> -->
            <label col={2} row={1} text={(item.formatter?.(item.value) || item.value) + ''} textAlignment="center" verticalTextAlignment="center" />
            <slider colSpan={2} maxValue={item.max} minValue={item.min} row={1} stepSize={item.step} value={item.value} on:valueChange={(e) => onValueChange(item, e)} />

            {#if item.resetValue !== undefined}
                <IconButton col={3} color={colorPrimary} marginLeft={10} row={1} small={true} text="mdi-restore" verticalTextAlignment="center" on:tap={() => resetValue(item)} />
            {/if}
        </gridlayout>
    {/each}
</PopoverBackgroundView>
