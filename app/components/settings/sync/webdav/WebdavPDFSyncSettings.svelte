<script lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { Writable } from 'svelte/store';
    import PDFSyncSettings from '~/components/settings/sync/PDFSyncSettings.svelte';
    import WebdavSettingsView from '~/components/settings/sync/webdav/WebdavSettingsView.svelte';
    import { lc } from '~/helpers/locale';
    import { WebdavPDFSyncServiceOptions } from '~/services/sync/webdav/WebdavPDFSyncService';

    export let data: WebdavPDFSyncServiceOptions = {} as any;

    let updateItem;
    let store: Writable<WebdavPDFSyncServiceOptions>;

    const topItems = [
        {
            type: 'sectionHeader',
            title: lc('webdav_config')
        },
        {
            type: 'webdav'
        }
    ];
    let webdavView: WebdavSettingsView;

    async function validateSave() {
        return webdavView?.validateSave();
    }
</script>

<PDFSyncSettings {data} serviceType="webdav_pdf" {topItems} {validateSave} bind:updateItem bind:store>
    <Template key="webdav" let:item>
        <WebdavSettingsView bind:this={webdavView} {store} />
    </Template>
</PDFSyncSettings>
