<script context="module" lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { TextFieldProperties } from '@nativescript-community/ui-material-textfield';
    import { ApplicationSettings, Color, ObservableArray, Page } from '@nativescript/core';
    import { closeModal } from '@shared/utils/svelte/ui';
    import { Writable, get, writable } from 'svelte/store';
    import CActionBar from '~/components/common/CActionBar.svelte';
    import OCRSettingsBottomSheet from '~/components/ocr/OCRSettingsBottomSheet.svelte';
    import PDFSyncSettingsView from '~/components/settings/sync/PDFSyncSettingsView.svelte';
    import SyncSettingsCollectionView from '~/components/settings/sync/SyncSettingsCollectionView.svelte';
    import { lc } from '~/helpers/locale';
    import { getPDFDefaultExportOptions } from '~/services/pdf/PDFCanvas';
    import { BasePDFSyncServiceOptions } from '~/services/sync/BasePDFSyncService';
    import { SERVICES_SYNC_COLOR, SYNC_TYPES } from '~/services/sync/types';
    import { FILENAME_DATE_FORMAT, FILENAME_USE_DOCUMENT_NAME, SETTINGS_FILE_NAME_FORMAT, SETTINGS_FILE_NAME_USE_DOCUMENT_NAME } from '~/utils/constants';
    import { checkOrDownloadOCRLanguages, getNameFormatHTMLArgs, openLink, requestNotificationPermission, showSnack } from '~/utils/ui';
</script>

<script lang="ts">
    // technique for only specific properties to get updated on store change
    let page: NativeViewElementNode<Page>;

    const pdfExportSettings = getPDFDefaultExportOptions();
    export let data: BasePDFSyncServiceOptions = {} as any;
    export let serviceType: SYNC_TYPES;

    export let store = writable<BasePDFSyncServiceOptions>(
        Object.assign(
            {
                exportOptions: pdfExportSettings,
                autoSync: false,
                enabled: true,
                OCREnabled: false,
                useFoldersStructure: false,
                OCRDataType: 'best',
                OCRLanguages: [],
                fileNameFormat: ApplicationSettings.getString(SETTINGS_FILE_NAME_FORMAT, FILENAME_DATE_FORMAT),
                useDocumentName: ApplicationSettings.getBoolean(SETTINGS_FILE_NAME_USE_DOCUMENT_NAME, FILENAME_USE_DOCUMENT_NAME),
                color: SERVICES_SYNC_COLOR[serviceType] as string | Color
            },
            data
        )
    );

    export let validateSave: () => Promise<boolean> = async () => true;
    export let onSaveError: Function = () =>
        showSnack({
            message: lc('need_fill_fields'),
            view: page.nativeView
        });
    // let folderPathName = data.folderPathName;
    const variant = 'outline';

    async function save() {
        const result = get(store);
        DEV_LOG && console.log('save', JSON.stringify(result));
        if (await validateSave()) {
            if (__ANDROID__) {
                await requestNotificationPermission();
            }
            if (result.OCREnabled && result.OCRLanguages.length) {
                await checkOrDownloadOCRLanguages({
                    dataType: result.OCRDataType,
                    languages: result.OCRLanguages,
                    shouldConfirm: false
                });
            }
            closeModal(result);
        } else {
            onSaveError();
        }
    }
    export let topItems: any[] | ((store: Writable<BasePDFSyncServiceOptions>) => any[]) = [];
    export let updateItem;

    const items = new ObservableArray([
        {
            type: 'color'
        },
        {
            type: 'switch',
            id: 'enabled',
            title: lc('enabled'),
            value: $store.enabled
        },
        {
            type: 'switch',
            id: 'autoSync',
            title: lc('auto_sync'),
            description: lc('local_auto_sync_desc'),
            value: $store.autoSync
        },
        ...(typeof topItems === 'function' ? topItems(store) : topItems),
        {
            type: 'switch',
            id: 'useDocumentName',
            title: lc('filename_use_document_name'),
            value: $store.useDocumentName
        },
        {
            id: 'setting',
            key: 'fileNameFormat',
            useHTML: true,
            title: lc('filename_date_format'),
            description: lc('filename_date_format_desc'),
            full_description: lc('filename_date_format_fulldesc', ...getNameFormatHTMLArgs()),
            onLinkTap: ({ link }) => {
                openLink(link);
            },
            valueType: 'string',
            textFieldProperties: {
                autocapitalizationType: 'none',
                autocorrect: false
            } as TextFieldProperties,
            rightValue: () => $store.fileNameFormat,
            type: 'prompt'
        },
        {
            type: 'switch',
            id: 'useFoldersStructure',
            title: lc('use_folder_structure'),
            description: lc('use_folder_structure_desc'),
            value: $store.useFoldersStructure
        },
        {
            type: 'sectionheader',
            title: lc('pdf_settings')
        },
        {
            type: 'pdfoptions'
        },
        {
            type: 'sectionheader',
            title: lc('ocr_settings')
        },
        {
            type: 'switch',
            id: 'OCREnabled',
            title: lc('ocr_enabled'),
            value: $store.OCREnabled
        },
        {
            type: 'ocroptions'
        }
    ]);
</script>

<page bind:this={page} actionBarHidden={true} {...$$restProps}>
    <gridlayout class="pageContent" rows="auto,*">
        <SyncSettingsCollectionView {items} row={1} bind:store bind:updateItem>
            <slot />
            <Template key="pdfoptions" let:item>
                <PDFSyncSettingsView {store} />
            </Template>
            <Template key="pdfoptions" let:item>
                <PDFSyncSettingsView {store} on:uppdate={() => updateItem({ type: 'pdfoptions' }, 'type')} />
            </Template>

            <Template key="ocroptions">
                <OCRSettingsBottomSheet onlySettings={true} bind:dataType={$store.OCRDataType} bind:languages={$store.OCRLanguages} />
            </Template>
        </SyncSettingsCollectionView>

        <CActionBar canGoBack modalWindow={true} title={lc('pdf_sync_settings')}>
            <mdbutton text={lc('save')} variant="text" verticalAlignment="middle" on:tap={save} />
            <!-- <mdbutton class="actionBarButton" text={lc('save')} variant="text" on:tap={save} /> -->
        </CActionBar>
    </gridlayout>
</page>
