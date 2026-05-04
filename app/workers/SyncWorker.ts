import { BaseWorker, WorkerEvent } from '@akylas/nativescript-app-utils/worker/BaseWorker';
import Queue from '@akylas/nativescript-app-utils/worker/queue';
import { ApplicationSettings, File, Utils, knownFolders, path } from '@nativescript/core';
import { prefs } from '@shared/services/preferences';
import { cropDocumentFromFile } from 'plugin-nativeprocessor';
import { DocFolder, OCRDocument, OCRPage, getDocumentsService, setDocumentsService } from '~/models/OCRDocument';
import { DocumentEvents, DocumentsService } from '~/services/documents';
import { getTransformedImage } from '~/services/pdf/PDFExportCanvas.common';
import type { SyncStateEventData } from '~/services/sync';
import { BaseDataSyncService } from '~/services/sync/BaseDataSyncService';
import { BaseImageSyncService } from '~/services/sync/BaseImageSyncService';
import { BasePDFSyncService } from '~/services/sync/BasePDFSyncService';
import { BaseSyncService, getStoredSyncServices } from '~/services/sync/BaseSyncService';
import { GoogleDriveDataSyncService } from '~/services/sync/gdrive/GoogleDriveDataSyncService';
import { GoogleDriveImageSyncService } from '~/services/sync/gdrive/GoogleDriveImageSyncService';
import { GoogleDrivePDFSyncService } from '~/services/sync/gdrive/GoogleDrivePDFSyncService';
import { LocalFolderImageSyncService } from '~/services/sync/local/LocalFolderImageSyncService';
import { LocalFolderPDFSyncService } from '~/services/sync/local/LocalFolderPDFSyncService';
import { OneDriveDataSyncService } from '~/services/sync/onedrive/OneDriveDataSyncService';
import { OneDriveImageSyncService } from '~/services/sync/onedrive/OneDriveImageSyncService';
import { OneDrivePDFSyncService } from '~/services/sync/onedrive/OneDrivePDFSyncService';
import { SYNC_TYPES, SyncType, getRemoteDeleteDocumentSettingsKey } from '~/services/sync/types';
import { WebdavDataSyncService } from '~/services/sync/webdav/WebdavDataSyncService';
import { WebdavImageSyncService } from '~/services/sync/webdav/WebdavImageSyncService';
import { WebdavPDFSyncService } from '~/services/sync/webdav/WebdavPDFSyncService';
import {
    DOCUMENT_DATA_FILENAME,
    EVENT_DOCUMENT_ADDED,
    EVENT_DOCUMENT_PAGES_ADDED,
    EVENT_DOCUMENT_PAGE_DELETED,
    EVENT_DOCUMENT_PAGE_UPDATED,
    EVENT_DOCUMENT_UPDATED,
    EVENT_FOLDER_ADDED,
    EVENT_FOLDER_UPDATED,
    EVENT_SYNC_STATE,
    FOLDERS_DATA_FILENAME,
    IMG_COMPRESS,
    IMG_FORMAT,
    getImageExportSettings
} from '~/utils/constants';
import { recycleImages } from '~/utils/images';
import { basename } from '~/utils/path';
import { SyncNotificationManager } from '~/workers/SyncNotificationManager';

const context: Worker = self as any;

let documentsService: DocumentsService;

export const SERVICES_TYPE_MAP: { [key in SYNC_TYPES]: typeof BaseSyncService } = {
    webdav_data: WebdavDataSyncService,
    webdav_pdf: WebdavPDFSyncService,
    webdav_image: WebdavImageSyncService,
    folder_image: LocalFolderImageSyncService,
    folder_pdf: LocalFolderPDFSyncService,
    gdrive_data: GoogleDriveDataSyncService,
    gdrive_image: GoogleDriveImageSyncService,
    gdrive_pdf: GoogleDrivePDFSyncService,
    onedrive_data: OneDriveDataSyncService,
    onedrive_image: OneDriveImageSyncService,
    onedrive_pdf: OneDrivePDFSyncService
};

const TAG = '[SyncWorker]';

function findArrayDiffs<S, T>(array1: S[], array2: T[], compare: (a: S, b: T) => boolean) {
    const union: S[] = [];
    array1 = Array.from(array1);
    array2 = Array.from(array2);
    for (let i = 0; i < array1.length; i++) {
        const a = array1[i];
        for (let j = 0; j < array2.length; j++) {
            const b = array2[j];
            if (compare(a, b)) {
                union.push(a);
                array1.splice(i, 1);
                array2.splice(j, 1);
                i--;
                break;
            }
        }
    }
    return {
        toBeAdded: array2,
        toBeDeleted: array1,
        union
    };
}

export default class SyncWorker extends BaseWorker {
    constructor(protected context) {
        DEV_LOG && console.log(TAG, 'constructor');

        super(context);

        this.queue.on('done', () => {
            this.notify({ eventName: EVENT_SYNC_STATE, state: 'finished' } as SyncStateEventData);
            // ensure we unregister preferences or it will crash once the worker is closed
            prefs.destroy();
            this.stop();
        });
    }

