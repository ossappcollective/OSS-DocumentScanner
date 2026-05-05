<script lang="ts">
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { closeBottomSheet } from '@nativescript-community/ui-material-bottomsheet/svelte';
    import { HorizontalPosition, VerticalPosition } from '@nativescript-community/ui-popover';
    import { showPopover } from '@nativescript-community/ui-popover/svelte';
    import { View, verticalAlignmentProperty } from '@nativescript/core';
    import { showError } from '@shared/utils/showError';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { lc } from '~/helpers/locale';
    import { OCRLanguages, ocrService } from '~/services/ocr';
    import { colors, fonts } from '~/variables';
    import ListItemAutoSize from '@shared/components/ListItemAutoSize.svelte';
    import Chip from '../widgets/Chip.svelte';
    import { checkOrDownloadOCRLanguages, localizedLanguage } from '~/utils/ui';

    // technique for only specific properties to get updated on store change
    $: ({ colorOnPrimary, colorOnSurface, colorOutline, colorPrimary, colorSurfaceContainer } = $colors);

    let collectionView: NativeViewElementNode<CollectionView>;
    let downloaded = ocrService.downloadedLanguages;
    export let languages = ocrService.languagesArray;
    export let onlySettings = false;
    export let showDownloadButton = false;
    export let dataType: string = ocrService.dataType;
    async function addLanguages(event) {
        try {
            const SearchModal = (await import('~/components/ocr/SelectOCRLanguaguesModal.svelte')).default;
            // const result: any = await showModal({ page: Settings, fullscreen: true, props: { position } });
            const anchorView = event.object as View;
            const result: any = await showPopover({
                backgroundColor: colorSurfaceContainer,
                vertPos: VerticalPosition.ALIGN_BOTTOM,
                horizPos: HorizontalPosition.ALIGN_RIGHT,
                view: SearchModal,
                anchor: anchorView,
                props: {
                    selectedLanguages: languages
                }
            });
            if (result) {
                ocrService.addLanguages(result);
                languages.push(result);
                languages = languages;
            }
        } catch (error) {
            showError(error);
        }
    }

    async function removeLanguage(key: string) {
        try {
            ocrService.removeLanguages(key);
            languages.splice(languages.indexOf(key), 1);
            languages = languages;
        } catch (error) {
            showError(error);
        }
    }
    const qualities = ocrService.qualities;

    async function setDataType(value) {
        dataType = value;
        ocrService.dataType = dataType as any;
        downloaded = ocrService.downloadedLanguages;
        languages = languages;
    }

    function startOrCloseOCR() {
        closeBottomSheet(onlySettings ? { languages, dataType } : true);
    }

    async function downloadLanguages() {
        try {
            await checkOrDownloadOCRLanguages({
                dataType,
                languages,
                shouldConfirm: false
            });
            downloaded = ocrService.downloadedLanguages;
        } catch (error) {
            showError(error);
        }
    }
</script>

