import { SDK_VERSION } from '@nativescript/core/utils';
import { ApplicationSettings, Screen, knownFolders } from '@nativescript/core';

export * from '@shared/constants';

export enum PDFImportImages {
    ask = 'ask',
    never = 'never',
    always = 'always'
}

export const SETTINGS_APP_VERSION = '_app_version';
export const SETTINGS_FIRST_OPEN = '_first_open';
export const SETTINGS_DOCUMENT_NAME_FORMAT = 'document_name_format';
export const SETTINGS_FILE_NAME_USE_DOCUMENT_NAME = 'filename_use_document_name';
export const SETTINGS_FILE_NAME_FORMAT = 'filename_date_format';
export const SETTINGS_IMPORT_PDF_IMAGES = 'import_pdf_images';
export const SETTINGS_CAMERA_SETTINGS = 'camera_settings';
export const SETTINGS_CROP_ENABLED = 'cropEnabled';
export const SETTINGS_ALWAYS_PROMPT_CROP_EDIT = 'always_prompt_crop_edit';
export const SETTINGS_REMOTE_AUTO_SYNC = 'webdav_auto_sync'; // we cant rename as it was already used before
export const SETTINGS_SYNC_SERVICES = 'sync_services'; // we cant rename as it was already used before
export const SETTINGS_IMAGE_EXPORT_FORMAT = 'image_export_format';
export const SETTINGS_IMAGE_EXPORT_QUALITY = 'image_export_quality';
export const SETTINGS_TRANSFORM_BATCH_SIZE = 'transform_batch_size';
export const SETTINGS_DEFAULT_TRANSFORM = 'defaultTransforms';
export const SETTINGS_DEFAULT_COLORTYPE = 'defaultColorType';
export const SETTINGS_DEFAULT_COLORMATRIX = 'defaultColorMatrix';
export const SETTINGS_DEFAULT_BRIGHTNESS = 'defaultBrightness';
export const SETTINGS_DEFAULT_CONTRAST = 'defaultContrast';
export const SETTINGS_MAGNIFIER_SENSITIVITY = 'magnifier_sensitivity';
export const SETTINGS_SYNC_ON_START = 'sync_on_start';
export const SETTINGS_DRAW_FOLDERS_BACKGROUND = 'draw_folder_background';
export const SETTINGS_START_ON_CAM = 'start_on_cam';
export const SETTINGS_FONT_CAM_MIRRORED = 'front_camera_mirrored';
export const SETTINGS_FORCE_WHITE_BACKGROUND_QRCODE = 'force_white_background_qrcode';
export const SETTINGS_NB_COLUMNS = 'nb_columns';
export const SETTINGS_NB_COLUMNS_LANDSCAPE = 'nb_columns_landscape';
export const SETTINGS_NB_COLUMNS_VIEW = 'nb_columns_view';
export const SETTINGS_NB_COLUMNS_VIEW_LANDSCAPE = 'nb_columns_view_landscape';
export const SETTINGS_SORT_ORDER = 'sort_order';
export const SETTINGS_VIEW_STYLE = 'documents_list_view_style';
export const SETTINGS_FOLDER_VIEW_STYLE = 'documents_list_folder_view_style';
export const SETTINGS_ROOT_DATA_FOLDER = 'root_data_folder';
export const SETTINGS_QUICK_TOGGLE_ENABLED = 'quick_toggle_enabled';
export const SETTINGS_OCR_COPY_USE_SPACE = 'ocr_copy_use_space';
export const SETTINGS_CARD_ALWAYS_SHOW_NAME = 'card_always_show_name';
export const SETTINGS_ENABLE_CRASH_REPORT = 'enable_crash_report';

export const TRANSFORMS_SPLIT = '|';

export const ANDROID_CONTENT = 'content://';
export const SEPARATOR = '/';

export const IMG_FORMAT = 'jpg';
export const PDF_EXT = '.pdf';
export const PKPASS_EXT = '.pkpass';
export const ESPASS_EXT = '.espass';
export const CARD_RATIO = 0.629;
export const IMAGE_DECODE_HEIGHT = Math.max(Screen.mainScreen.widthPixels, Screen.mainScreen.heightPixels);

export const BOTTOM_BUTTON_OFFSET = __ANDROID__ && SDK_VERSION < 30 ? 130 : 120;
export const FAB_BUTTON_OFFSET = 72;

