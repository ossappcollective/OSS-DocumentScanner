<script lang="ts">
    import { SilentError } from '@akylas/nativescript-app-utils/error';
    import { showError } from '@shared/utils/showError';
    import { Writable, get } from 'svelte/store';
    import { lc } from '~/helpers/locale';
    import { acquireToken, testPaperlessConnection } from '~/services/sync/paperless/PaperlessNgx';
    import type { PaperlessNgxSyncOptions } from '~/services/sync/paperless/PaperlessNgx';
    import { colors } from '~/variables';

    $: ({ colorError, colorOnError, colorSecondary } = $colors);

    const variant = 'outline';
    export let store: Writable<PaperlessNgxSyncOptions & { token?: string }>;
    let testing = false;
    let testConnectionSuccess = 0;

    async function testConnection() {
        try {
            const data = get(store);
            if (!data.serverUrl?.length) {
                throw new SilentError(lc('missing_paperless_server_url'));
            }
            if (!data.token?.length && !(data.username?.length && data.password?.length)) {
                throw new SilentError(lc('missing_paperless_credentials'));
            }
            testing = true;
            testConnectionSuccess = 0;

            // If only username/password provided, acquire a token first
            if (!data.token && data.username && data.password) {
                const acquiredToken = await acquireToken({ serverUrl: data.serverUrl }, data.username, data.password);
                $store.token = acquiredToken;
                // Clear password from store so it is not persisted; the token is used going forward
                $store.password = '';
            }

            const result = await testPaperlessConnection(get(store));
            testConnectionSuccess = result ? 1 : -1;
        } catch (error) {
            showError(error);
            testConnectionSuccess = -1;
        } finally {
            testing = false;
        }
    }

    export async function validateSave() {
        const data = get(store);
        if (!data.serverUrl?.length) {
            return false;
        }
        // Must have a token OR credentials (we acquired a token already during test)
        if (!data.token?.length && !(data.username?.length && data.password?.length)) {
            return false;
        }
        if (testConnectionSuccess === 0) {
            await testConnection();
        }
        return testConnectionSuccess > 0;
    }
</script>

<stacklayout padding="4 10 4 10">
    <textfield
        autocapitalizationType="none"
        hint={lc('server_address')}
        keyboardType="url"
        margin="5 0 5 0"
        placeholder={lc('server_address') + ' https://...'}
        returnKeyType="next"
        text={$store.serverUrl}
        {variant}
        on:textChange={(e) => {
            $store.serverUrl = e['value'];
            testConnectionSuccess = 0;
        }} />

    <label fontSize={12} margin="2 0 6 2" opacity={0.7} text={lc('paperless_token_hint')} textWrap={true} />

    <textfield
        autocapitalizationType="none"
        autocorrect={false}
        hint={lc('api_token')}
        margin="5 0 5 0"
        placeholder={lc('api_token')}
        returnKeyType="next"
        text={$store.token}
        {variant}
        on:textChange={(e) => {
            $store.token = e['value'];
            testConnectionSuccess = 0;
        }} />

    <label fontSize={12} margin="6 0 2 2" opacity={0.7} text={lc('or')} />

    <textfield
        autocapitalizationType="none"
        autocorrect={false}
        hint={lc('username')}
        margin="5 0 5 0"
        placeholder={lc('username')}
        returnKeyType="next"
        text={$store.username}
        {variant}
        on:textChange={(e) => {
            $store.username = e['value'];
            testConnectionSuccess = 0;
        }} />
    <textfield
        autocapitalizationType="none"
        autocorrect={false}
        hint={lc('password_not_saved')}
        margin="5 0 5 0"
        placeholder={lc('password')}
        placeholderColor="gray"
        returnKeyType="done"
        secure={true}
        text={$store.password}
        {variant}
        on:textChange={(e) => {
            $store.password = e['value'];
            testConnectionSuccess = 0;
        }} />

    <gridlayout columns="*,*" margin="5 0 0 0" rows="auto">
        <gridlayout col={1} columns="auto" horizontalAlignment="right" rows="auto" verticalAlignment="middle">
            <mdbutton
                backgroundColor={testConnectionSuccess < 0 ? colorError : testConnectionSuccess > 0 ? 'lightgreen' : colorSecondary}
                color={colorOnError}
                text={testConnectionSuccess < 0 ? lc('failed') : testConnectionSuccess > 0 ? lc('successful') : lc('test')}
                verticalAlignment="middle"
                visibility={testing ? 'hidden' : 'visible'}
                on:tap={testConnection} />
            <activityindicator busy={testing} height={20} horizontalAlignment="center" verticalAlignment="middle" visibility={testing ? 'visible' : 'hidden'} />
        </gridlayout>
    </gridlayout>
</stacklayout>
