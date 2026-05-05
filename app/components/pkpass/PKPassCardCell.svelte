<script lang="ts">
    import { lang } from '~/helpers/locale';
    import { PKPass, PKPassData, PKPassField, PKPassStructure, PKPassStyle, PKPassTransitType, PKPassType } from '~/models/PKPass';
    import { getFieldTextAlignment, getTransitIcon } from '~/utils/pkpass';
    import { colors } from '~/variables';
    import { Item } from '~/components/list/MainList.svelte';
    import { Color } from '@nativescript/core';

    const FIELD_LINE_HEIGHT = 15;

    export let item: Item = null;
    export let pkpass: PKPass;
    export let itemWidth: number;
    export let layout: string = null;

    let { colorOnBackground, colorOnPrimary, colorSurface } = $colors;
    $: ({ colorOnBackground, colorOnPrimary, colorSurface } = $colors);

    let passData: PKPassData;
    let foregroundColor: string;
    let labelColor: string;
    let backgroundColor: string;
    let logoImage: string;
    let iconImage: string;
    let thumbnailImage: string;
    let stripImage: string;
    let structure: PKPassStructure;
    let primaryFields: PKPassField[];
    let headerFields: PKPassField[];
    let secondaryFields: PKPassField[];
    let auxiliaryFields: PKPassField[];
    let secondaryFieldsCount: number;
    let primaryFieldsCount: number;
    let headerFieldsCount: number;
    let auxiliaryFieldsCount: number;
    let transitType: PKPassTransitType;
    let transitIcon: string;
    let withTransitionIcon: boolean;

    // Get organization name
    $: orgName = passData?.organizationName || getLocalizedText(passData?.logoText) || item?.doc?.name || '';

    $: updatePkPass(pkpass);

    function updatePkPass(pkpass: PKPass) {
        passData = pkpass?.passData;
        backgroundColor = passData?.backgroundColor || colorSurface;
        foregroundColor = passData?.foregroundColor || (new Color(backgroundColor).getBrightness() < 145 ? 'white' : 'black');
        labelColor = passData.labelColor || (new Color(backgroundColor).getBrightness() < 145 ? 'white' : 'black');
        // Apple PKPass image specifications - prefer @2x for quality
        logoImage = pkpass?.images?.logo2x || pkpass?.images?.logo; // Max 160x50 points
        iconImage = pkpass?.images?.icon2x || pkpass?.images?.icon; // 29x29 points
        thumbnailImage = pkpass?.images?.thumbnail2x || pkpass?.images?.thumbnail; // 90x90 points
        stripImage = pkpass?.images?.strip2x || pkpass?.images?.strip; // Variable dimensions
        structure = pkpass.getPassStructure();
        primaryFields = structure?.primaryFields || [];
        headerFields = structure?.headerFields || [];
        secondaryFields = structure?.secondaryFields || [];
        auxiliaryFields = structure?.auxiliaryFields || [];
        secondaryFieldsCount = secondaryFields.length;
        primaryFieldsCount = primaryFields.length;
        headerFieldsCount = headerFields.length;
        auxiliaryFieldsCount = auxiliaryFields.length;
        transitType = structure?.transitType;
        transitIcon = getTransitIcon(transitType);
        withTransitionIcon = transitIcon && primaryFieldsCount === 2;

        // Get organization name
        orgName = passData?.organizationName || getLocalizedText(passData?.logoText) || item?.doc?.name || '';
    }

    // Calculate scale factor based on card width (credit card aspect ratio ~1.586:1)
    $: scaleFactor = itemWidth / 320; // Base width 320px

    // Get formatted field value
    function getFieldValue(field: any): string {
        return pkpass.formatFieldValue(field, lang);
    }

    // Get formatted field label
    function getFieldLabel(field: any): string {
        if (!field) return null;
        return pkpass.getLocalizedFieldLabel(field, lang).toUpperCase() + '\n';
    }

    function getLocalizedText(text: string): string {
        return text ? pkpass.getLocalizedValue(text, lang) : text;
    }
</script>

