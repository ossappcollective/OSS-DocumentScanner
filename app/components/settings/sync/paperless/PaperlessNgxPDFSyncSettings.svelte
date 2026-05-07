<script lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { Writable } from 'svelte/store';
    import PDFSyncSettings from '~/components/settings/sync/PDFSyncSettings.svelte';
    import PaperlessNgxSettingsView from '~/components/settings/sync/paperless/PaperlessNgxSettingsView.svelte';
    import { lc } from '~/helpers/locale';
    import { PaperlessNgxPDFSyncServiceOptions } from '~/services/sync/paperless/PaperlessNgxPDFSyncService';

    export let data: PaperlessNgxPDFSyncServiceOptions = {} as any;

    let updateItem;
    let store: Writable<PaperlessNgxPDFSyncServiceOptions>;

    const topItems = [
        {
            type: 'header',
            title: lc('paperless_config')
        },
        {
            type: 'paperless'
        }
    ];
    let paperlessView: PaperlessNgxSettingsView;

    async function validateSave() {
        return paperlessView?.validateSave();
    }
</script>

<PDFSyncSettings {data} serviceType="paperless_pdf" {topItems} {validateSave} bind:updateItem bind:store>
    <Template key="paperless" let:item>
        <PaperlessNgxSettingsView bind:this={paperlessView} {store} />
    </Template>
</PDFSyncSettings>
