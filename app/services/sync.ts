import BaseWorkerHandler from '@akylas/nativescript-app-utils/worker/BaseWorkerHandler';
import { lc } from '@nativescript-community/l';
import { ApplicationSettings, EventData } from '@nativescript/core';
import { debounce } from '@nativescript/core/utils';
import { CustomError } from '@akylas/nativescript-app-utils/error';
import { showError } from '@shared/utils/showError';
import { writable } from 'svelte/store';
import { OCRDocument, OCRPage } from '~/models/OCRDocument';
import {
    EVENT_DOCUMENT_ADDED,
    EVENT_DOCUMENT_DELETED,
    EVENT_DOCUMENT_MOVED_FOLDER,
    EVENT_DOCUMENT_PAGES_ADDED,
    EVENT_DOCUMENT_PAGE_DELETED,
    EVENT_DOCUMENT_PAGE_UPDATED,
    EVENT_DOCUMENT_UPDATED,
    EVENT_FOLDER_ADDED,
    EVENT_FOLDER_UPDATED,
    EVENT_STATE,
    EVENT_SYNC_STATE,
    SETTINGS_SYNC_SERVICES
} from '~/utils/constants';
import type SyncWorker from '~/workers/SyncWorker';
import {
    DocumentAddedEventData,
    DocumentDeletedEventData,
    DocumentEventData,
    DocumentEvents,
    DocumentPagesAddedEventData,
    DocumentUpdatedEventData,
    FolderUpdatedEventData,
    documentsService
} from './documents';
import { SYNC_TYPES, SyncType, getRemoteDeleteDocumentSettingsKey } from './sync/types';
import { getStoredSyncServices } from '~/services/sync/BaseSyncService';

export const syncServicesStore = writable([]);

const SETTINGS_KEY = 'webdav_config';

