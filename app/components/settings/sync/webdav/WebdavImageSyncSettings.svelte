<script lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { Writable } from 'svelte/store';
    import ImageSyncSettings from '~/components/settings/sync/ImageSyncSettings.svelte';
    import WebdavSettingsView from '~/components/settings/sync/webdav/WebdavSettingsView.svelte';
    import { lc } from '~/helpers/locale';
    import { WebdavImageSyncServiceOptions } from '~/services/sync/webdav/WebdavImageSyncService';

    export let data: WebdavImageSyncServiceOptions = {} as any;

    let updateItem;
    let store: Writable<WebdavImageSyncServiceOptions>;

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

<ImageSyncSettings {data} serviceType="webdav_image" {topItems} {validateSave} bind:updateItem bind:store>
    <Template key="webdav" let:item>
        <WebdavSettingsView bind:this={webdavView} {store} />
    </Template>
</ImageSyncSettings>
