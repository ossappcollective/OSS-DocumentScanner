import { lc } from '@nativescript-community/l';
import { SYNC_TYPES } from '~/services/sync/types';

export const SERVICES_SYNC_TITLES: { [key in SYNC_TYPES]: string } = {
    webdav_image: lc('webdav_server'),
    webdav_pdf: lc('webdav_server'),
    webdav_data: lc('webdav_server'),
    folder_image: lc('local_folder'),
    folder_pdf: lc('local_folder'),
    gdrive_image: 'Google Drive',
    gdrive_pdf: 'Google Drive',
    gdrive_data: 'Google Drive',
    onedrive_image: 'OneDrive',
    onedrive_pdf: 'OneDrive',
    onedrive_data: 'OneDrive',
    paperless_pdf: 'Paperless-ngx'
};
