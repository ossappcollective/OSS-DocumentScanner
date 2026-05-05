<script context="module" lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { ObservableArray } from '@nativescript/core';
    import ListItemAutoSize from '@shared/components/ListItemAutoSize.svelte';
    import { showError } from '@shared/utils/showError';
    import { showModal } from '@shared/utils/svelte/ui';
    import CActionBar from '~/components/common/CActionBar.svelte';
    import { lc, onLanguageChanged } from '~/helpers/locale';
    import { onThemeChanged } from '~/helpers/theme';
    import { syncService } from '~/services/sync';
    import { LocalFolderImageSyncServiceOptions } from '~/services/sync/local/LocalFolderImageSyncService';
    import { LocalFolderPDFSyncServiceOptions } from '~/services/sync/local/LocalFolderPDFSyncService';
    import { SERVICES_SYNC_COLOR, SERVICES_SYNC_ICONS_COLORS, SyncTypes } from '~/services/sync/types';
    import { WebdavSyncOptions, createWebdavConfig } from '~/services/sync/webdav/Webdav';
    import { WebdavDataSyncOptions } from '~/services/sync/webdav/WebdavDataSyncService';
    import { ALERT_OPTION_MAX_HEIGHT } from '~/utils/constants';
    import { getDirectoryName, hideLoading, requestNotificationPermission, showAlertOptionSelect } from '~/utils/ui';
    import { colors, fontScale, fonts, windowInset } from '~/variables';
    type Item = BaseSyncServiceOptions & { title?: string; description?: string; syncColor?: string };

    import type GoogleDriveDataSyncSettings__SvelteComponent_ from '~/components/settings/sync/gdrive/GoogleDriveDataSyncSettings.svelte';
    import type GoogleDriveImageSyncSettings__SvelteComponent_ from '~/components/settings/sync/gdrive/GoogleDriveImageSyncSettings.svelte';
    import type GoogleDrivePDFSyncSettings__SvelteComponent_ from '~/components/settings/sync/gdrive/GoogleDrivePDFSyncSettings.svelte';
    import type FolderImageSyncSettings__SvelteComponent_ from '~/components/settings/sync/local/FolderImageSyncSettings.svelte';
    import type FolderPDFSyncSettings__SvelteComponent_ from '~/components/settings/sync/local/FolderPDFSyncSettings.svelte';
    import type OneDriveDataSyncSettings__SvelteComponent_ from '~/components/settings/sync/onedrive/OneDriveDataSyncSettings.svelte';
    import type OneDriveImageSyncSettings__SvelteComponent_ from '~/components/settings/sync/onedrive/OneDriveImageSyncSettings.svelte';
    import type OneDrivePDFSyncSettings__SvelteComponent_ from '~/components/settings/sync/onedrive/OneDrivePDFSyncSettings.svelte';
    import type PaperlessNgxPDFSyncSettings__SvelteComponent_ from '~/components/settings/sync/paperless/PaperlessNgxPDFSyncSettings.svelte';
    import type WebdavDataSyncSettings__SvelteComponent_ from '~/components/settings/sync/webdav/WebdavDataSyncSettings.svelte';
    import type WebdavImageSyncSettings__SvelteComponent_ from '~/components/settings/sync/webdav/WebdavImageSyncSettings.svelte';
    import type WebdavPDFSyncSettings__SvelteComponent_ from '~/components/settings/sync/webdav/WebdavPDFSyncSettings.svelte';
    import { BaseDataSyncServiceOptions } from '~/services/sync/BaseDataSyncService';
    import { BaseSyncServiceOptions } from '~/services/sync/BaseSyncService';
    import { GoogleDriveSyncOptions } from '~/services/sync/gdrive/GoogleDrive';
    import { OneDriveSyncOptions } from '~/services/sync/onedrive/OneDrive';
    import type { PaperlessNgxSyncOptions } from '~/services/sync/paperless/PaperlessNgx';
    import { SERVICES_SYNC_TITLES } from '~/services/sync/ui';

    interface SettingsComponentReturnType {
        [SyncTypes.folder_image]: typeof FolderImageSyncSettings__SvelteComponent_;
        [SyncTypes.folder_pdf]: typeof FolderPDFSyncSettings__SvelteComponent_;
        [SyncTypes.webdav_data]: typeof WebdavDataSyncSettings__SvelteComponent_;
        [SyncTypes.webdav_image]: typeof WebdavImageSyncSettings__SvelteComponent_;
        [SyncTypes.webdav_pdf]: typeof WebdavPDFSyncSettings__SvelteComponent_;
        [SyncTypes.onedrive_data]: typeof OneDriveDataSyncSettings__SvelteComponent_;
        [SyncTypes.onedrive_image]: typeof OneDriveImageSyncSettings__SvelteComponent_;
        [SyncTypes.onedrive_pdf]: typeof OneDrivePDFSyncSettings__SvelteComponent_;
        [SyncTypes.gdrive_data]: typeof GoogleDriveDataSyncSettings__SvelteComponent_;
        [SyncTypes.gdrive_image]: typeof GoogleDriveImageSyncSettings__SvelteComponent_;
        [SyncTypes.gdrive_pdf]: typeof GoogleDrivePDFSyncSettings__SvelteComponent_;
        [SyncTypes.paperless_pdf]: typeof PaperlessNgxPDFSyncSettings__SvelteComponent_;
    }
    async function getSettingsComponent<T extends SyncTypes>(syncType: T): Promise<SettingsComponentReturnType[T]> {
        switch (syncType) {
            case SyncTypes.folder_image:
                return (await import('~/components/settings/sync/local/FolderImageSyncSettings.svelte')).default as any;
            case 'folder_pdf':
                return (await import('~/components/settings/sync/local/FolderPDFSyncSettings.svelte')).default as any;
            case 'webdav_data':
                return (await import('~/components/settings/sync/webdav/WebdavDataSyncSettings.svelte')).default as any;
            case 'webdav_image':
                return (await import('~/components/settings/sync/webdav/WebdavImageSyncSettings.svelte')).default as any;
            case 'webdav_pdf':
                return (await import('~/components/settings/sync/webdav/WebdavPDFSyncSettings.svelte')).default as any;
            case 'onedrive_data':
                return (await import('~/components/settings/sync/onedrive/OneDriveDataSyncSettings.svelte')).default as any;
            case 'onedrive_image':
                return (await import('~/components/settings/sync/onedrive/OneDriveImageSyncSettings.svelte')).default as any;
            case 'onedrive_pdf':
                return (await import('~/components/settings/sync/onedrive/OneDrivePDFSyncSettings.svelte')).default as any;
            case 'gdrive_data':
                return (await import('~/components/settings/sync/gdrive/GoogleDriveDataSyncSettings.svelte')).default as any;
            case 'gdrive_image':
                return (await import('~/components/settings/sync/gdrive/GoogleDriveImageSyncSettings.svelte')).default as any;
            case 'gdrive_pdf':
                return (await import('~/components/settings/sync/gdrive/GoogleDrivePDFSyncSettings.svelte')).default as any;
            case 'paperless_pdf':
                return (await import('~/components/settings/sync/paperless/PaperlessNgxPDFSyncSettings.svelte')).default as any;
        }
    }
