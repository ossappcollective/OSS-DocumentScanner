import { ApplicationSettings } from '@nativescript/core';
import dayjs from 'dayjs';
import type { OCRDocument } from '~/models/OCRDocument';
import { FILENAME_DATE_FORMAT, FILENAME_USE_DOCUMENT_NAME, SETTINGS_FILE_NAME_FORMAT, SETTINGS_FILE_NAME_USE_DOCUMENT_NAME } from '~/utils/constants';
export { restartApp } from '@akylas/nativescript-app-utils';

// type Many<T> = T | T[];
export function pick<T extends object, U extends keyof T>(object: T, ...props: U[]): Pick<T, U> {
    return props.reduce((o, k) => ((o[k] = object[k]), o), {} as any);
}
export function omit<T extends object, U extends keyof T>(object: T, ...props: U[]): Omit<T, U> {
    return Object.keys(object)
        .filter((key) => (props as string[]).indexOf(key) < 0)
        .reduce((newObj, key) => Object.assign(newObj, { [key]: object[key] }), {} as any);
}

export function cleanFilename(str, wsRep = (w) => '_') {
    return str.replace(/[\x00-\x1f\x7f"*\/<>?\\:|\0x7f]+/g, wsRep).replace(/\s/g, wsRep);
}
export function getFormatedDateForFilename(value?: number, dateFormat = ApplicationSettings.getString(SETTINGS_FILE_NAME_FORMAT, FILENAME_DATE_FORMAT), clean = true) {
    const now = dayjs(value);

    // DEV_LOG && console.log('getFormatedDateForFilename', value, now, dateFormat, now.valueOf(), now.toISOString(), now.format(dateFormat));
    let result: string;
    switch (dateFormat.trim()) {
        case 'timestamp':
            result = now.valueOf() + '';
            break;
        case 'iso':
            result = now.toISOString();
            break;
        default:
            result = now.format(dateFormat);
            break;
    }
    return clean ? cleanFilename(result) : result;
}

export function getFileNameForDocument(
    document?: OCRDocument,
    useDocumentName = ApplicationSettings.getBoolean(SETTINGS_FILE_NAME_USE_DOCUMENT_NAME, FILENAME_USE_DOCUMENT_NAME),
    date?: number,
    dateFormat?: string
) {
    if (useDocumentName === true && document?.name) {
        return cleanFilename(document.name);
    }
    return getFormatedDateForFilename(date, dateFormat);
}

export function getRealPath(src: string) {
    return src;
}

type SortOrder = 'ASC' | 'DESC';
export function sortByKey<T = any>(data: T[], sort: string): T[] {
    const [rawKey, rawOrder] = sort.trim().split(/\s+/);

    const key = rawKey as keyof T;
    const order: SortOrder = (rawOrder?.toUpperCase() as SortOrder) || 'ASC';
    const direction = order === 'DESC' ? -1 : 1;

    return [...data].sort((a, b) => {
        const aValue = a[key];
        const bValue = b[key];

        if (aValue == null) return -1 * direction;
        if (bValue == null) return 1 * direction;

        if (typeof aValue === 'number' && typeof bValue === 'number') {
            return (aValue - bValue) * direction;
        }

        // Fallback to string comparison
        return String(aValue).localeCompare(String(bValue)) * direction;
    });
}

export function ellipsize(str: string, maxLength: number): string {
    if (str.length <= maxLength) return str;

    if (maxLength <= 1) return '…';

    return str.slice(0, maxLength - 1) + '…';
}