export const AUTO_SYNC = true;
export const DEFAULT__BATCH_CHUNK_SIZE = 10;
export const FILTER_ROW_HEIGHT = CARD_APP ? 55 : 85;
export const FILTER_COL_WIDTH = CARD_APP ? FILTER_ROW_HEIGHT / CARD_RATIO : 60;
export const IMG_COMPRESS = 80;
export const PREVIEW_RESIZE_THRESHOLD = 200;
export const AREA_SCALE_MIN_FACTOR = 0.1;
export const QRCODE_RESIZE_THRESHOLD = 900;
export const COLOR_PALETTE_RESIZE_THRESHOLD = 100;
export const DOCUMENT_NOT_DETECTED_MARGIN = 10;
export const PDF_IMPORT_IMAGES = PDFImportImages.ask;
export const USE_SYSTEM_CAMERA = false;
export const CROP_ENABLED = true;
export const ALWAYS_PROMPT_CROP_EDIT = false;
export const MAGNIFIER_SENSITIVITY = 0.8;
export const TRANSFORM_BATCH_SIZE = 3;
export const AUTO_SCAN_ENABLED = true;
export const FILENAME_USE_DOCUMENT_NAME = true;
export const FILENAME_DATE_FORMAT = 'timestamp';
export const DOCUMENT_NAME_FORMAT = 'L LTS';
export const AUTO_SCAN_DISTANCETHRESHOLD = 50;
export const AUTO_SCAN_DURATION = 1000;
export const AUTO_SCAN_DELAY = 1000;
export const DEFAULT_FONT_CAM_MIRRORED = false;
export const DEFAULT_DRAW_FOLDERS_BACKGROUND = false;
export const DEFAULT_TRANSFORM = '';
export const DEFAULT_COLORTYPE = 'normal';
export const DEFAULT_CONTRAST = 1;
export const DEFAULT_BRIGHTNESS = 0;
export const DEFAULT_NB_COLUMNS_LANDSCAPE = 2;
export const DEFAULT_NB_COLUMNS = 1;
export const DEFAULT_NB_COLUMNS_VIEW_LANDSCAPE = 3;
export const DEFAULT_NB_COLUMNS_VIEW = 2;
export const DEFAULT_COLORMATRIX = null;
export const DEFAULT_SORT_ORDER = CARD_APP ? 'createdDate ASC' : 'createdDate DESC';
export const DEFAULT_VIEW_STYLE = CARD_APP ? 'columns' : 'default';
export const DEFAULT_FOLDER_VIEW_STYLE = 'horizontal';
export const DEFAULT_FORCE_WHITE_BACKGROUND_QRCODE = false;
export const DEFAULT_OCR_COPY_USE_SPACE = false;
export const DEFAULT_PDF_OPTIONS = {
    paper_size: 'full',
    color: 'color',
    orientation: 'portrait',
    page_padding: 10,
    jpegQuality: 80,
    items_per_page: 1,
    dpi: 96,
    imageSizeThreshold: 1500,
    imageLoadScale: 2,
    draw_ocr_text: true
};
export const DEFAULT_CARD_ALWAYS_SHOW_NAME = true;
export const IMAGE_CONTEXT_OPTIONS = {
    originalQueryCacheType: 0,
    originalStoreCacheType: 0,
    imageEncodeOptions: {
        encodeCompressionQuality: IMG_COMPRESS / 100
    }
};
export const DEFAULT_PDF_OPTIONS_STRING = JSON.stringify(DEFAULT_PDF_OPTIONS);
export const DEFAULT_EXPORT_DIRECTORY = __ANDROID__
    ? SDK_VERSION < 30
        ? android.os.Environment.getExternalStoragePublicDirectory(android.os.Environment.DIRECTORY_DOWNLOADS).getAbsolutePath()
        : undefined
    : undefined;

export function getImageExportSettings() {
    return {
        imageFormat: ApplicationSettings.getString(SETTINGS_IMAGE_EXPORT_FORMAT, IMG_FORMAT) as 'png' | 'jpeg' | 'jpg',
        imageQuality: ApplicationSettings.getNumber(SETTINGS_IMAGE_EXPORT_QUALITY, IMG_COMPRESS)
    };
}

export const EVENT_DOCUMENT_ADDED = 'documentAdded';
export const EVENT_DOCUMENT_MOVED_FOLDER = 'documentMovedFolder';
export const EVENT_DOCUMENT_UPDATED = 'documentUpdated';
export const EVENT_DOCUMENT_DELETED = 'documentsDeleted';
export const EVENT_DOCUMENT_PAGES_ADDED = 'documentPagesAdded';
export const EVENT_DOCUMENT_PAGE_DELETED = 'documentPageDeleted';
export const EVENT_DOCUMENT_PAGE_UPDATED = 'documentPageUpdated';
export const EVENT_DOCUMENT_USE_COUNT = 'documentUseCount';
export const EVENT_FOLDER_UPDATED = 'folderUpdated';
export const EVENT_FOLDER_ADDED = 'folderAdded';

export const EVENT_STATE = 'state';
export const EVENT_SYNC_STATE = 'syncState';
export const EVENT_SYNC_PROGRESS = 'syncProgress';

export const FOLDERS_DATA_FILENAME = 'folders.json';
export const DOCUMENT_DATA_FILENAME = 'data.json';
export const VALID_MARKER_FILENAME = '.valid';
