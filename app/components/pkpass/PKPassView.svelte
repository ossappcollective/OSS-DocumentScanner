<script lang="ts">
    import { Color, GridLayout, path } from '@nativescript/core';
    import { PKBarcodeFormat, PKPass, PKPassBarcode, PKPassField, PKPassStructure, PKPassStyle, PKPassTransitType, PKPassType } from '~/models/PKPass';
    import { colors, fontScale, windowInset } from '~/variables';
    import { qrcodeService } from '~/services/qrcode';
    import { getBarcodeSVG, getFieldTextAlignment, getTransitIcon } from '~/utils/pkpass';
    import { lang, lc } from '~/helpers/locale';
    import { isEInk } from '@shared/helpers/theme';
    import { showError } from '@shared/utils/showError';
    import { OCRDocument } from '~/models/OCRDocument';
    import { conditionalEvent, createEventDispatcher } from '@shared/utils/svelte/ui';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { openLink } from '~/utils/ui';

    const FIELD_LINE_HEIGHT = 16;

    export let pkpass: PKPass;
    export let document: OCRDocument;
    export let includeBackFields = true;
    export let forImageRendering = false;
    export let barcodeSvg: string = null;
    let mainGrid: NativeViewElementNode<GridLayout>;

    let { colorOnBackground, colorOnSurface, colorSurface, colorSurfaceContainerHigh } = $colors;
    $: ({ colorOnBackground, colorOnSurface, colorSurface, colorSurfaceContainerHigh } = $colors);

    const passData = pkpass.passData;
    const passStyle = pkpass.getPassStyle();
    const passType = pkpass.passType;
    const structure = pkpass.getPassStructure();
    const primaryBarcode = pkpass.getPrimaryBarcode();
    const transitType = structure?.transitType;
    const transitIcon = getTransitIcon(transitType);
    const headerFieldsCount = structure?.headerFields?.length ?? 0;
    const secondaryFieldsCount = structure?.secondaryFields?.length ?? 0;
    const primaryFieldsCount = structure?.primaryFields?.length ?? 0;
    const auxiliaryFieldsCount = structure?.auxiliaryFields?.length ?? 0;

    $: backgroundColor = passData.backgroundColor ?? colorSurface;
    $: foregroundColor = passData.foregroundColor ?? (new Color(backgroundColor).getBrightness() < 145 ? 'white' : 'black');
    $: labelColor = passData.labelColor ?? (new Color(backgroundColor).getBrightness() < 145 ? 'white' : 'black');
    // Apple PKPass image specifications - prefer @2x for quality
    const logoImage = pkpass?.images?.logo2x || pkpass?.images?.logo; // Max 160x50 points
    const iconImage = pkpass?.images?.icon2x || pkpass?.images?.icon; // 29x29 points
    const stripImage = pkpass?.images?.strip2x || pkpass?.images?.strip; // Variable by device/type
    const backgroundImage = pkpass?.images?.background2x || pkpass?.images?.background; // 180x220 points
    const thumbnailImage = pkpass?.images?.thumbnail2x || pkpass?.images?.thumbnail; // 90x90 points
    const footerImage = pkpass?.images?.footer2x || pkpass?.images?.footer; // 286x15 points
    const orgName = passData?.organizationName || getLocalizedText(passData?.logoText) || '';

    let waitCounter = 0;
    $: {
        if (primaryBarcode && foregroundColor) {
            updateBarcode();
        }
    }

    function onLoadingStep() {
        waitCounter -= 1;
        if (waitCounter === 0) {
            mainGrid.nativeView.notify({ eventName: 'layoutDone' });
        }
    }

    async function updateBarcode() {
        if (!barcodeSvg) {
            barcodeSvg = await getBarcodeSVG({ barcode: primaryBarcode, foregroundColor });
        }
    }

    function renderFieldValue(field: PKPassField): string {
        const value = pkpass.formatFieldValue(field, lang);
        return value;
    }

    function renderFieldLabel(field: PKPassField): string {
        if (!field) return null;
        return pkpass.getLocalizedFieldLabel(field, lang).toUpperCase() + '\n';
    }

    function getLocalizedText(text: string): string {
        return text ? pkpass.getLocalizedValue(text, lang) : text;
    }
    async function onQRCodeTap() {
        try {
            await qrcodeService.showQRCode([{ pkpass } as any], document, 0);
        } catch (error) {
            showError(error);
        }
    }
    function onLayoutChanged() {
        if (waitCounter <= 0) {
            mainGrid.nativeView.notify({ eventName: 'layoutDone' });
        }
    }
    function onLinkTap({ link }) {
        openLink(link);
    }
