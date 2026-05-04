<script lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { Writable } from 'svelte/store';
    import DataSyncSettings from '~/components/settings/sync/DataSyncSettings.svelte';
    import WebdavSettingsView from '~/components/settings/sync/webdav/WebdavSettingsView.svelte';
    import { lc } from '~/helpers/locale';
    import { WebdavDataSyncOptions } from '~/services/sync/webdav/WebdavDataSyncService';

    export let data: WebdavDataSyncOptions = {} as any;

    let updateItem;
    let store: Writable<WebdavDataSyncOptions>;

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

<DataSyncSettings {data} serviceType="webdav_data" {topItems} {validateSave} bind:updateItem bind:store>
    <Template key="webdav" let:item>
        <WebdavSettingsView bind:this={webdavView} {store} />
    </Template>
</DataSyncSettings>
