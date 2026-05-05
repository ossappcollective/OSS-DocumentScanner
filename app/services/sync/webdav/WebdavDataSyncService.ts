import { File, Folder, path } from '@nativescript/core';
import { type OCRDocument } from '~/models/OCRDocument';
import { networkService } from '~/services/api';
import { DocumentEvents } from '~/services/documents';
import { BaseDataSyncService, BaseDataSyncServiceOptions } from '~/services/sync/BaseDataSyncService';
import { SERVICES_SYNC_MASK } from '~/services/sync/types';
import { WebdavSyncOptions } from '~/services/sync/webdav/Webdav';
import { AuthType, FileStat, GetFileContentsOptions, ResponseData, WebDAVClient, createClient } from '~/webdav';

export interface WebdavDataSyncOptions extends BaseDataSyncServiceOptions, WebdavSyncOptions {}

export class WebdavDataSyncService extends BaseDataSyncService {
    shouldSync(force?: boolean, event?: DocumentEvents) {
        return (force || (event && this.autoSync)) && networkService.connected;
    }
    static type = 'webdav_data';
    type = WebdavDataSyncService.type;
    syncMask = SERVICES_SYNC_MASK[WebdavDataSyncService.type];
    remoteURL;
    username;
    authType;
    client: WebDAVClient;
    token;
    password;

    stop() {}
    static start(config?: { id: number; [k: string]: any }) {
        if (config) {
            // const config = JSON.parse(econfigStr);
            const { authType, headers, remoteURL, ...otherConfig } = config;
            const service = WebdavDataSyncService.getOrCreateInstance();
            Object.assign(service, config);
            DEV_LOG && console.log('WebdavDataSyncService', 'start', JSON.stringify(config), service.autoSync);
            // const context = createContext(config.remoteURL, { config.username, password, authType: AuthType.Password });
            service.client = createClient(remoteURL, {
                headers,
                authType: !authType || authType === AuthType.Password ? AuthType.None : authType,
                ...otherConfig
            });
            return service;
        }
    }
    override async ensureRemoteFolder() {
        DEV_LOG && console.log('ensureRemoteFolder', this.remoteFolder);
        if (!(await this.client.exists(this.remoteFolder))) {
            return this.client.createDirectory(this.remoteFolder, { recursive: true });
        }
    }

    override async getRemoteFolderDirectories(relativePath: string) {
        return this.client.getDirectoryContents(path.join(this.remoteFolder, relativePath), { includeSelf: false, details: false }) as Promise<FileStat[]>;
    }

    override getFileContents<V extends 'binary' | 'text' | 'file' | 'json' = 'json'>(filePath: string, options?: GetFileContentsOptions & { format?: V }): Promise<ResponseData<V>> {
        return this.client.getFileContents(filePath, options);
    }

    override async sendFolderToRemote(folder: Folder, remoteRelativePath: string) {
        const remotePath = path.join(this.remoteFolder, remoteRelativePath);
        DEV_LOG && console.log('sendFolderToWebDav', folder.path, remotePath);
        try {
            await this.client.createDirectory(remotePath, { recursive: false });
        } catch (error) {
            if (error.statusCode !== 405) {
                throw error;
            }
        }

        const entities = await folder.getEntities();
        for (let index = 0; index < entities.length; index++) {
            const entity = entities[index];
            if (entity instanceof File) {
                await this.putFileContents(path.join(remotePath, entity.name), entity.path);
            } else {
                await this.sendFolderToRemote(Folder.fromPath(entity.path), path.join(remoteRelativePath, entity.name));
            }
        }
    }
    override async fileExists(filename: string) {
        return this.client.exists(path.join(this.remoteFolder, filename));
    }

    override async getFileFromRemote(filename: string, document?: OCRDocument) {
        const remoteDocPath = document ? path.join(this.remoteFolder, document.id) : this.remoteFolder;
        const result = await this.client.getFileContents(path.join(remoteDocPath, filename), {
            format: 'text'
        });
        DEV_LOG && console.log('getFileFromRemote', result);
        return result;
    }

    override async putFileContents(relativePath: string, localFilePath: string, options?) {
        return this.client.putFileContents(relativePath, File.fromPath(localFilePath), options);
    }
    override putFileContentsFromData(relativePath: string, data: string, options?) {
        return this.client.putFileContents(relativePath, data, options);
    }
    override async deleteFile(relativePath: string) {
        return this.client.deleteFile(relativePath);
    }
}