</script>

<gridlayout
    bind:this={mainGrid}
    {backgroundColor}
    borderRadius={8}
    elevation={forImageRendering || isEInk ? 0 : 2}
    margin={forImageRendering ? 0 : 16}
    marginBottom={forImageRendering ? 0 : $windowInset.bottom + 16}
    rows={`auto,auto,${forImageRendering ? 'auto' : '*'},auto`}
    {...$$restProps}
    use:conditionalEvent={{ condition: forImageRendering, event: 'layoutChanged', callback: onLayoutChanged }}>
    {#if backgroundImage && passStyle === PKPassStyle.EventTicket}
        <image height="100%" opacity="0.3" rowSpan={4} src={backgroundImage} stretch="aspectFill" />
    {/if}
    {#if stripImage && passType === PKPassType.ESpass}
        <image noRatioEnforce={true} opacity={0.3} rowSpan={4} src={stripImage} stretch="aspectFill" width="100%" />
    {/if}
    <stacklayout padding="16" row={0}>
        <gridlayout columns="auto,*,auto,auto">
            <!-- Icon on the left (29x29 points per Apple spec) -->
            <!-- {#if iconImage}
                <image col={0} height={29} marginRight={6} src={iconImage} stretch="aspectFit" verticalAlignment="center" width={29} />
            {/if} -->

            <!-- Logo (max 160x50 points per Apple spec) -->
            {#if logoImage}
                <image col={0} height={50} src={logoImage} stretch="aspectFit" verticalAlignment="top" />
            {:else if !iconImage}
                <!-- Just name if no icon or logo -->
                <label col={0} color={foregroundColor} fontSize={14 * $fontScale} fontWeight="bold" maxLines={1} selectable={true} text={orgName} verticalAlignment="center" />
            {:else}
                <!-- Name next to icon -->
                <image col={0} height={50} src={iconImage} stretch="aspectFit" verticalAlignment="top" />
                <label
                    col={1}
                    color={foregroundColor}
                    fontSize={16 * $fontScale}
                    fontWeight="bold"
                    horizontalAlignment="left"
                    maxLines={1}
                    paddingLeft={10 * $fontScale}
                    selectable={true}
                    text={orgName}
                    verticalAlignment="center"
                    width={120} />
            {/if}

            <!-- Header fields -->
            {#if headerFieldsCount > 0}
                <gridlayout col={2} columns={Array.from('*'.repeat(headerFieldsCount)).join(',')} marginBottom={16}>
                    {#each structure.headerFields as field, index}
                        <label col={index} paddingLeft={index !== 0 ? 10 : 0} selectable={true} textAlignment={getFieldTextAlignment(field, 'right')}>
                            <cspan
                                color={labelColor}
                                fontSize={13 * $fontScale}
                                fontWeight="500"
                                lineHeight={FIELD_LINE_HEIGHT}
                                text={renderFieldLabel(field)}
                                verticalTextAlignment="top"
                                visibility={field.label ? 'visible' : 'hidden'} />
                            <cspan color={foregroundColor} fontSize={17 * $fontScale} fontWeight="500" text={renderFieldValue(field)} />
                        </label>
                    {/each}
                </gridlayout>
            {/if}
        </gridlayout>

        <label
            color={labelColor}
            fontSize={14 * $fontScale}
            marginTop={4}
            selectable={true}
            text={passData.organizationName}
            textAlignment="center"
            visibility={passData.logoText !== passData.organizationName ? 'visible' : 'collapsed'} />
    </stacklayout>

    <!-- Main content -->
    <scrollview row={2}>
        <stacklayout padding={16}>
            <!-- Primary fields -->
            {#if primaryFieldsCount > 0}
                <!-- Strip image behind primary fields (variable dimensions per Apple spec) -->

                {#if passStyle === PKPassStyle.BoardingPass && transitIcon && primaryFieldsCount === 2}
                    <!-- Boarding pass with transit icon between two primary fields -->
                    <gridlayout columns="*,auto,*" marginBottom={16}>
                        <!-- Left primary field (departure) -->
                        <label col={0} selectable={true} textAlignment="left">
                            {#if structure.primaryFields[0].label}
                                <cspan color={labelColor} fontSize={12 * $fontScale} fontWeight="500" lineHeight={FIELD_LINE_HEIGHT} text={renderFieldLabel(structure.primaryFields[0])} />
                            {/if}
                            <cspan color={foregroundColor} fontSize={32 * $fontScale} fontWeight="bold" text={renderFieldValue(structure.primaryFields[0])} />
                        </label>

                        <!-- Transit icon in center -->
                        <label
                            class="mdi"
                            col={1}
                            color={labelColor}
                            fontSize={50 * $fontScale}
                            marginLeft={16}
                            marginRight={16}
                            text={transitIcon}
                            textAlignment="center"
                            verticalAlignment="center" />

                        <!-- Right primary field (arrival) -->
                        <label col={2} selectable={true} textAlignment="right">
                            {#if structure.primaryFields[1].label}
                                <cspan color={labelColor} fontSize={12 * $fontScale} fontWeight="500" lineHeight={FIELD_LINE_HEIGHT} text={renderFieldLabel(structure.primaryFields[1])} />
                            {/if}
                            <cspan color={foregroundColor} fontSize={32 * $fontScale} fontWeight="bold" text={renderFieldValue(structure.primaryFields[1])} />
                        </label>
                    </gridlayout>
                {:else}
                    <!-- Default primary fields layout (no transit icon or different field count) -->
                    <gridlayout columns={Array.from('*'.repeat(primaryFieldsCount)).join(',')} marginBottom={16}>
                        {#if stripImage && passType === PKPassType.PKPass}
                            <image colSpan={primaryFieldsCount} noRatioEnforce={true} opacity={0.5} src={stripImage} stretch="aspectFill" width="100%" />
                        {/if}
                        {#each structure.primaryFields as field, index}
                            <label col={index} marginBottom="16" selectable={true} textAlignment={getFieldTextAlignment(field)}>
                                {#if field.label}
                                    <cspan color={labelColor} fontSize={12 * $fontScale} fontWeight="500" lineHeight={FIELD_LINE_HEIGHT} text={renderFieldLabel(field)} />
                                {/if}
                                <cspan color={foregroundColor} fontSize={32 * $fontScale} fontWeight="bold" text={renderFieldValue(field)} />
                            </label>
                        {/each}
                    </gridlayout>
                {/if}
            {/if}

            <!-- Secondary fields -->
            {#if secondaryFieldsCount > 0}
                <gridlayout columns={Array.from('*'.repeat(secondaryFieldsCount)).join(',')} marginTop={16}>
                    {#each structure.secondaryFields as field, index}
                        <label col={index} padding={index !== 0 && index !== auxiliaryFieldsCount - 1 ? '0 10 0 10' : 0} selectable={true} textAlignment={getFieldTextAlignment(field)}>
                            {#if field.label}
                                <cspan color={labelColor} fontSize={11 * $fontScale} fontWeight="500" lineHeight={FIELD_LINE_HEIGHT} text={renderFieldLabel(field)} />
                            {/if}
                            <cspan color={foregroundColor} fontSize={18 * $fontScale} text={renderFieldValue(field)} />
                        </label>
                    {/each}
                </gridlayout>
            {/if}

            <!-- Auxiliary fields -->
            {#if auxiliaryFieldsCount > 0}
                <gridlayout columns={Array.from('*'.repeat(auxiliaryFieldsCount)).join(',')} marginTop={16}>
                    {#each structure.auxiliaryFields as field, index}
                        <label col={index} padding={index !== 0 && index !== auxiliaryFieldsCount - 1 ? '0 10 0 10' : 0} selectable={true} textAlignment={getFieldTextAlignment(field)}>
                            {#if field.label}
                                <cspan color={labelColor} fontSize={10 * $fontScale} fontWeight="500" lineHeight={FIELD_LINE_HEIGHT} text={renderFieldLabel(field)} />
                            {/if}
                            <cspan color={foregroundColor} fontSize={14 * $fontScale} text={renderFieldValue(field)} />
                        </label>
                    {/each}
                </gridlayout>
            {/if}

            <!-- Footer image if available (286x15 points per Apple spec) -->
            {#if footerImage && passType === PKPassType.PKPass}
                <image height={15} margin={8} row={3} src={footerImage} stretch="aspectFill" width="100%" />
            {/if}

            <!-- Barcode -->
            {#if barcodeSvg}
                <stacklayout
                    backgroundColor={foregroundColor && new Color(foregroundColor).getBrightness() < 145 ? '#ffffff' : 'transparent'}
                    borderRadius={4}
                    horizontalAlignment="center"
                    marginTop={24}
                    padding={8}
                    on:tap={onQRCodeTap}
                    use:conditionalEvent={{ condition: forImageRendering, event: 'layoutChanged', callback: onLoadingStep }}>
                    <svgview src={barcodeSvg} stretch="aspectFit" width="50%" />
                    {#if primaryBarcode?.altText}
                        <label color={foregroundColor} fontSize="12" marginTop="8" selectable={true} text={primaryBarcode.altText} textAlignment="center" />
                    {/if}
                </stacklayout>
            {/if}

            <!-- Footer image if available (286x15 points per Apple spec) -->
            {#if footerImage && passType === PKPassType.ESpass}
                <image margin={8} row={3} src={footerImage} stretch="aspectFill" width="100%" />
            {/if}

            <!-- Back fields (additional info) -->
            {#if includeBackFields && structure?.backFields && structure.backFields.length > 0}
                <stacklayout marginTop={24}>
                    <label color={labelColor} fontSize={18 * $fontScale} fontWeight="bold" marginBottom="12" text={lc('additional_information')} />
                    {#each structure.backFields as field}
                        <label color={foregroundColor} marginBottom={12} selectable={true} on:linkTap={onLinkTap}>
                            {#if field.label}
                                <cspan color={labelColor} fontSize={12 * $fontScale} fontWeight="500" lineHeight={FIELD_LINE_HEIGHT} text={renderFieldLabel(field)} />
                            {/if}
                            <cspan color={foregroundColor} fontSize={14 * $fontScale} html={renderFieldValue(field)} linkColor={labelColor} tappable={true} />
                        </label>
                    {/each}
                </stacklayout>
            {/if}

            <!-- Expiration/Relevant date info -->
            {#if passData.expirationDate}
                <label
                    backgroundColor={colorSurfaceContainerHigh}
                    borderRadius={8}
                    color={pkpass.isExpired() ? '#ff5252' : colorOnSurface}
                    fontSize={15 * $fontScale}
                    fontWeight="bold"
                    marginTop={16}
                    padding={12}
                    text={pkpass.isExpired() ? lc('pkpass_expired') : lc('pkpass_expires_at', new Date(passData.expirationDate).toLocaleDateString())}
                    textAlignment="center" />
            {/if}

            {#if pkpass.isVoided()}
                <label
                    backgroundColor="#ff5252"
                    borderRadius={8}
                    color="#ffffff"
                    fontSize={15 * $fontScale}
                    fontWeight="bold"
                    marginTop={8}
                    padding={12}
                    text={lc('pkpass_voided')}
                    textAlignment="center" />
            {/if}
        </stacklayout>
    </scrollview>
</gridlayout>
