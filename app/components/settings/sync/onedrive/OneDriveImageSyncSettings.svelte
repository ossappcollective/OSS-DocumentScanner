<script lang="ts">
    import { lc } from '@nativescript-community/l';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { Writable } from 'svelte/store';
    import ImageSyncSettings from '~/components/settings/sync/ImageSyncSettings.svelte';
    import OAuthSettingsView from '~/components/settings/sync/OAuthSettingsView.svelte';
    import { ONEDRIVE_PROVIDER, testOneDriveConnection } from '~/services/sync/onedrive/OneDrive';
    import { OneDriveImageSyncServiceOptions } from '~/services/sync/onedrive/OneDriveImageSyncService';

    export let data: OneDriveImageSyncServiceOptions = {} as any;

    let updateItem;
    let store: Writable<OneDriveImageSyncServiceOptions>;

    const topItems = [
        {
            type: 'sectionHeader',
            title: lc('authentication')
        },
        {
            type: 'oauth'
        }
    ];
    let oauthView: OAuthSettingsView;

    async function validateSave() {
        return oauthView?.validateSave();
    }
    async function testConnection(tokens) {
        return testOneDriveConnection({ tokens } as any);
    }
</script>

<ImageSyncSettings {data} serviceType="onedrive_image" {topItems} {validateSave} bind:updateItem bind:store>
    <Template key="oauth" let:item>
        <OAuthSettingsView bind:this={oauthView} provider={ONEDRIVE_PROVIDER} {store} {testConnection} />
    </Template>
</ImageSyncSettings>
