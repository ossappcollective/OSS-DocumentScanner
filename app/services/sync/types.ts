import { type BaseSyncService } from '~/services/sync/BaseSyncService';

export enum SyncTypes {
    webdav_data = 'webdav_data',
    folder_image = 'folder_image',
    folder_pdf = 'folder_pdf',
    webdav_image = 'webdav_image',
    webdav_pdf = 'webdav_pdf',
    gdrive_data = 'gdrive_data',
    gdrive_image = 'gdrive_image',
    gdrive_pdf = 'gdrive_pdf',
    onedrive_data = 'onedrive_data',
    onedrive_image = 'onedrive_image',
    onedrive_pdf = 'onedrive_pdf',
    paperless_pdf = 'paperless_pdf'
}

export type SYNC_TYPES = keyof typeof SyncTypes;
export const SERVICES_SYNC_MASK: { [key in SYNC_TYPES]: number } = {
    webdav_data: 1 << 2,
    folder_image: 1 << 3,
    folder_pdf: 1 << 4,
    webdav_image: 1 << 5,
    webdav_pdf: 1 << 6,
    gdrive_data: 1 << 7,
    gdrive_image: 1 << 8,
    gdrive_pdf: 1 << 9,
    onedrive_data: 1 << 10,
    onedrive_image: 1 << 11,
    onedrive_pdf: 1 << 12,
    paperless_pdf: 1 << 13
};

export const SERVICES_SYNC_ICONS_COLORS: { [key: string]: { icon: string; color?: string; fontFamily?: string } } = {
    webdav: { icon: 'app-webdav', color: '#347df0', fontFamily: 'app' },
    gdrive: { icon: 'app-gdrive', color: '#f7b403', fontFamily: 'app' },
    onedrive: { icon: 'app-onedrive', color: '#28a3e4', fontFamily: 'app' },
    folder: { icon: 'mdi-folder' }
};
export const SERVICES_SYNC_COLOR: { [key in SYNC_TYPES]: string } = {
    webdav_pdf: '#8293CE',
    webdav_image: '#C2EF1F',
    webdav_data: '#BC768B',
    folder_image: '#C18F4E',
    folder_pdf: '#4CA49D',
    gdrive_data: '#4285F4',
    gdrive_image: '#34A853',
    gdrive_pdf: '#FBBC05',
    onedrive_data: '#0078D4',
    onedrive_image: '#50E6FF',
    onedrive_pdf: '#00BCF2',
    paperless_pdf: '#17541F'
};

export enum SyncType {
    DATA = 1 << 2,
    IMAGE = 1 << 3,
    PDF = 1 << 4,
    ALL = 0
}

export function getRemoteDeleteDocumentSettingsKey(s: BaseSyncService) {
    return s.type + '_docs_to_remove_remote';
}

export interface SyncProgressData {
    type: 'data' | 'image' | 'pdf';
    current: number;
    total: number;
    documentId?: string;
    documentName?: string;
}
