<script lang="ts">
    import { lc } from '@nativescript-community/l';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { Writable } from 'svelte/store';
    import OAuthSettingsView from '~/components/settings/sync/OAuthSettingsView.svelte';
    import PDFSyncSettings from '~/components/settings/sync/PDFSyncSettings.svelte';
    import { ONEDRIVE_PROVIDER, testOneDriveConnection } from '~/services/sync/onedrive/OneDrive';
    import { OneDrivePDFSyncServiceOptions } from '~/services/sync/onedrive/OneDrivePDFSyncService';

    export let data: OneDrivePDFSyncServiceOptions = {} as any;

    let updateItem;
    let store: Writable<OneDrivePDFSyncServiceOptions>;

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

<PDFSyncSettings {data} serviceType="onedrive_pdf" {topItems} {validateSave} bind:updateItem bind:store>
    <Template key="oauth" let:item>
        <OAuthSettingsView bind:this={oauthView} provider={ONEDRIVE_PROVIDER} {store} {testConnection} />
    </Template>
</PDFSyncSettings>