    async handleStart(event: WorkerEvent) {
        if (!documentsService) {
            documentsService = new DocumentsService();
            DEV_LOG && console.warn('SyncWorker', 'handleStart', documentsService.id, event.data.nativeData.db);
            documentsService.notify = (e: any) => {
                if (e.eventName === 'started') {
                    return;
                }
                const { object, ...other } = e;
                if (other.changedProps) {
                    other.changedProps = [...other.changedProps];
                }
                this.notify({ ...other, target: 'documentsService', object: object === this ? undefined : object });
            };
            setDocumentsService(documentsService);
            await documentsService.start(event.data.nativeData.db);
        }
        if (!this.services) {
            const syncServices = this.getStoredSyncServices().filter((s) => s.enabled !== false);
            DEV_LOG && console.log('Sync', 'start services', syncServices.length);
            syncServices.forEach((data) => {
                SERVICES_TYPE_MAP[data.type].start(data);
                DEV_LOG && console.log('started sync service', data.type);
            });
            // AVAILABLE_SYNC_SERVICES.forEach((sClass) => sClass.start());
            this.services = BaseSyncService.getEnabledServices();
            DEV_LOG && console.log('Sync', 'started services', this.services.length);
        }
    }

    async receivedMessage(event: WorkerEvent) {
        const data = event.data;
        switch (data.type) {
            case 'sync':
                await worker.handleStart(event);
                this.syncDocumentsQueue(event.data.messageData);
                break;
            case 'stop':
                worker.stop(data.messageData?.error, data.id);
                break;
        }
    }

    get enabled() {
        return this.services?.length > 0;
    }
    services: BaseSyncService[];
    notificationManager: SyncNotificationManager;

    getStoredSyncServices() {
        return getStoredSyncServices();
    }

    queue = new Queue();

    // Helper method to update progress on both notification and event
    updateSyncProgress(type: 'data' | 'image' | 'pdf', current: number, total: number, documentId?: string, documentName?: string) {
        // Update Android notification
        this.notificationManager?.updateProgress(current, total, documentName);

        // Emit progress event
        this.notify({
            eventName: EVENT_SYNC_STATE,
            state: 'running',
            progress: { type, current, total, documentId, documentName }
        } as SyncStateEventData);
    }

    async syncDocumentsQueue(
        data: {
            withFolders?;
            force?;
            bothWays?;
            type?: number;
            event?: DocumentEvents;
        } = {}
    ) {
        return this.queue.add(() => this.syncDocumentsInternal(data));
    }
    async syncDocumentsInternal({
        bothWays = false,
        event,
        force = false,
        type,
        withFolders = false
    }: {
        withFolders?;
        force?;
        bothWays?;
        type?: number;
        event?: DocumentEvents;
    } = {}) {
        try {
            if (!documentsService.started) {
                return;
            }

            // Initialize notification manager if not already done
            this.notificationManager = new SyncNotificationManager();
            this.notificationManager?.showSyncStarted();

            this.notify({ eventName: EVENT_SYNC_STATE, state: 'running' } as SyncStateEventData);
            DEV_LOG && console.warn('syncDocuments', bothWays, event?.eventName, type);

            if (event) {
                if (event['doc']) {
                    event['doc'] = OCRDocument.fromJSON(event['doc']);
                }
                if (event['object']?.['pages']) {
                    event['object'] = OCRDocument.fromJSON(event['object'] as any);
                }
                if (event['documents']) {
                    event['documents'] = event['documents'].map((d) => OCRDocument.fromJSON(d));
                }
                if (event['pages']) {
                    event['pages'] = event['pages'].map((d) => OCRPage.fromJSON(d));
                }
            }
            // this.syncRunning = true;
            if (type === 0 || (type & SyncType.DATA) !== 0) {
                await this.syncDataDocuments({ force, bothWays, withFolders, event });
            }
            if (type === 0 || (type & SyncType.IMAGE) !== 0) {
                await this.syncImageDocuments({ force, event });
            }
            if (type === 0 || (type & SyncType.PDF) !== 0) {
                await this.syncPDFDocuments({ force, event });
            }

            // Show sync complete notification
            this.notificationManager?.showSyncComplete();
        } catch (error) {
            // console.error('error during worker sync', error, error.stack);

            // Show error notification
            this.notificationManager?.showSyncError(error?.message || 'Sync failed');

            this.sendError(error);
        } finally {
            console.warn('sync done');
        }
    }