</script>

<script lang="ts">
    // technique for only specific properties to get updated on store change
    let { colorError, colorOnError, colorOnSurface, colorOnSurfaceVariant, colorOutlineVariant, colorPrimary, colorSurfaceContainerHigh } = $colors;
    $: ({ colorError, colorOnError, colorOnSurface, colorOnSurfaceVariant, colorOutlineVariant, colorPrimary, colorSurfaceContainerHigh } = $colors);

    let collectionView: NativeViewElementNode<CollectionView>;

    let items: ObservableArray<any>;

    export let type: 'pdf' | 'data' | 'image' = 'data';
    export let title = null;
    export let actionBarButtons = [{ icon: 'mdi-plus', id: 'add' }];

    const dataSyncServices = syncService.getStoredSyncServices();

    const options: Item[] = dataSyncServices.filter((s) => s.type.endsWith(type));

    function refresh() {
        const newItems: Item[] = options || [];

        items = new ObservableArray(newItems.map(prepareItem));
    }
    refresh();

    function prepareItem(item: Item) {
        const iconData = SERVICES_SYNC_ICONS_COLORS[item.type.split('_')[0]];
        const { color, ...others } = item;
        DEV_LOG && console.log('prepareItem0', item.type, iconData);
        return { ...others, syncColor: color, title: getTitle(item), subtitle: getDescription(item), icon: iconData?.icon, iconColor: iconData?.color, iconFontFamily: iconData?.fontFamily };
    }
    async function onItemTap(item: Item, event) {
        try {
            DEV_LOG && console.log('SyncListSettings', 'onItemTap', item);
            let configToUpdate;
            switch (item.type) {
                case SyncTypes.webdav_data:
                case SyncTypes.webdav_image:
                case SyncTypes.webdav_pdf: {
                    const type = item.type;
                    const page = await getSettingsComponent(type);
                    const result: WebdavDataSyncOptions = await showModal({
                        page,
                        fullscreen: true,
                        props: {
                            data: item as WebdavDataSyncOptions
                        }
                    });
                    if (result) {
                        configToUpdate = createWebdavConfig(result);
                    }
                    break;
                }
                case SyncTypes.folder_image:
                case SyncTypes.folder_pdf: {
                    const type = item.type;
                    const page = await getSettingsComponent(type);
                    const result: LocalFolderPDFSyncServiceOptions = await showModal({
                        page,
                        fullscreen: true,
                        props: {
                            data: item as LocalFolderPDFSyncServiceOptions
                        }
                    });
                    if (result) {
                        configToUpdate = result;
                    }
                    break;
                }
                case SyncTypes.gdrive_data:
                case SyncTypes.gdrive_pdf:
                case SyncTypes.gdrive_image: {
                    const type = item.type;
                    const page = await getSettingsComponent(type);
                    const result: BaseDataSyncServiceOptions & GoogleDriveSyncOptions = await showModal({
                        page,
                        fullscreen: true,
                        props: {
                            data: item as BaseDataSyncServiceOptions & GoogleDriveSyncOptions
                        }
                    });
                    if (result) {
                        configToUpdate = result;
                    }
                    break;
                }
                case SyncTypes.onedrive_data:
                case SyncTypes.onedrive_image:
                case SyncTypes.onedrive_pdf: {
                    const type = item.type;
                    const page = await getSettingsComponent(type);
                    const result: BaseDataSyncServiceOptions & OneDriveSyncOptions = await showModal({
                        page,
                        fullscreen: true,
                        props: {
                            data: item as BaseDataSyncServiceOptions & OneDriveSyncOptions
                        }
                    });
                    if (result) {
                        configToUpdate = result;
                    }
                    break;
                }
                case SyncTypes.paperless_pdf: {
                    const type = item.type;
                    const page = await getSettingsComponent(type);
                    const result: BaseSyncServiceOptions & PaperlessNgxSyncOptions = await showModal({
                        page,
                        fullscreen: true,
                        props: {
                            data: item as BaseSyncServiceOptions & PaperlessNgxSyncOptions
                        }
                    });
                    if (result) {
                        configToUpdate = result;
                    }
                    break;
                }
            }
            if (configToUpdate) {
                syncService.updateService(configToUpdate);
                const index = items.findIndex((d) => d.id === item.id);
                if (index !== -1) {
                    items.splice(index, 1, prepareItem(configToUpdate));
                }
            }
        } catch (err) {
            showError(err);
        }
    }
    async function deleteService(item: Item) {
        try {
            const removed = syncService.removeService(item);
            if (removed) {
                const index = items.findIndex((d) => d.id === item.id);
                if (index !== -1) {
                    items.splice(index, 1);
                }
            }
        } catch (err) {
            showError(err);
        }
    }

    async function onTap(item, event) {
        try {
            // DEV_LOG && console.log('SyncListSettings', 'onItemTap', item);
            switch (item.id) {
                case 'add':
                    const options = Object.keys(SERVICES_SYNC_TITLES)
                        .filter((s) => s.endsWith(type))
                        .map((s) => {
                            const key = s.split('_')[0];
                            const iconData = SERVICES_SYNC_ICONS_COLORS[key];
                            return {
                                data: s,
                                icon: iconData?.icon,
                                iconColor: iconData?.color,
                                iconFontFamily: iconData?.fontFamily,
                                name: SERVICES_SYNC_TITLES[s]
                            };
                        });
                    const selection = await showAlertOptionSelect(
                        {
                            fontWeight: 'normal',
                            height: Math.min(options.length * 56, ALERT_OPTION_MAX_HEIGHT),
                            rowHeight: 56,
                            options
                        },
                        {
                            title: lc('select_sync_service')
                        }
                    );
                    if (selection?.data) {
                        const { id, ...data } = item;
                        let configToAdd;
                        if (__ANDROID__) {
                            await requestNotificationPermission();
                        }
                        switch (selection?.data as SyncTypes) {
                            case SyncTypes.webdav_data:
                            case SyncTypes.webdav_image:
                            case SyncTypes.webdav_pdf: {
                                const type = selection?.data as SyncTypes.webdav_data | SyncTypes.webdav_image | SyncTypes.webdav_pdf;
                                const page = await getSettingsComponent(type);
                                const result: BaseDataSyncServiceOptions & WebdavSyncOptions = await showModal({
                                    page,
                                    fullscreen: true,
                                    props: {
                                        data
                                    }
                                });
                                if (result) {
                                    configToAdd = createWebdavConfig(result);
                                }
                                break;
                            }

                            case SyncTypes.folder_image:
                            case SyncTypes.folder_pdf: {
                                const type = selection?.data as SyncTypes.folder_image | SyncTypes.folder_pdf;
                                const page = await getSettingsComponent(type);
                                const result: LocalFolderPDFSyncServiceOptions = await showModal({
                                    page,
                                    fullscreen: true,
                                    props: {
                                        data
                                    }
                                });
                                if (result) {
                                    configToAdd = result;
                                }
                                break;
                            }
                            case SyncTypes.gdrive_data:
                            case SyncTypes.gdrive_pdf:
                            case SyncTypes.gdrive_image: {
                                const type = selection?.data as SyncTypes.gdrive_data | SyncTypes.gdrive_pdf | SyncTypes.gdrive_image;
                                const page = await getSettingsComponent(type);
                                const result: BaseDataSyncServiceOptions & GoogleDriveSyncOptions = await showModal({
                                    page,
                                    fullscreen: true,
                                    props: {
                                        data
                                    }
                                });
                                if (result) {
                                    configToAdd = result;
                                }
                                break;
                            }
                            case SyncTypes.onedrive_data:
                            case SyncTypes.onedrive_image:
                            case SyncTypes.onedrive_pdf: {
                                const type = selection?.data as SyncTypes.onedrive_data | SyncTypes.onedrive_image | SyncTypes.onedrive_pdf;
                                const page = await getSettingsComponent(type);
                                const result: BaseDataSyncServiceOptions & OneDriveSyncOptions = await showModal({
                                    page,
                                    fullscreen: true,
                                    props: {
                                        data
                                    }
                                });
                                if (result) {
                                    configToAdd = result;
                                }
                                break;
                            }
                            case SyncTypes.paperless_pdf: {
                                const page = await getSettingsComponent(SyncTypes.paperless_pdf);
                                const result: BaseSyncServiceOptions & PaperlessNgxSyncOptions = await showModal({
                                    page,
                                    fullscreen: true,
                                    props: {
                                        data
                                    }
                                });
                                if (result) {
                                    configToAdd = result;
                                }
                                break;
                            }
                        }
                        if (configToAdd) {
                            const data = syncService.addService(selection?.data, configToAdd);
                            DEV_LOG && console.log('adding item', data);
                            items.push(prepareItem(data));
                        }
                    }
            }
        } catch (err) {
            showError(err);
        } finally {
            hideLoading();
        }
    }
    onLanguageChanged(refresh);

    function refreshCollectionView() {
        collectionView?.nativeView?.refresh();
    }
    onThemeChanged(refreshCollectionView);

    function getTitle(item: Item) {
        const result = SERVICES_SYNC_TITLES[item.type];
        switch (item.type) {
            case 'gdrive_data':
            case 'gdrive_pdf':
            case 'gdrive_image':
                return 'Google Drive';
            case 'onedrive_data':
            case 'onedrive_pdf':
            case 'onedrive_image':
                return 'OneDrive';
            case 'paperless_pdf': {
                const serverUrl = (item as BaseSyncServiceOptions & PaperlessNgxSyncOptions).serverUrl;
                if (serverUrl) {
                    try {
                        const url = serverUrl.startsWith('http') ? serverUrl : 'https://' + serverUrl;
                        return result + url.split('//')[1].split('/')[0];
                    } catch (e) {
                        return result + serverUrl;
                    }
                }
                return 'Paperless-ngx';
            }
            case 'webdav_data':
            case 'webdav_pdf':
            case 'webdav_image':
                let url = (item as WebdavDataSyncOptions).remoteURL;
                if (!url.startsWith('http')) {
                    url = 'https://' + url;
                }
                return result + ': ' + url.split('//')[1].split('/')[0];
            case 'folder_image':
            case 'folder_pdf':
                return result;
            default:
                return item.title;
        }
    }
    function getDescription(item: Item) {
        switch (item.type) {
            case 'webdav_data':
            case 'gdrive_data':
            case 'onedrive_data':
                return (item as WebdavDataSyncOptions).remoteFolder;
            // case 'folder_image':
            //     return (item as LocalFolderImageSyncServiceOptions).localFolderPath;
            case 'folder_image':
            case 'folder_pdf':
                return getDirectoryName((item as LocalFolderImageSyncServiceOptions).localFolderPath);
            default:
                return item.description;
        }
    }

    function getSyncColor(item: Item) {
        return item.syncColor || SERVICES_SYNC_COLOR[item.type];
    }

    function selectTemplate(item, index, items) {
        // if (type === 'pdf') {
        //     return 'rightIcon';
        // }
        return 'default';
    }

    function drawerTranslationFunction(side, width, value, delta, progress) {
        const result = {
            mainContent: {
                translateX: -delta
            },
            rightDrawer: {
                translateX: width - delta
            },
            backDrop: {
                translateX: -delta,
                opacity: progress * 0.1
            }
        } as any;

        return result;
    }