<!-- Credit card sized layout with scalable content -->
<gridlayout {backgroundColor} borderRadius={12 * scaleFactor} {...$$restProps}>
    {#if stripImage && pkpass?.passType === PKPassType.ESpass}
        <image noRatioEnforce={true} opacity={0.3} rowSpan={4} src={stripImage} stretch="aspectFill" width="100%" />
    {/if}
    <!-- Strip or thumbnail banner at top if available -->
    {#if pkpass?.passType === PKPassType.PKPass}
        <image colSpan={3} height={60 * scaleFactor} row={0} src={stripImage || thumbnailImage} stretch="aspectFill" visibility={stripImage || thumbnailImage ? 'visible' : 'collapse'} />
    {/if}

    <!-- Content container with proper padding -->
    <gridlayout padding={12 * scaleFactor} rows={`auto,${pkpass?.getPassStyle() === PKPassStyle.BoardingPass ? '*' : 'auto'},auto,auto`}>
        <!-- Top row: Logo/Icon + Name (limited width) + Important right-side data -->
        <gridlayout columns="auto,*,auto" marginBottom={5 * scaleFactor} row={0}>
            <!-- Left: Logo (max 80px scaled) or Icon (20px scaled) + Name -->
            <stacklayout col={0} orientation="horizontal" verticalAlignment="center">
                <!-- Logo takes priority (Apple spec: max 160x50, scaled to 80px width max for card) -->
                <!-- Icon (Apple spec: 29x29, scaled to ~20px for card) + Name -->
                <image
                    height={(logoImage ? 40 : 20) * scaleFactor}
                    marginRight={(logoImage ? 8 : 6) * scaleFactor}
                    src={logoImage || iconImage}
                    stretch="aspectFit"
                    verticalAlignment="center"
                    visibility={logoImage || iconImage ? 'visible' : 'collapse'}
                    width={(logoImage ? 80 : 20) * scaleFactor} />
                <label
                    color={foregroundColor}
                    fontSize={14 * scaleFactor}
                    fontWeight="bold"
                    maxLines={1}
                    text={orgName}
                    verticalAlignment="center"
                    visibility={!logoImage ? 'visible' : 'collapse'}
                    width={120 * scaleFactor} />
            </stacklayout>

            <!-- Right: Important data (header fields like gate, seat, date) -->
            <gridlayout col={2} columns={Array.from('*'.repeat(headerFieldsCount)).join(',')} marginBottom={10 * scaleFactor} visibility={headerFieldsCount > 0 ? 'visible' : 'collapsed'}>
                {#each { length: 4 } as _, index (index)}
                    {@const field = structure?.headerFields?.[index]}
                    <label col={index} maxLines={2} paddingLeft={index !== 0 ? 12 * scaleFactor : 0} textAlignment={getFieldTextAlignment(field, 'right')} visibility={field ? 'visible' : 'collapsed'}>
                        <cspan
                            color={labelColor}
                            fontSize={10 * scaleFactor}
                            fontWeight="500"
                            lineHeight={FIELD_LINE_HEIGHT * scaleFactor}
                            text={getFieldLabel(field)}
                            visibility={field?.label ? 'visible' : 'hidden'} />
                        <span color={foregroundColor} fontSize={13 * scaleFactor} fontWeight="500" text={getFieldValue(field)} />
                    </label>
                {/each}
            </gridlayout>
        </gridlayout>

        <!-- Middle row: Primary fields + Transit icon (for boarding passes) -->
        <gridlayout colSpan={3} columns={withTransitionIcon ? '*,auto,*' : '*'} row={1} verticalAlignment="center">
            <!-- Departure -->
            <label col={0} horizontalAlignment="left" maxLines={2} verticalAlignment="center" visibility={withTransitionIcon ? 'visible' : 'collapsed'}>
                <cspan color={labelColor} fontSize={10 * scaleFactor} fontWeight="500" lineHeight={FIELD_LINE_HEIGHT * scaleFactor} text={getFieldLabel(primaryFields[0])} />
                <cspan color={foregroundColor} fontSize={28 * scaleFactor} fontWeight="bold" text={getFieldValue(primaryFields[0])} />
            </label>

            <!-- Transit Icon -->
            <label
                class="mdi"
                col={1}
                color={labelColor}
                fontSize={32 * scaleFactor}
                horizontalAlignment="center"
                marginLeft={16 * scaleFactor}
                marginRight={16 * scaleFactor}
                text={transitIcon}
                verticalAlignment="center"
                visibility={withTransitionIcon ? 'visible' : 'collapsed'} />

            <!-- Arrival -->
            <label col={2} horizontalAlignment="right" maxLines={2} textAlignment="right" verticalAlignment="center" visibility={withTransitionIcon ? 'visible' : 'collapsed'}>
                <cspan color={labelColor} fontSize={10 * scaleFactor} fontWeight="500" lineHeight={FIELD_LINE_HEIGHT * scaleFactor} text={getFieldLabel(primaryFields[1])} />
                <cspan color={foregroundColor} fontSize={28 * scaleFactor} fontWeight="bold" text={getFieldValue(primaryFields[1])} />
            </label>
            <!-- Standard layout for primary fields -->
            <gridlayout columns={Array.from('*'.repeat(primaryFieldsCount)).join(',')} visibility={!withTransitionIcon && primaryFieldsCount > 0 ? 'visible' : 'collapsed'}>
                {#each { length: 4 } as _, index (index)}
                    {@const field = primaryFields?.[index]}
                    <label col={index} marginTop={4 * scaleFactor} maxLines={2} padding={index !== 0 && index !== auxiliaryFieldsCount - 1 ? '0 10 0 10' : 0}>
                        <cspan
                            color={labelColor}
                            fontSize={10 * scaleFactor}
                            fontWeight="500"
                            lineHeight={FIELD_LINE_HEIGHT * scaleFactor}
                            text={getFieldLabel(field)}
                            visibility={field?.label ? 'visible' : 'hidden'} />
                        <cspan color={foregroundColor} fontSize={24 * scaleFactor} fontWeight="bold" text={getFieldValue(field)} />
                    </label>
                {/each}
            </gridlayout>
        </gridlayout>

        <!-- Bottom row: Secondary and auxiliary fields -->
        <!-- Left: Secondary fields -->
        <gridlayout colSpan={3} columns={Array.from('*'.repeat(secondaryFieldsCount)).join(',')} row={2} visibility={secondaryFieldsCount > 0 ? 'visible' : 'collapsed'}>
            {#each { length: 4 } as _, index (index)}
                {@const field = secondaryFields[index]}
                <label col={index} maxLines={2} padding={index !== 0 && index !== secondaryFieldsCount - 1 ? '0 10 0 10' : 0}>
                    <cspan color={labelColor} fontSize={9 * scaleFactor} lineHeight={FIELD_LINE_HEIGHT * scaleFactor} text={getFieldLabel(field)} />
                    <cspan color={foregroundColor} fontSize={11 * scaleFactor} fontWeight="600" text={getFieldValue(field)} />
                </label>
            {/each}
        </gridlayout>

        <!-- Left: Auxiliary fields -->
        <gridlayout colSpan={3} columns={Array.from('*'.repeat(auxiliaryFieldsCount)).join(',')} row={3} visibility={auxiliaryFieldsCount > 0 ? 'visible' : 'collapsed'}>
            {#each { length: 4 } as _, index (index)}
                {@const field = auxiliaryFields[index]}
                <label col={index} maxLines={2} padding={index !== 0 && index !== auxiliaryFieldsCount - 1 ? '0 10 0 10' : 0}>
                    <cspan color={labelColor} fontSize={9 * scaleFactor} lineHeight={FIELD_LINE_HEIGHT * scaleFactor} text={getFieldLabel(field)} />
                    <cspan color={foregroundColor} fontSize={11 * scaleFactor} fontWeight="600" text={getFieldValue(field)} />
                </label>
            {/each}
        </gridlayout>

        <!-- Right: Auxiliary fields -->
        <!-- {#if auxiliaryFieldsCount > 0}
                <stacklayout col={1} horizontalAlignment="right" orientation="horizontal">
                    {#each auxiliaryFields as field, idx}
                        <stacklayout marginLeft={idx > 0 ? 8 * scaleFactor : 0}>
                            <label color={labelColor} fontSize={9 * scaleFactor} text={getFieldLabel(field)} textAlignment="right" />
                            <label color={foregroundColor} fontSize={11 * scaleFactor} fontWeight="600" text={getFieldValue(field)} textAlignment="right" />
                        </stacklayout>
                    {/each}
                </stacklayout>
            {/if} -->
    </gridlayout>
</gridlayout>