    async syncDataDocuments({ bothWays = false, event, force = false, withFolders = false }: { withFolders?; force; bothWays; event: DocumentEvents }) {
        if (event && (event.eventName === EVENT_DOCUMENT_PAGES_ADDED || event.eventName === EVENT_DOCUMENT_PAGE_UPDATED || event.eventName === EVENT_DOCUMENT_PAGE_DELETED)) {
            // we ignore this event
            // pages will be updated independently
            return;
        }
        const localDocuments = event?.['doc'] ? [event['doc'] as OCRDocument] : ((event?.['documents'] as OCRDocument[]) ?? (await documentsService.documentRepository.findDocuments()));

        DEV_LOG &&
            console.info(
                'Sync',
                'syncDataDocuments',
                event?.eventName,
                localDocuments.map((d) => d.id)
            );
        await Promise.all(
            this.services
                .filter((s) => s instanceof BaseDataSyncService)
                .map(async (service) => {
                    DEV_LOG && console.log('syncDataDocuments', 'handling service', service.type, service.id, service.autoSync, force);
                    if (!service.shouldSync(force)) {
                        return;
                    }

                    const deleteKey = getRemoteDeleteDocumentSettingsKey(service);
                    const documentsToDeleteOnRemote: string[] = JSON.parse(ApplicationSettings.getString(deleteKey, '[]'));
                    if (bothWays) {
                        {
                            // first we sync folders
                            await service.ensureRemoteFolder();
                            DEV_LOG && console.log('syncing folders both ways');
                            const remoteFolders = ((await service.fileExists(FOLDERS_DATA_FILENAME)) ? JSON.parse(await service.getFileFromRemote(FOLDERS_DATA_FILENAME)) : []) as DocFolder[];
                            // we need to send folders not on remote
                            DEV_LOG && console.log('remoteFolders', JSON.stringify(remoteFolders));
                            const localFolders = await documentsService.folderRepository.search();
                            let needsRemoteChange = false;

                            const { toBeAdded: missingLocalFolders, toBeDeleted: missingRemoteFolders, union: toBeSyncFolders } = findArrayDiffs(localFolders, remoteFolders, (a, b) => a.id === b.id);
                            for (let index = 0; index < missingRemoteFolders.length; index++) {
                                remoteFolders.push(missingRemoteFolders[index].toJSON() as any);
                                needsRemoteChange = true;
                            }
                            for (let index = 0; index < missingLocalFolders.length; index++) {
                                DEV_LOG && console.log('creating folder from remote', JSON.stringify(missingLocalFolders[index]));
                                const folder = await getDocumentsService().folderRepository.create(missingLocalFolders[index]);
                                documentsService.notify({ eventName: EVENT_FOLDER_ADDED, folder });
                            }
                            for (let index = 0; index < toBeSyncFolders.length; index++) {
                                const folderId = toBeSyncFolders[index].id;
                                const remoteFolder = remoteFolders.find((f) => f.id === folderId);
                                const localFolder = localFolders.find((f) => f.id === folderId);
                                if (remoteFolder.modifiedDate > localFolder.modifiedDate) {
                                    Object.assign(remoteFolder, localFolder.toJSON());
                                    needsRemoteChange = true;
                                } else if (remoteFolder.modifiedDate < localFolder.modifiedDate) {
                                    localFolder.save(remoteFolder);
                                }
                            }
                            if (needsRemoteChange) {
                                await service.putFileContentsFromData(FOLDERS_DATA_FILENAME, JSON.stringify(remoteFolders));
                            }
                        }
                        const remoteDocuments = (await service.getRemoteFolderDirectories('')).filter((s) => s.type === 'directory');
                        DEV_LOG && console.log('remoteDocuments', JSON.stringify(remoteDocuments));
                        const {
                            toBeAdded: missingLocalDocuments,
                            toBeDeleted: missingRemoteDocuments,
                            union: toBeSyncDocuments
                        } = findArrayDiffs(localDocuments, remoteDocuments, (a, b) => a.id === b.basename);

                        DEV_LOG &&
                            console.log(
                                'missingRemoteDocuments',
                                missingRemoteDocuments.map((d) => d.id)
                            );
                        DEV_LOG &&
                            console.log(
                                'missingLocalDocuments',
                                missingLocalDocuments.map((d) => d.basename)
                            );
                        DEV_LOG &&
                            console.log(
                                'toBeSyncDocuments',
                                toBeSyncDocuments.map((d) => d.id)
                            );

                        // Calculate total items to sync for progress tracking
                        const totalItemsToSync = documentsToDeleteOnRemote.length + missingRemoteDocuments.length + missingLocalDocuments.length + toBeSyncDocuments.length;
                        let currentItemIndex = 0;

                        for (let index = 0; index < documentsToDeleteOnRemote.length; index++) {
                            const id = documentsToDeleteOnRemote[index];
                            const missingLocalIndex = missingLocalDocuments.findIndex((d) => d.basename === id);
                            if (missingLocalIndex !== -1) {
                                missingLocalDocuments.splice(missingLocalIndex, 1);
                                await service.removeDocumentFromRemote(id);
                                currentItemIndex++;
                                this.updateSyncProgress('data', currentItemIndex, totalItemsToSync, id);
                            }
                        }
                        ApplicationSettings.remove(deleteKey);

                        for (let index = 0; index < toBeSyncDocuments.length; index++) {
                            const doc = toBeSyncDocuments[index];
                            const canBeSynced = await this.syncDocumentOnRemote(doc, service);
                            if (canBeSynced === false) {
                                missingRemoteDocuments.push(doc);
                                continue;
                            }
                            currentItemIndex++;
                            this.updateSyncProgress('data', currentItemIndex, totalItemsToSync, doc.id, doc.name);
                        }
                        for (let index = 0; index < missingRemoteDocuments.length; index++) {
                            const doc = missingRemoteDocuments[index];
                            await service.addDocumentToRemote(doc);
                            doc.save({ _synced: doc._synced | service.syncMask }, false);
                            currentItemIndex++;
                            this.updateSyncProgress('data', currentItemIndex, totalItemsToSync, doc.id, doc.name);
                        }

                        for (let index = 0; index < missingLocalDocuments.length; index++) {
                            const data = await service.importDocumentFromRemote(missingLocalDocuments[index]);
                            if (data) {
                                const { doc, folder } = data;
                                await doc.save({ _synced: doc._synced | service.syncMask }, true, false);
                                DEV_LOG && console.log('importFolderFromWebdav done');
                                documentsService.notify({ eventName: EVENT_DOCUMENT_ADDED, doc, folder });
                                currentItemIndex++;
                                this.updateSyncProgress('data', currentItemIndex, totalItemsToSync, doc.id, doc.name);
                            }
                        }
                    } else {
                        if (withFolders || (event && (event.eventName === EVENT_FOLDER_ADDED || event.eventName === EVENT_FOLDER_UPDATED))) {
                            const remoteFolders = ((await service.fileExists(FOLDERS_DATA_FILENAME)) ? JSON.parse(await service.getFileFromRemote(FOLDERS_DATA_FILENAME)) : []) as any[];
                            // we need to send folders not on remote
                            const localFolders = await getDocumentsService().folderRepository.search();
                            let changed = false;
                            for (let index = 0; index < localFolders.length; index++) {
                                const folder = localFolders[index];
                                const remoteIndex = remoteFolders.findIndex((f) => folder.id === f.id);
                                DEV_LOG && console.log('should update remote folder', folder.id, remoteIndex);
                                if (remoteIndex >= 0) {
                                    const remoteFolder = remoteFolders[remoteIndex];
                                    DEV_LOG && console.log('updating remote folder', folder.id, remoteFolder.modifiedDate, folder.modifiedDate);
                                    if (remoteFolder.modifiedDate < folder.modifiedDate) {
                                        Object.assign(remoteFolder, folder.toJSON());
                                        changed = true;
                                    }
                                } else {
                                    DEV_LOG && console.log('creating remote folder', folder.id);
                                    remoteFolders.push(folder.toString());
                                    changed = true;
                                }
                            }
                            if (changed) {
                                await service.putFileContentsFromData(FOLDERS_DATA_FILENAME, JSON.stringify(remoteFolders));
                            }
                        }
                        // just test if we have local document marked as needing update
                        const documentsToSyncLength = localDocuments.filter((d) => (d._synced & service.syncMask) === 0).concat(documentsToDeleteOnRemote as any[]).length;
                        if (documentsToSyncLength) {
                            await service.ensureRemoteFolder();
                            const remoteDocuments = (await service.getRemoteFolderDirectories('')).filter((s) => s.type === 'directory');
                            DEV_LOG && console.log('remoteDocuments', JSON.stringify(remoteDocuments));
                            const {
                                toBeAdded: missingLocalDocuments,
                                toBeDeleted: missingRemoteDocuments,
                                union: toBeSyncDocuments
                            } = findArrayDiffs(localDocuments, remoteDocuments, (a, b) => a.id === b.basename);
                            DEV_LOG &&
                                console.log(
                                    'missingRemoteDocuments',
                                    missingRemoteDocuments.map((d) => d.id)
                                );
                            DEV_LOG &&
                                console.log(
                                    'missingLocalDocuments',
                                    missingLocalDocuments.map((d) => d.basename)
                                );
                            DEV_LOG &&
                                console.log(
                                    'toBeSyncDocuments',
                                    toBeSyncDocuments.map((d) => d.id)
                                );

                            // Calculate total items to sync for progress tracking
                            const totalItemsToSync = documentsToDeleteOnRemote.length + missingRemoteDocuments.length + toBeSyncDocuments.length;
                            let currentItemIndex = 0;

                            if (service.allowToRemoveOnRemote) {
                                for (let index = 0; index < documentsToDeleteOnRemote.length; index++) {
                                    const id = documentsToDeleteOnRemote[index];
                                    const missingLocalIndex = missingLocalDocuments.findIndex((d) => d.basename === id);
                                    if (missingLocalIndex !== -1) {
                                        missingLocalDocuments.splice(missingLocalIndex, 1);
                                        await service.removeDocumentFromRemote(id);
                                        currentItemIndex++;
                                        this.updateSyncProgress('data', currentItemIndex, totalItemsToSync, id);
                                    }
                                }
                            }
                            for (let index = 0; index < missingRemoteDocuments.length; index++) {
                                await service.addDocumentToRemote(missingRemoteDocuments[index]);
                                currentItemIndex++;
                                this.updateSyncProgress('data', currentItemIndex, totalItemsToSync, missingRemoteDocuments[index].id, missingRemoteDocuments[index].name);
                            }
                            // for (let index = 0; index < missingLocalDocuments.length; index++) {
                            //     await this.importDocumentFromWebdav(missingLocalDocuments[index]);
                            // }
                            for (let index = 0; index < toBeSyncDocuments.length; index++) {
                                await this.syncDocumentOnRemote(toBeSyncDocuments[index], service);
                                currentItemIndex++;
                                this.updateSyncProgress('data', currentItemIndex, totalItemsToSync, toBeSyncDocuments[index].id, toBeSyncDocuments[index].name);
                            }
                        }
                    }
                    ApplicationSettings.remove(deleteKey);
                    this.onServiceSyncDone(service);
                })
        );
        DEV_LOG && console.log('syncDataDocuments done');
    }

