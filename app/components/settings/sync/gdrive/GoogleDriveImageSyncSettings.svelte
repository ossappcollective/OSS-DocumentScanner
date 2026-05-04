<script lang="ts">
    import { lc } from '@nativescript-community/l';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { Writable } from 'svelte/store';
    import ImageSyncSettings from '~/components/settings/sync/ImageSyncSettings.svelte';
    import OAuthSettingsView from '~/components/settings/sync/OAuthSettingsView.svelte';
    import { GOOGLE_DRIVE_PROVIDER, testGoogleDriveConnection } from '~/services/sync/gdrive/GoogleDrive';
    import { GoogleDriveImageSyncServiceOptions } from '~/services/sync/gdrive/GoogleDriveImageSyncService';

    export let data: GoogleDriveImageSyncServiceOptions = {} as any;

    let updateItem;
    let store: Writable<GoogleDriveImageSyncServiceOptions>;

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
        return testGoogleDriveConnection({ tokens } as any);
    }
</script>

<ImageSyncSettings {data} serviceType="gdrive_image" {topItems} {validateSave} bind:updateItem bind:store>
    <Template key="oauth" let:item>
        <OAuthSettingsView bind:this={oauthView} provider={GOOGLE_DRIVE_PROVIDER} {store} {testConnection} />
    </Template>
</ImageSyncSettings>