export function findArrayDiffs<S, T>(array1: S[], array2: T[], compare: (a: S, b: T) => boolean) {
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

export interface SyncStateEventData extends EventData {
    state: 'finished' | 'running';
    progress?: {
        type: 'data' | 'image' | 'pdf';
        current: number;
        total: number;
        documentId?: string;
        documentName?: string;
    };
}
export interface SyncEnabledEventData extends EventData {
    enabled: boolean;
}

const SYNC_DELAY = 1000;

// function debounceSync(fn, delay = 300, { ignoreFirstTrail = false, leading = false, trailing = true } = {}) {
//     let timer;
//     return (event) => {
//         let noTrail = false;
//         if (timer === undefined && leading) {
//             noTrail = ignoreFirstTrail === true;
//             fn.call(this, event);
//         }
//         clearTimeout(timer);
//         timer = setTimeout(() => {
//             if (trailing !== false && !noTrail) {
//                 syncService.syncDocuments({ force: true });
//             }
//             timer = undefined;
//         }, delay);
//     };
// }

export class SyncService extends BaseWorkerHandler<SyncWorker> {
    constructor() {
        super(() => new Worker('~/workers/SyncWorker'));
    }
    handleError(error: any) {
        showError(error);
    }
    handleWorkerError(error: any) {
        showError(CustomError.fromObject({ ...error, sentryReportTranslatedName: true }));
    }
    async onWorkerEvent(eventData) {
        // DEV_LOG && console.log('onWorkerEvent', eventData);

        // DEV_LOG && console.info('worker event', documentsService.id, eventData.eventName, eventData.target, !!eventData.object, Object.keys(eventData));
        if (eventData.target === 'documentsService') {
            if (eventData.doc) {
                eventData.doc = OCRDocument.fromJSON(eventData.doc);
            }
            if (eventData.changedProps) {
                eventData.changedProps = new Set(eventData.changedProps);
            }
            if (eventData.documents) {
                eventData.documents = eventData.documents.map((d) => OCRDocument.fromJSON(d));
            }
            if (eventData.pages) {
                eventData.pages = eventData.pages.map((d) => OCRPage.fromJSON(d));
            }
            if (eventData.object?.id === eventData.doc?.id) {
                eventData.object = eventData.doc;
            } else {
                delete eventData.object;
            }
            // DEV_LOG && console.info('worker notifying event', documentsService.id, eventData.eventName, documentsService.notify);
            documentsService.notify({ ...eventData, object: eventData.object || documentsService, fromWorker: true });
        } else {
            this.notify({ ...eventData });
        }
    }
    services: any[] = [];
    onDocumentAdded(event: DocumentAddedEventData) {
        DEV_LOG && console.log('SYNC', 'onDocumentAdded', event.fromWorker);
        let type = SyncType.PDF;
        if (event.fromWorker !== true) {
            type |= SyncType.DATA;
        }
        this.syncDocumentsInternal({ event, type, fromEvent: event.eventName });
    }
    onDocumentDeleted(event: DocumentDeletedEventData) {
        DEV_LOG && console.log('SYNC', 'onDocumentDeleted');

        this.getStoredSyncServices()
            .filter((s) => s.enabled !== false)
            // .filter((s) => s instanceof BaseDataSyncService)
            .forEach(async (service) => {
                const key = getRemoteDeleteDocumentSettingsKey(service as any);
                const documentsToDeleteOnRemote = JSON.parse(ApplicationSettings.getString(key, '[]'));
                documentsToDeleteOnRemote.push(...event.documents.map((d) => d.id));
                ApplicationSettings.setString(key, JSON.stringify(documentsToDeleteOnRemote));
            });
        this.sendDataEvent(event);
    }
    onDocumentUpdated(event: DocumentUpdatedEventData) {
        // only used for data sync
        DEV_LOG && console.log('SYNC', 'onDocumentUpdated', event.updateModifiedDate);
        if (event.updateModifiedDate !== false && event.fromWorker !== true) {
            // let type = SyncType.PDF;
            // if (event.fromWorker !== true) {
            // let type = SyncType.DATA;
            // }
            this.syncDocumentsInternal({ event, type: SyncType.DATA, fromEvent: event.eventName });
        }
    }
    sendImageEvent(event: DocumentPagesAddedEventData) {
        DEV_LOG && console.log('Sync', 'sendImageEvent');
        // only used for image sync
        this.syncDocumentsInternal({ event, type: SyncType.IMAGE, fromEvent: event.eventName });
    }
    sendImagesEvent(event: DocumentPagesAddedEventData) {
        DEV_LOG && console.log('Sync', 'sendImagesEvent');
        // only used for image sync
        this.syncDocumentsInternal({ event, type: SyncType.IMAGE | SyncType.PDF, fromEvent: event.eventName });
    }
    sendPDFEvent(event: DocumentPagesAddedEventData) {
        this.syncDocumentsInternal({ event, type: SyncType.PDF, fromEvent: event.eventName });
    }
    sendDataEvent(event: DocumentEventData) {
        if (event.fromWorker !== true) {
            this.syncDocumentsInternal({ event, type: SyncType.DATA, fromEvent: event.eventName });
        }
    }
    get enabled() {
        return this.services?.length > 0;
    }

    getStoredSyncServices() {
        return getStoredSyncServices();
    }

    addService(type: SYNC_TYPES, data) {
        const syncServices = this.getStoredSyncServices();
        const dataToAdd = { type, id: Date.now(), ...data };
        syncServices.push(dataToAdd);
        ApplicationSettings.setString(SETTINGS_SYNC_SERVICES, JSON.stringify(syncServices));
        DEV_LOG && console.log('Sync', 'addService', type, JSON.stringify(data));
        if (!this.enabled) {
            this.start();
        } else {
            this.onServiceChanged();
        }
        return dataToAdd;
    }
    removeService(data) {
        const syncServices = this.getStoredSyncServices();
        const index = syncServices.findIndex((s) => s.id === data.id);
        DEV_LOG && console.log('Sync', 'removeService', index, JSON.stringify(data));
        if (index !== -1) {
            syncServices.splice(index, 1);
            ApplicationSettings.setString(SETTINGS_SYNC_SERVICES, JSON.stringify(syncServices));
        }
        this.onServiceChanged();
        return true;
    }
    updateService(data) {
        const syncServices = this.getStoredSyncServices();
        const index = syncServices.findIndex((s) => s.id === data.id);
        DEV_LOG && console.log('Sync', 'updateService', index, JSON.stringify(data));
        if (index !== -1) {
            syncServices.splice(index, 1, data);
            ApplicationSettings.setString(SETTINGS_SYNC_SERVICES, JSON.stringify(syncServices));
        }
        this.onServiceChanged(true);
        return true;
        // }
    }
    async start() {
        if (this.enabled) {
            return;
        }
        const syncServices = (this.services = this.getStoredSyncServices().filter((s) => s.enabled !== false));
        DEV_LOG && console.log('Sync', 'start', syncServices);
        syncServicesStore.set(syncServices);
        // DEV_LOG && console.log('Sync', 'start', /* this.services.length,  */ this.enabled);
        if (this.enabled) {
            this.notify({ eventName: EVENT_STATE, enabled: this.enabled } as SyncEnabledEventData);
            documentsService.on(EVENT_DOCUMENT_ADDED, this.onDocumentAdded, this);
            documentsService.on(EVENT_DOCUMENT_UPDATED, this.onDocumentUpdated, this);
            documentsService.on(EVENT_DOCUMENT_DELETED, this.onDocumentDeleted, this);
            documentsService.on(EVENT_DOCUMENT_PAGE_UPDATED, this.sendImageEvent, this);
            documentsService.on(EVENT_DOCUMENT_PAGES_ADDED, this.sendImageEvent, this);
            documentsService.on(EVENT_DOCUMENT_PAGE_DELETED, this.sendPDFEvent, this);
            documentsService.on(EVENT_DOCUMENT_MOVED_FOLDER, this.sendDataEvent, this);
            documentsService.on(EVENT_FOLDER_UPDATED, this.sendDataEvent, this);
            documentsService.on(EVENT_FOLDER_ADDED, this.sendDataEvent, this);
            this.on(EVENT_SYNC_STATE, this.onSyncState, this);

            DEV_LOG && console.log('SyncService', 'start');
        }
    }
    async stop() {
        DEV_LOG && console.log('Sync', 'stop');
        this.off(EVENT_SYNC_STATE, this.onSyncState, this);
        documentsService.off(EVENT_DOCUMENT_ADDED, this.onDocumentAdded, this);
        documentsService.off(EVENT_DOCUMENT_UPDATED, this.onDocumentUpdated, this);
        documentsService.off(EVENT_DOCUMENT_DELETED, this.onDocumentDeleted, this);
        documentsService.off(EVENT_DOCUMENT_PAGE_UPDATED, this.sendImageEvent, this);
        documentsService.off(EVENT_DOCUMENT_PAGES_ADDED, this.sendImageEvent, this);
        documentsService.off(EVENT_DOCUMENT_PAGE_DELETED, this.sendPDFEvent, this);
        documentsService.off(EVENT_DOCUMENT_MOVED_FOLDER, this.sendDataEvent, this);
        documentsService.off(EVENT_FOLDER_UPDATED, this.sendDataEvent, this);
        documentsService.off(EVENT_FOLDER_ADDED, this.sendDataEvent, this);
        if (this.syncRunning) {
            // if sync is running wait for it to be finished
            await new Promise((resolve) => this.once(EVENT_SYNC_STATE, resolve));
        }
        this.worker?.stop();
        this.worker = null;
    }

    onServiceChanged(force = false) {
        const services = this.getStoredSyncServices().filter((s) => s.enabled !== false);
        this.services = services;
        syncServicesStore.set(services);
        DEV_LOG && console.log('onServiceChanged', this.services.length);
    }

    syncRunning = false;
    onSyncState(event: EventData & { state: 'running' | 'finished' }) {
        this.syncRunning = event.state === 'running';
    }

    syncDocuments = debounce(
        async ({
            bothWays = false,
            force = true,
            type = SyncType.ALL,
            withFolders = false
        }: {
            withFolders?;
            force?;
            bothWays?;
            type?: number;
        } = {}) => {
            this.syncDocumentsInternal({ force, bothWays, type, withFolders });
        },
        SYNC_DELAY
    );

    async syncDocumentsInternal(
        data: {
            withFolders?;
            force?;
            bothWays?;
            type?: number;
            fromEvent?: string;
            event?: DocumentEvents;
        } = {}
    ) {
        try {
            const db = documentsService.db?.db?.db;
            if (!this.enabled || !db) {
                return;
            }
            // send syncState event right now for the UI to be updated as soon as possible
            this.notify({ eventName: EVENT_SYNC_STATE, state: 'running' } as SyncStateEventData);
            let eventData;
            if (data.event) {
                const { object, ...otherProps } = data.event;
                // if (object?.['toJSONObject']) {
                //     eventData = { ...otherProps, object: event.object['toJSONObject']?.() };
                // } else {
                eventData = otherProps;
                // }
                DEV_LOG && console.log('syncDocumentsInternal', data.type, data.fromEvent, Object.keys(otherProps));
            }
            await this.sendMessageToWorker(
                'sync',
                {
                    ...data,
                    event: eventData
                },
                undefined,
                undefined,
                false,
                0,
                { db: documentsService.db.db.db }
            );
        } catch (error) {
            DEV_LOG && console.error(error);
            showError(error);
        }
    }
}
export const syncService = new SyncService();