    onServiceSyncDone(service: BaseSyncService) {
        if (__ANDROID__) {
            const intent = new android.content.Intent(`${__APP_ID__}.SYNC_FINISHED`);
            intent.putExtra('service', service.id);
            intent.putExtra('type', service.type);
            const context: android.content.Context = Utils.android.getApplicationContext();
            context.sendBroadcast(intent);
        }
    }

    async syncDocumentOnRemote(document: OCRDocument, service: BaseDataSyncService) {
        let dataJSON: OCRDocument;
        try {
            dataJSON = JSON.parse(await service.getFileFromRemote(DOCUMENT_DATA_FILENAME, document)) as OCRDocument;
        } catch (error) {
            if (error.statusCode === 404) {
                return false;
            }
            throw error;
        }
        const docDataFolder = documentsService.dataFolder.getFolder(document.id);

        // Check if this is a legacy document (no .valid marker yet) for migration
        const hasValidMarker = await service.hasValidMarker(document.id);
        if (!hasValidMarker) {
            // we know that doc but it seems not to have a valid marker let s ignore
            return;
        }

        if (dataJSON.modifiedDate > document.modifiedDate) {
            let needsRemoteDocUpdate = false;
            const { folders: localFolders, pages: docPages, ...docProps } = document.toJSON();
            const { folders: remoteFolders, pages: remotePages, ...remoteProps } = dataJSON;
            const toUpdate = {};
            Object.keys(remoteProps).forEach((k) => {
                if (k.startsWith('_')) {
                    return;
                }
                if (remoteProps[k] !== docProps[k]) {
                    toUpdate[k] = remoteProps[k];
                }
            });
            const { toBeAdded: missingLocalPages, toBeDeleted: removedRemotePages, union: toBeSyncPages } = findArrayDiffs(docPages, remotePages, (a, b) => a.id === b.id);

            DEV_LOG &&
                console.warn(
                    'document need to be synced FROM webdav!',
                    toUpdate,
                    missingLocalPages,
                    removedRemotePages,
                    toBeSyncPages.map((p) => p.id)
                );
            DEV_LOG &&
                console.log(
                    'missingLocalPages',
                    missingLocalPages.map((p) => p.id)
                );
            DEV_LOG &&
                console.log(
                    'removedRemotePages',
                    removedRemotePages.map((p) => p.id)
                );
            DEV_LOG &&
                console.log(
                    'toBeSyncPages',
                    toBeSyncPages.map((p) => p.id)
                );
            for (let index = 0; index < removedRemotePages.length; index++) {
                const pageToRemove = removedRemotePages[index];
                const pageIndex = docPages.findIndex((p) => p.id === pageToRemove.id);
                if (pageIndex !== -1) {
                    document.deletePage(pageIndex);
                }
            }
            for (let index = 0; index < missingLocalPages.length; index++) {
                const missingLocalPage = missingLocalPages[index];
                const pageDataFolder = docDataFolder.getFolder(missingLocalPage.id);
                missingLocalPage.sourceImagePath = path.join(pageDataFolder.path, basename(missingLocalPage.sourceImagePath));
                missingLocalPage.imagePath = path.join(pageDataFolder.path, basename(missingLocalPage.imagePath));
                await service.importFolderFromRemote(path.join(document.id, missingLocalPage.id), pageDataFolder);

                // we insert page one by one because of the index
                await document.addPage(
                    missingLocalPage,
                    remotePages.findIndex((p) => p.id === missingLocalPage.id)
                );
                // await document.save();
            }
            for (let index = 0; index < toBeSyncPages.length; index++) {
                const localPage = toBeSyncPages[index];
                const localPageIndex = docPages.findIndex((p) => p.id === localPage.id);
                const remotePageIndex = remotePages.findIndex((p) => p.id === localPage.id);
                const remotePageToSync = remotePages[remotePageIndex];
                DEV_LOG && console.log('sync page', remotePageToSync.id, remotePageToSync.modifiedDate, localPage.modifiedDate);
                if (remotePageToSync.modifiedDate > localPage.modifiedDate) {
                    //we need to update the data and then recreate the image if necessary
                    const { imagePath: localImagePath, sourceImagePath: localSourceImagePath, ...pageProps } = localPage;
                    const { imagePath: remoteImagePath, sourceImagePath: remoteSourceImagePath, ...remotePageProps } = remotePageToSync;
                    const pageToUpdate: Partial<OCRPage> = {};
                    Object.keys(remotePageProps).forEach((k) => {
                        if (k.startsWith('_')) {
                            return;
                        }
                        if (remotePageProps[k] !== pageProps[k] && JSON.stringify(remotePageProps[k]) !== JSON.stringify(pageProps[k])) {
                            pageToUpdate[k] = remotePageProps[k];
                        }
                    });
                    // check if we need to recreate the image
                    let imageChanged = false;
                    DEV_LOG && console.log('sync page FROM webdav!', remotePageToSync.id, JSON.stringify(pageToUpdate));
                    if (pageToUpdate.crop || pageToUpdate.transforms) {
                        const file = File.fromPath(localPage.imagePath);
                        const crop = pageToUpdate.crop || localPage.crop;
                        const transforms = pageToUpdate.transforms || localPage.transforms;
                        imageChanged = true;
                        DEV_LOG && console.log('page sync needed size update', file.size, transforms, crop);

                        await cropDocumentFromFile(localPage.sourceImagePath, [crop], {
                            saveInFolder: file.parent.path,
                            fileName: file.name,
                            compressFormat: IMG_FORMAT,
                            compressQuality: IMG_COMPRESS,
                            transforms
                        });
                        pageToUpdate.size = file.size;
                    } else if (pageToUpdate.size === 0) {
                        const file = File.fromPath(localPage.imagePath);
                        pageToUpdate.size = file.size;
                    }
                    await document.updatePage(localPageIndex, pageToUpdate, imageChanged);
                } else if (remotePageToSync.modifiedDate < localPage.modifiedDate) {
                    //we need to update the data and then recreate the image if necessary
                    const { imagePath: localImagePath, sourceImagePath: localSourceImagePath, ...pageProps } = localPage;
                    const { imagePath: remoteImagePath, sourceImagePath: remoteSourceImagePath, ...remotePageProps } = remotePageToSync;
                    const pageTooUpdate: Partial<OCRPage> = {};
                    Object.keys(pageProps).forEach((k) => {
                        if (k.startsWith('_')) {
                            return;
                        }
                        if (remotePageProps[k] !== pageProps[k]) {
                            pageTooUpdate[k] = pageProps[k];
                        }
                    });
                    // check if we need to upload the image
                    DEV_LOG && console.log('sync page FROM local!', remotePageToSync.id, JSON.stringify(pageTooUpdate));
                    if (pageTooUpdate.crop || pageTooUpdate.transforms) {
                        await service.putFileContents(path.join(document.id, basename(localImagePath)), localImagePath);
                    }
                    needsRemoteDocUpdate = true;
                }
            }
            DEV_LOG && console.info('update document', needsRemoteDocUpdate, toUpdate, localFolders, remoteFolders);
            // mark the document as synced
            await document.save({ _synced: document._synced | service.syncMask, ...toUpdate });

            const { toBeAdded: missingLocalFolders, toBeDeleted: missingRemoteFolders, union: toBeSyncFolders } = findArrayDiffs(localFolders || [], remoteFolders || [], (a, b) => a === b);
            DEV_LOG && console.info('update document folders', document.id, document.name, document.folders, missingLocalFolders, missingRemoteFolders);
            for (let index = 0; index < missingRemoteFolders.length; index++) {
                DEV_LOG && console.info('doc missingRemoteFolders', document.id, missingRemoteFolders[index]);
                document.removeFromFolder(missingRemoteFolders[index]);
            }
            for (let index = 0; index < missingLocalFolders.length; index++) {
                const folderId = missingLocalFolders[index];
                const folder = await documentsService.folderRepository.findFolderById(folderId);
                document.setFolder({ folderId });
                DEV_LOG && console.info('doc missingLocalFolders', document.id, folderId, folder, document.folders);
            }

            if (needsRemoteDocUpdate) {
                // Remove .valid marker before updating to mark as invalid during sync
                await service.removeValidMarker(document.id);
                await service.putFileContentsFromData(path.join(document.id, DOCUMENT_DATA_FILENAME), document.toString());
                // Recreate .valid marker after successful update
                await service.createValidMarker(document.id);
            }
        } else if (dataJSON.modifiedDate < document.modifiedDate || (document.folders && !dataJSON.folders)) {
            // DEV_LOG && console.log('syncDocumentOnWebdav', document.id, document.modifiedDate, dataJSON.modifiedDate);
            const { pages: docPages, ...docProps } = document.toJSON?.() ?? document;
            const { pages: remotePages, ...remoteProps } = dataJSON;
            // const toUpdate = {};
            // Object.keys(remoteProps).forEach((k) => {
            //     if (k.startsWith('_')) {
            //         return;
            //     }
            //     if (remoteProps[k] !== docProps[k]) {
            //         toUpdate[k] = remoteProps[k];
            //     }
            // });
            const { toBeAdded: missingRemotePages, toBeDeleted: removedRemotePages, union: toBeSyncPages } = findArrayDiffs(remotePages, docPages, (a, b) => a.id === b.id);
            DEV_LOG && console.log('document need to be synced FROM local!', document.pages.length);
            DEV_LOG &&
                console.log(
                    'missingRemotePages',
                    missingRemotePages.map((p) => p.id)
                );
            DEV_LOG &&
                console.log(
                    'removedRemotePages',
                    removedRemotePages.map((p) => p.id)
                );
            DEV_LOG &&
                console.log(
                    'toBeSyncPages',
                    toBeSyncPages.map((p) => p.id)
                );
            for (let index = 0; index < missingRemotePages.length; index++) {
                const missingRemotePage = missingRemotePages[index];
                const pageDataFolder = docDataFolder.getFolder(missingRemotePage.id);
                await service.sendFolderToRemote(pageDataFolder, path.join(document.id, missingRemotePage.id));
            }
            for (let index = 0; index < removedRemotePages.length; index++) {
                const removedRemotePage = removedRemotePages[index];
                await service.deleteFile(path.join(document.id, removedRemotePage.id));
            }

            for (let index = 0; index < toBeSyncPages.length; index++) {
                const remotePage = toBeSyncPages[index];
                const remotePageIndex = remotePages.findIndex((p) => p.id === remotePage.id);
                const localPageIndex = docPages.findIndex((p) => p.id === remotePage.id);
                const localPageToSync = docPages[localPageIndex];
                DEV_LOG && console.log('sync page', localPageToSync.id, localPageToSync.modifiedDate, remotePage.modifiedDate);
                if (remotePage.modifiedDate > localPageToSync.modifiedDate) {
                    //we need to update the data and then recreate the image if necessary
                    const { imagePath: localImagePath, sourceImagePath: localSourceImagePath, ...pageProps } = localPageToSync;
                    const { imagePath: remoteImagePath, sourceImagePath: remoteSourceImagePath, ...remotePageProps } = remotePage;
                    const pageToUpdate: Partial<OCRPage> = {};
                    Object.keys(remotePageProps).forEach((k) => {
                        if (k.startsWith('_')) {
                            return;
                        }
                        if (remotePageProps[k] !== pageProps[k] && JSON.stringify(remotePageProps[k]) !== JSON.stringify(pageProps[k])) {
                            pageToUpdate[k] = remotePageProps[k];
                        }
                    });
                    // check if we need to recreate the image
                    DEV_LOG && console.log('sync page FROM webdav!', localPageToSync.id, JSON.stringify(pageToUpdate));
                    let imageChanged = false;
                    if (pageToUpdate.crop || pageToUpdate.transforms) {
                        const file = File.fromPath(localPageToSync.imagePath);

                        const crop = pageToUpdate.crop || localPageToSync.crop;
                        const transforms = pageToUpdate.transforms || localPageToSync.transforms;
                        DEV_LOG && console.log('page sync needed size update', file.size, transforms, crop);
                        await cropDocumentFromFile(localPageToSync.sourceImagePath, [crop], {
                            saveInFolder: file.parent.path,
                            fileName: file.name,
                            compressFormat: IMG_FORMAT,
                            compressQuality: IMG_COMPRESS,
                            transforms
                        });
                        pageToUpdate.size = file.size;
                        imageChanged = true;
                    } else if (pageToUpdate.size === 0) {
                        const file = File.fromPath(localPageToSync.imagePath);
                        pageToUpdate.size = file.size;
                    }
                    await document.updatePage(localPageIndex, pageToUpdate, imageChanged);
                } else if (remotePage.modifiedDate < localPageToSync.modifiedDate) {
                    //we need to update the data and then recreate the image if necessary
                    const { imagePath: localImagePath, sourceImagePath: localSourceImagePath, ...pageProps } = localPageToSync;
                    const { imagePath: remoteImagePath, sourceImagePath: remoteSourceImagePath, ...remotePageProps } = remotePage;
                    const pageTooUpdate: Partial<OCRPage> = {};
                    Object.keys(pageProps).forEach((k) => {
                        if (k.startsWith('_')) {
                            return;
                        }
                        if (remotePageProps[k] !== pageProps[k]) {
                            pageTooUpdate[k] = pageProps[k];
                        }
                    });
                    // check if we need to upload the image
                    DEV_LOG && console.log('sync page FROM local!', localPageToSync.id, JSON.stringify(pageTooUpdate));
                    if (pageTooUpdate.crop || pageTooUpdate.transforms) {
                        await service.putFileContents(path.join(document.id, basename(localImagePath)), localImagePath);
                    }
                }
            }
            // Remove .valid marker before updating to mark as invalid during sync
            await service.removeValidMarker(document.id);
            await service.putFileContentsFromData(path.join(document.id, DOCUMENT_DATA_FILENAME), document.toString(), { overwrite: true });
            // Recreate .valid marker after successful update
            await service.createValidMarker(document.id);
            return document.save({ _synced: document._synced | service.syncMask });
        } else if ((document._synced & service.syncMask) === 0) {
            return document.save({ _synced: document._synced | service.syncMask });
        }
    }
    async syncImageDocuments({ event, force = false }: { force; event: DocumentEvents }) {
        if (event?.eventName === EVENT_DOCUMENT_UPDATED || event?.eventName === EVENT_DOCUMENT_ADDED || event?.eventName === EVENT_DOCUMENT_PAGE_DELETED) {
            // we ignore this event
            // pages will be updated independently
            return;
        }
        let localDocuments = event?.['pages']
            ? [{ document: event['doc'] as OCRDocument, pages: event['pages'] as OCRPage[] }]
            : event?.['pageIndex'] !== undefined
              ? [{ document: event['doc'] as OCRDocument, pages: [event['doc']['pages'][event['pageIndex']]] as OCRPage[] }]
              : (await documentsService.documentRepository.search()).map((d) => ({ document: d, pages: d.pages }));

        // this should not happened but i got bug reports with null document. cant reproduce
        localDocuments = localDocuments.filter((d) => !!d.document);
        DEV_LOG &&
            console.log(
                'Sync',
                'syncImageDocuments',
                event?.eventName,
                localDocuments.map((d) => d.document.id)
            );

        const defaultExportSettings = getImageExportSettings();
        await Promise.all(
            this.services
                .filter((s) => s instanceof BaseImageSyncService)
                .map(async (service) => {
                    DEV_LOG && console.log('syncImageDocuments', 'handling service', service.type, service.id, service.autoSync, force);
                    if (!service.shouldSync(force, event)) {
                        return;
                    }
                    const exportFormat = service.imageFormat || defaultExportSettings.imageFormat;
                    const exportQuality = service.imageQuality || defaultExportSettings.imageQuality;
                    const deleteKey = getRemoteDeleteDocumentSettingsKey(service);

                    // just test if we have local document marked as needing update
                    const documentsToSync = localDocuments.filter((d) => force || (d.document._synced & service.syncMask) === 0);
                    // DEV_LOG && console.log('syncImageDocuments', 'documentsToSync', documentsToSync.length);
                    if (documentsToSync.length) {
                        await service.ensureRemoteFolder();
                        const remoteFiles = await service.getRemoteFolderFiles('');
                        DEV_LOG && console.log('remoteFiles', JSON.stringify(remoteFiles));

                        // Calculate total pages for progress tracking
                        const totalPages = documentsToSync.reduce((sum, d) => sum + d.pages.length, 0);
                        let currentPageIndex = 0;

                        for (let index = 0; index < documentsToSync.length; index++) {
                            const doc = documentsToSync[index];
                            for (let j = 0; j < doc.pages.length; j++) {
                                const page = doc.pages[j];
                                const name = service.getImageName(doc.document, page, j, exportFormat);
                                const existing = remoteFiles.find((r) => r.basename === name);
                                DEV_LOG && console.info('syncImageDocuments', 'test', doc.document.id, j, existing?.lastmod, page.modifiedDate);
                                if (!existing || new Date(existing.lastmod).valueOf() < page.modifiedDate) {
                                    const document = doc.document;
                                    // we need to create the image on remote
                                    const imageSource = await getTransformedImage({
                                        page,
                                        options: {
                                            colorMatrix: service.colorMatrix,
                                            brightness: service.brightness,
                                            contrast: service.contrast
                                        },
                                        document
                                    });
                                    if (imageSource) {
                                        try {
                                            await service.writeImage(
                                                imageSource,
                                                name,
                                                exportFormat,
                                                exportQuality,
                                                true,
                                                service.useFoldersStructure && document.folders?.length ? await documentsService.folderRepository.findFolderById(document.folders[0]) : null
                                            );
                                        } finally {
                                            recycleImages(imageSource);
                                        }
                                    }
                                }
                                currentPageIndex++;
                                this.updateSyncProgress('image', currentPageIndex, totalPages, doc.document.id, doc.document.name);
                            }
                            await doc.document.save({ _synced: doc.document._synced | service.syncMask });
                        }
                    }
                    ApplicationSettings.remove(deleteKey);
                    this.onServiceSyncDone(service);
                })
        );
        DEV_LOG && console.log('syncImageDocuments done ');
    }

