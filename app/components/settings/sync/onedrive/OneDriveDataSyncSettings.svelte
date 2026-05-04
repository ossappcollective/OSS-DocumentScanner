<script lang="ts">
    import { lc } from '@nativescript-community/l';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { Writable } from 'svelte/store';
    import DataSyncSettings from '~/components/settings/sync/DataSyncSettings.svelte';
    import OAuthSettingsView from '~/components/settings/sync/OAuthSettingsView.svelte';
    import { ONEDRIVE_PROVIDER, testOneDriveConnection } from '~/services/sync/onedrive/OneDrive';
    import { OneDriveDataSyncOptions } from '~/services/sync/onedrive/OneDriveDataSyncService';

    export let data: OneDriveDataSyncOptions = {} as any;

    let updateItem;
    let store: Writable<OneDriveDataSyncOptions>;

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

<DataSyncSettings {data} serviceType="onedrive_data" {topItems} {validateSave} bind:updateItem bind:store>
    <Template key="oauth" let:item>
        <OAuthSettingsView bind:this={oauthView} provider={ONEDRIVE_PROVIDER} {store} {testConnection} />
    </Template>
</DataSyncSettings>
