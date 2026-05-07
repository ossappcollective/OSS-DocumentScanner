import { File, Screen, knownFolders, path } from '@nativescript/core';
import { wrapNativeException } from '@nativescript/core/utils';
import { generatePDFASync } from 'plugin-nativeprocessor';
import type { DocFolder, OCRDocument } from '~/models/OCRDocument';
import { networkService } from '~/services/api';
import { DocumentEvents } from '~/services/documents';
import PDFExportCanvas from '~/services/pdf/PDFExportCanvas';
import { BasePDFSyncService, BasePDFSyncServiceOptions } from '~/services/sync/BasePDFSyncService';
import { PaperlessNgxSyncOptions, PaperlessTask, ensureToken, fetchTasks, listDocuments, updateDocumentVersion, uploadDocument } from '~/services/sync/paperless/PaperlessNgx';
import { SERVICES_SYNC_MASK } from '~/services/sync/types';
import { PDF_EXT } from '~/utils/constants';
import { getPageColorMatrix } from '~/utils/matrix';
import type { FileStat } from '~/webdav';

export interface PaperlessNgxPDFSyncServiceOptions extends BasePDFSyncServiceOptions, PaperlessNgxSyncOptions {}

/** Key in doc.extra where the linked Paperless document ID is stored. */
const EXTRA_PAPERLESS_ID_KEY = 'paperless_pdf_id';

/** Polling interval in milliseconds. */
const POLL_INTERVAL_MS = 2000;
const MAX_POLL_TIME_MS = 20000;

export class PaperlessNgxPDFSyncService extends BasePDFSyncService {
    shouldSync(force?: boolean, event?: DocumentEvents) {
        return (force || (event && this.autoSync)) && networkService.connected;
    }
    static type = 'paperless_pdf';
    type = PaperlessNgxPDFSyncService.type;
    syncMask = SERVICES_SYNC_MASK[PaperlessNgxPDFSyncService.type];
    serverUrl: string;
    token: string;
    username?: string;
    password?: string;

    /** Map from task UUID to its promise resolvers, used for polling. */
    private pendingTasks = new Map<string, { resolve: (id: number) => void; reject: (err: Error) => void }>();
    /** Single shared polling loop promise, null when not running. */
    private pollingPromise: Promise<void> | null = null;

    static start(config?: { id: number; [k: string]: any }) {
        if (config) {
            const service = PaperlessNgxPDFSyncService.getOrCreateInstance();
            Object.assign(service, config);
            return service;
        }
    }

    override stop() {}

    /**
     * Paperless-ngx manages its own storage — no remote folder to create.
     */
    override async ensureRemoteFolder(): Promise<any> {
        return ensureToken(this);
    }

    override async getRemoteFolderFiles(_relativePath: string): Promise<FileStat[]> {
        const documents = await listDocuments(this);
        return documents.map((doc) => {
            const baseName = doc.original_file_name || `${doc.title}.pdf`;
            const displayName = baseName.endsWith(PDF_EXT) ? baseName : `${baseName}${PDF_EXT}`;
            return {
                filename: displayName,
                basename: displayName,
                lastmod: doc.modified || doc.added || new Date().toISOString(),
                size: 0,
                type: 'file' as const,
                mime: 'application/pdf'
            };
        });
    }

    /**
     * Register a task UUID and return a Promise that resolves with the Paperless
     * document ID once the task reaches SUCCESS, or rejects on failure.
     * Starts the polling loop if not already running.
     */
    private waitForTask(taskUuid: string): Promise<number> {
        return new Promise<number>((resolve, reject) => {
            this.pendingTasks.set(taskUuid, { resolve, reject });
            this.startPolling();
        });
    }
    startPollingStartTime;
    private startPolling() {
        if (!this.pollingPromise) {
            this.startPollingStartTime = Date.now();
            this.pollingPromise = this.pollLoop();
        }
    }

    private async pollLoop() {
        while (this.pendingTasks.size > 0 && Date.now() - this.startPollingStartTime < MAX_POLL_TIME_MS) {
            await new Promise<void>((r) => setTimeout(r, POLL_INTERVAL_MS));
            try {
                const tasks: PaperlessTask[] = await fetchTasks(this);
                for (const task of tasks) {
                    const pending = this.pendingTasks.get(task.task_id);
                    if (!pending) {
                        continue;
                    }
                    if (task.status === 'SUCCESS') {
                        this.pendingTasks.delete(task.task_id);
                        pending.resolve(task.related_document);
                    } else if (task.status === 'FAILURE' || task.status === 'REVOKED') {
                        this.pendingTasks.delete(task.task_id);
                        pending.reject(new Error(`Paperless task ${task.task_id} failed with status ${task.status}: ${task.result ?? ''}`));
                    }
                }
            } catch (err) {
                DEV_LOG && console.error('PaperlessNgxPDFSyncService', 'pollLoop error', err);
            }
        }
        this.pollingPromise = null;
    }

    override async writePDF(document: OCRDocument, fileName: string, _docFolder?: DocFolder) {
        const pages = document.pages;
        if (!pages || pages.length === 0) {
            return;
        }
        if (!fileName.endsWith(PDF_EXT)) {
            fileName += PDF_EXT;
        }
        const temp = knownFolders.temp().path;

        if (__ANDROID__) {
            const exportOptions = this.exportOptions;
            const black_white = exportOptions.color === 'black_white';
            const options = JSON.stringify({
                overwrite: true,
                text_scale: Screen.mainScreen.scale * 1.4,
                pages: pages.map((p) => ({ ...p, colorMatrix: getPageColorMatrix(p, black_white ? 'grayscale' : undefined) })),
                ...exportOptions
            });
            await generatePDFASync(temp, fileName, options, wrapNativeException);
        } else {
            const exporter = new PDFExportCanvas();
            await exporter.export({ pages: pages.map((page) => ({ page, document })), folder: temp, filename: fileName, compress: true, options: this.exportOptions });
        }
        const localFilePath = path.join(temp, fileName);
        try {
            const existingPaperlessId = document.extra?.[EXTRA_PAPERLESS_ID_KEY] as number;

            if (existingPaperlessId) {
                // Document already exists on Paperless — upload a new version
                await updateDocumentVersion(this, existingPaperlessId, fileName, File.fromPath(localFilePath));
            } else {
                // New document — upload and wait for the task to resolve with the Paperless doc ID
                const taskUuid = await uploadDocument(this, fileName, File.fromPath(localFilePath));
                const paperlessDocId = await this.waitForTask(taskUuid);
                await document.save({ extra: { [EXTRA_PAPERLESS_ID_KEY]: paperlessDocId } }, false, false);
            }
        } finally {
            try {
                File.fromPath(localFilePath).remove();
            } catch (_) {
                // ignore cleanup errors
            }
        }
    }
}