    async syncPDFDocuments({ event, force = false }: { force; event: DocumentEvents }) {
        if (event?.eventName === EVENT_DOCUMENT_UPDATED) {
            // we ignore this event
            // pages will be updated independently
            return;
        }
        let localDocuments = event?.['pages']
            ? [{ document: event['doc'] as OCRDocument, pages: event['pages'] as OCRPage[] }]
            : event?.['pageIndex'] !== undefined
              ? [{ document: event['doc'] as OCRDocument, pages: [event['doc']['pages'][event['pageIndex']]] as OCRPage[] }]
              : (await documentsService.documentRepository.search()).map((d) => ({ document: d, pages: d.pages }));

        // this should not happened but i got bug reports with null document. cant reproduce
        localDocuments = localDocuments.filter((d) => !!d.document);
        DEV_LOG &&
            console.log(
                'Sync',
                'syncPDFDocuments',
                event?.eventName,
                localDocuments.map((d) => d.document.id)
            );

        await Promise.all(
            this.services
                .filter((s) => s instanceof BasePDFSyncService)
                .map(async (service) => {
                    DEV_LOG && console.log('syncPDFDocuments', 'handling service', service.type, service.id, service.autoSync, force);
                    if (!service.shouldSync(force, event)) {
                        return;
                    }
                    const deleteKey = getRemoteDeleteDocumentSettingsKey(service);
                    // just test if we have local document marked as needing update
                    const documentsToSync = localDocuments.filter((d) => force || (d.document._synced & service.syncMask) === 0);
                    // DEV_LOG && console.log('syncImageDocuments', 'documentsToSync', documentsToSync.length);
                    if (documentsToSync.length) {
                        await service.ensureRemoteFolder();
                        DEV_LOG && console.log('ensureRemoteFolder done');
                        const remoteFiles = await service.getRemoteFolderFiles('');
                        DEV_LOG && console.log('remoteFiles', JSON.stringify(remoteFiles));
                        const baseOCRDataPath = ApplicationSettings.getString('tesseract_datapath_base', path.join(knownFolders.documents().path, 'tesseract'));

                        // Calculate total documents for progress tracking
                        const totalDocuments = documentsToSync.length;
                        let currentDocIndex = 0;

                        for (let index = 0; index < documentsToSync.length; index++) {
                            const doc = documentsToSync[index];
                            const pages = doc.pages.filter((p) => !!p);
                            const document = doc.document;
                            //see if we need to OCR
                            if (service.OCREnabled && service.OCRLanguages.length && event?.eventName !== EVENT_DOCUMENT_PAGE_DELETED) {
                                const OCRDataPath = path.join(baseOCRDataPath, service.OCRDataType);
                                // we need to make sure the OCR update wont trigger another sync or we will end up in an endless loop
                                await Promise.all(
                                    pages.map(async (p, index) => document.ocrPage({ pageIndex: index, language: service.OCRLanguages.join('+'), dataPath: OCRDataPath, notify: false }))
                                );
                            }

                            const name = service.getPDFName(document);
                            const existing = remoteFiles.find((r) => r.basename === name);
                            DEV_LOG && console.info('syncPDFDocuments', 'test', document.id, existing?.lastmod, document.modifiedDate);
                            if (!existing || new Date(existing.lastmod).valueOf() < document.modifiedDate) {
                                await service.writePDF(
                                    document,
                                    name,
                                    service.useFoldersStructure && document.folders?.length ? await documentsService.folderRepository.findFolderById(document.folders[0]) : null
                                );
                            }
                            await document.save({ _synced: document._synced | service.syncMask });
                            currentDocIndex++;
                            this.updateSyncProgress('pdf', currentDocIndex, totalDocuments, document.id, document.name);
                        }
                    }
                    DEV_LOG && console.log('syncPDFDocuments', 'handling service done', service.type, service.id, service.autoSync, force);
                    ApplicationSettings.remove(deleteKey);
                    this.onServiceSyncDone(service);
                })
        );
        DEV_LOG && console.log('syncPDFDocuments done ');
    }
}

const worker = new SyncWorker(context);