</script>

<page id="syncSettingsPage" actionBarHidden={true}>
    <gridlayout class="pageContent" rows="auto,*">
        <collectionview bind:this={collectionView} itemTemplateSelector={selectTemplate} {items} row={1} android:paddingBottom={$windowInset.bottom}>
            <Template let:item>
                <swipemenu
                    gestureHandlerOptions={{
                        failOffsetYStart: -40,
                        failOffsetYEnd: 40,
                        minDist: 50
                    }}
                    rightSwipeDistance={0}
                    rows="*"
                    startingSide={item.startingSide}
                    translationFunction={drawerTranslationFunction}>
                    <ListItemAutoSize
                        borderLeftColor={getSyncColor(item)}
                        borderLeftWidth={5}
                        columns="auto,*,auto"
                        {item}
                        mainCol={1}
                        prop:mainContent
                        showBottomLine={false}
                        on:tap={(event) => onItemTap(item, event)}>
                        <label
                            col={0}
                            color={item.iconColor || colorOnSurface}
                            fontFamily={item.iconFontFamily || $fonts.mdi}
                            fontSize={(item.iconFontSize || 24) * $fontScale}
                            paddingLeft="8"
                            text={item.icon}
                            verticalAlignment="center"
                            width={(item.iconFontSize || 24) * 2} />
                    </ListItemAutoSize>
                    <label
                        class="mdi"
                        backgroundColor={colorError}
                        color={colorOnError}
                        prop:rightDrawer
                        fontSize={24}
                        rippleColor={colorOnError}
                        text="mdi-trash-can"
                        textAlignment="center"
                        verticalTextAlignment="middle"
                        width={80}
                        on:tap={(e) => deleteService(item)} />
                </swipemenu>
            </Template>
        </collectionview>
        <CActionBar canGoBack {title}>
            {#each actionBarButtons as button (button.id)}
                <mdbutton class="actionBarButton" text={button.icon} variant="text" on:tap={(event) => onTap({ id: button.id }, event)} />
            {/each}
        </CActionBar>
    </gridlayout>
</page>