<gesturerootview id="ocrSettingsBottomSheet" rows="auto">
    <stacklayout padding={onlySettings ? 0 : 16}>
        {#if onlySettings}
            <ListItemAutoSize item={{ title: lc('quality'), titleProps: { verticalAlignment: 'top' } }}>
                <stacklayout marginTop={50} orientation="horizontal" verticalAlignment="bottom">
                    {#each qualities as quality (quality)}
                        <gridlayout
                            backgroundColor={dataType === quality ? colorPrimary : undefined}
                            borderColor={colorOutline}
                            borderRadius={8}
                            borderWidth={dataType === quality ? 0 : 1}
                            columns="auto,auto"
                            height={32}
                            margin={4}
                            paddingLeft={dataType === quality ? 8 : 12}
                            paddingRight={12}
                            rippleColor={colorPrimary}
                            on:tap={() => setDataType(quality)}>
                            <label
                                color={dataType === quality ? colorOnPrimary : colorOnSurface}
                                fontFamily={$fonts.mdi}
                                fontSize={16}
                                marginRight={4}
                                text="mdi-check"
                                verticalAlignment="middle"
                                visibility={dataType === quality ? 'visible' : 'collapse'} />
                            <label
                                col={1}
                                color={dataType === quality ? colorOnPrimary : colorOnSurface}
                                fontSize={14}
                                fontWeight={dataType === quality ? 'bold' : 'normal'}
                                text={lc(quality)}
                                verticalAlignment="middle" />
                        </gridlayout>
                    {/each}
                </stacklayout>
            </ListItemAutoSize>
        {:else}
            <label class="sectionBigHeader" marginBottom={onlySettings ? 0 : 16} text={lc('quality')} />
            <stacklayout orientation="horizontal">
                {#each qualities as quality (quality)}
                    <gridlayout
                        backgroundColor={dataType === quality ? colorPrimary : undefined}
                        borderColor={colorOutline}
                        borderRadius={8}
                        borderWidth={dataType === quality ? 0 : 1}
                        columns="auto,auto"
                        height={32}
                        margin={4}
                        paddingLeft={dataType === quality ? 8 : 12}
                        paddingRight={12}
                        rippleColor={colorPrimary}
                        on:tap={() => setDataType(quality)}>
                        <label
                            color={dataType === quality ? colorOnPrimary : colorOnSurface}
                            fontFamily={$fonts.mdi}
                            fontSize={16}
                            marginRight={4}
                            text="mdi-check"
                            verticalAlignment="middle"
                            visibility={dataType === quality ? 'visible' : 'collapse'} />
                        <label
                            col={1}
                            color={dataType === quality ? colorOnPrimary : colorOnSurface}
                            fontSize={14}
                            fontWeight={dataType === quality ? 'bold' : 'normal'}
                            text={lc(quality)}
                            verticalAlignment="middle" />
                    </gridlayout>
                {/each}
            </stacklayout>
        {/if}
        {#if onlySettings}
            <ListItemAutoSize item={{ title: lc('languages'), titleProps: { verticalAlignment: 'top' } }} paddingRight={0}>
                <mdbutton class="icon-btn" col={1} marginTop={6} text="mdi-plus" variant="text" verticalAlignment="top" on:tap={addLanguages} />
                <wraplayout marginTop={50} verticalAlignment="bottom">
                    {#each languages as language (language)}
                        <Chip rightIcon={downloaded.indexOf(language) !== -1 ? 'mdi-download' : null} text={localizedLanguage(language, OCRLanguages)} on:tap={() => removeLanguage(language)} />
                    {/each}
                </wraplayout>
            </ListItemAutoSize>
        {:else}
            <gridlayout columns="*,auto" marginBottom={16} marginTop={15} rows="auto">
                <label class="sectionBigHeader" text={lc('languages')} verticalAlignment="middle" />
                <mdbutton class="icon-btn" col={1} padding={0} text="mdi-plus" variant="text" on:tap={addLanguages} />
            </gridlayout>

            <wraplayout row={1}>
                {#each languages as language (language)}
                    <Chip rightIcon={downloaded.indexOf(language) !== -1 ? 'mdi-download' : null} text={localizedLanguage(language, OCRLanguages)} on:tap={() => removeLanguage(language)} />
                {/each}
            </wraplayout>
        {/if}
        <!-- <gridlayout , backgroundColor="" borderRadius="50%" columns="*,auto">
            <textfield hint={lc('languages')} placeholder={lc('languages')} returnKeyType="search" variant="none" verticalTextAlignment="center" />
        </gridlayout> -->
        {#if !onlySettings}
            <mdbutton horizontalAlignment="right" padding="10 16 10 16" row={1} text={lc('start')} on:tap={startOrCloseOCR} />
        {/if}

        {#if showDownloadButton}
            <mdbutton horizontalAlignment="right" padding="10 16 10 16" row={1} text={lc('download')} on:tap={downloadLanguages} />
        {/if}
    </stacklayout>
</gesturerootview>
