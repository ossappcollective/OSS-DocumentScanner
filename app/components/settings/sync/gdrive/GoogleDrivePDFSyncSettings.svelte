<script lang="ts">
    import { lc } from '@nativescript-community/l';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { Writable } from 'svelte/store';
    import OAuthSettingsView from '~/components/settings/sync/OAuthSettingsView.svelte';
    import PDFSyncSettings from '~/components/settings/sync/PDFSyncSettings.svelte';
    import { GOOGLE_DRIVE_PROVIDER, testGoogleDriveConnection } from '~/services/sync/gdrive/GoogleDrive';
    import { GoogleDrivePDFSyncServiceOptions } from '~/services/sync/gdrive/GoogleDrivePDFSyncService';

    export let data: GoogleDrivePDFSyncServiceOptions = {} as any;

    let updateItem;
    let store: Writable<GoogleDrivePDFSyncServiceOptions>;

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

<PDFSyncSettings {data} serviceType="gdrive_pdf" {topItems} {validateSave} bind:updateItem bind:store>
    <Template key="oauth" let:item>
        <OAuthSettingsView bind:this={oauthView} provider={GOOGLE_DRIVE_PROVIDER} {store} {testConnection} />
    </Template>
</PDFSyncSettings>
