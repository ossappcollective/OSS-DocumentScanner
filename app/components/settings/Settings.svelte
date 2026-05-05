<script context="module" lang="ts">
    import { CheckBox } from '@nativescript-community/ui-checkbox';
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { openFilePicker, pickFolder, saveFile } from '@nativescript-community/ui-document-picker';
    import { Label } from '@nativescript-community/ui-label';
    import { showBottomSheet } from '@nativescript-community/ui-material-bottomsheet/svelte';
    import { alert, confirm, prompt } from '@nativescript-community/ui-material-dialogs';
    import { TextField, TextFieldProperties } from '@nativescript-community/ui-material-textfield';
    import { TextView } from '@nativescript-community/ui-material-textview';
    import { ApplicationSettings, File, ObservableArray, Page, ScrollView, StackLayout, Utils, View, knownFolders, path } from '@nativescript/core';
    import { Sentry, startSentry, stopSentry } from '@shared/utils/sentry';
    import { showError } from '@shared/utils/showError';
    import { navigate } from '@shared/utils/svelte/ui';
    import dayjs from 'dayjs';
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import CActionBar from '~/components/common/CActionBar.svelte';
    import ListItemAutoSize from '@shared/components/ListItemAutoSize.svelte';
    import { getLocaleDisplayName, l, lc, lu, onLanguageChanged, selectLanguage, slc } from '~/helpers/locale';
    import { getColorThemeDisplayName, getThemeDisplayName, onThemeChanged, selectColorTheme, selectTheme } from '~/helpers/theme';
    import { DocumentsService, documentsService } from '~/services/documents';
    import { securityService } from '~/services/security';
    import { backupWorkerService } from '~/services/backupWorker';
    import {
        ALERT_OPTION_MAX_HEIGHT,
        ALWAYS_PROMPT_CROP_EDIT,
        AUTO_SCAN_DELAY,
        AUTO_SCAN_DISTANCETHRESHOLD,
        AUTO_SCAN_DURATION,
        AUTO_SCAN_ENABLED,
        CROP_ENABLED,
        DEFAULT_CARD_ALWAYS_SHOW_NAME,
        DEFAULT_DRAW_FOLDERS_BACKGROUND,
        DEFAULT_EXPORT_DIRECTORY,
        DEFAULT_FONT_CAM_MIRRORED,
        DEFAULT_FORCE_WHITE_BACKGROUND_QRCODE,
        DEFAULT_NB_COLUMNS,
        DEFAULT_NB_COLUMNS_LANDSCAPE,
        DEFAULT_NB_COLUMNS_VIEW,
        DEFAULT_NB_COLUMNS_VIEW_LANDSCAPE,
        DEFAULT_OCR_COPY_USE_SPACE,
        DEFAULT_PDF_OPTIONS_STRING,
        DOCUMENT_NAME_FORMAT,
        DOCUMENT_NOT_DETECTED_MARGIN,
        FILENAME_DATE_FORMAT,
        FILENAME_USE_DOCUMENT_NAME,
        IMG_COMPRESS,
        IMG_FORMAT,
        MAGNIFIER_SENSITIVITY,
        PDFImportImages,
        PDF_IMPORT_IMAGES,
        PREVIEW_RESIZE_THRESHOLD,
        SETTINGS_ALWAYS_PROMPT_CROP_EDIT,
        SETTINGS_CARD_ALWAYS_SHOW_NAME,
        SETTINGS_CROP_ENABLED,
        SETTINGS_DOCUMENT_NAME_FORMAT,
        SETTINGS_DRAW_FOLDERS_BACKGROUND,
        SETTINGS_ENABLE_CRASH_REPORT,
        SETTINGS_FILE_NAME_FORMAT,
        SETTINGS_FILE_NAME_USE_DOCUMENT_NAME,
        SETTINGS_FONT_CAM_MIRRORED,
        SETTINGS_FORCE_WHITE_BACKGROUND_QRCODE,
        SETTINGS_IMAGE_EXPORT_FORMAT,
        SETTINGS_IMAGE_EXPORT_QUALITY,
        SETTINGS_IMPORT_PDF_IMAGES,
        SETTINGS_MAGNIFIER_SENSITIVITY,
        SETTINGS_NB_COLUMNS,
        SETTINGS_NB_COLUMNS_LANDSCAPE,
        SETTINGS_NB_COLUMNS_VIEW,
        SETTINGS_NB_COLUMNS_VIEW_LANDSCAPE,
        SETTINGS_OCR_COPY_USE_SPACE,
        SETTINGS_QUICK_TOGGLE_ENABLED,
        SETTINGS_ROOT_DATA_FOLDER,
        SETTINGS_START_ON_CAM,
        SETTINGS_SYNC_ON_START,
        SETTINGS_TRANSFORM_BATCH_SIZE,
        TRANSFORM_BATCH_SIZE,
        USE_SYSTEM_CAMERA
    } from '~/utils/constants';
    import { copyFolderContent, removeFolderContent } from '~/utils/file';
    import { PDF_OPTIONS } from '~/utils/localized_constant';
    import { createView, getNameFormatHTMLArgs, hideLoading, openLink, showAlertOptionSelect, showLoading, showSettings, showSliderPopover, showSnack } from '~/utils/ui';
    import { restartApp, toggleQuickSetting } from '~/utils/utils';
    import { colors, fontScale, fonts, hasCamera, onFontScaleChanged, windowInset } from '~/variables';
    import IconButton from '@shared/components/IconButton.svelte';
    import { share } from '@akylas/nativescript-app-utils/share';
    import { inappItems, presentInAppSponsorBottomsheet } from '@shared/utils/inapp-purchase';
    import OCRSettingsBottomSheet from '../ocr/OCRSettingsBottomSheet.svelte';
    import { restoreSettings } from '~/utils/settings.android';
    const version = __APP_VERSION__ + ' Build ' + __APP_BUILD_NUMBER__;
    const storeSettings = {};
    const variant = 'outline';

    const numberTextFieldProperties = {
        keyboardType: 'integer'
    } as TextFieldProperties;
</script>

<script lang="ts">
    // technique for only specific properties to get updated on store change
    let { colorOnBackground, colorOnSurfaceVariant, colorPrimary } = $colors;
    $: ({ colorOnBackground, colorOnSurfaceVariant, colorPrimary } = $colors);

    let collectionView: NativeViewElementNode<CollectionView>;
    let page: NativeViewElementNode<Page>;

    let items: ObservableArray<any>;

    const inAppAvailable = PLAY_STORE_BUILD && inappItems?.length > 0;

    const dataSettingsAvailable = __ANDROID__ && android.os.Environment.getExternalStorageState() === 'mounted';

    export let title = null;
    export let id = 'settingsPage';
    export let actionBarButtons = [
        { icon: 'mdi-message-alert', id: 'feedback' },
        { icon: 'mdi-share-variant', id: 'share' },
        { icon: 'mdi-github', id: 'github' }
    ];
    export let subSettingsOptions: string = null;
    export let options: any[] = null;
    if (!options && subSettingsOptions) {
        options = getSubSettings(subSettingsOptions);
    }

    function getTitle(item) {
        switch (item.id) {
            case 'token':
                return lc(item.token);
            default:
                return item.title;
        }
    }
    function getDescription(item) {
        return typeof item.description === 'function' ? item.description(item) : item.description;
    }

    function getStoreSetting(k: string, defaultValue) {
        if (!storeSettings[k]) {
            storeSettings[k] = JSON.parse(ApplicationSettings.getString(k, defaultValue));
        }
        return storeSettings[k];
    }
    function getSubSettings(id: string) {
        switch (id) {
            case 'ocr':
                return [
                    {
                        type: 'switch',
                        id: SETTINGS_OCR_COPY_USE_SPACE,
                        title: lc('use_space_when__ocr_copy_text'),
                        description: lc('use_space_when__ocr_copy_text_desc'),
                        value: ApplicationSettings.getBoolean(SETTINGS_OCR_COPY_USE_SPACE, DEFAULT_OCR_COPY_USE_SPACE)
                    },
                    {
                        type: 'ocr_settings',
                        id: 'ocr_settings'
                    }
                ];
            case 'camera':
                return (
                    __ANDROID__
                        ? [
                              {
                                  type: 'switch',
                                  id: 'use_system_camera',
                                  title: lc('use_system_camera'),
                                  description: lc('use_system_camera_desc'),
                                  value: ApplicationSettings.getBoolean('use_system_camera', USE_SYSTEM_CAMERA)
                              }
                          ]
                        : []
                ).concat([
                    {
                        type: 'switch',
                        id: SETTINGS_START_ON_CAM,
                        title: lc('start_app_on_cam'),
                        description: lc('start_app_on_cam_desc'),
                        value: ApplicationSettings.getBoolean(SETTINGS_START_ON_CAM, START_ON_CAM)
                    },
                    {
                        type: 'switch',
                        id: SETTINGS_FONT_CAM_MIRRORED,
                        title: lc('front_cam_mirrored'),
                        description: lc('front_cam_mirrored_desc'),
                        value: ApplicationSettings.getBoolean(SETTINGS_FONT_CAM_MIRRORED, DEFAULT_FONT_CAM_MIRRORED)
                    }
                ]);
            case 'security':
                return []
                    .concat(
                        __ANDROID__
                            ? [
                                  {
                                      type: 'switch',
                                      key: 'allow_screenshot',
                                      title: lc('allow_app_screenshot'),
                                      value: ApplicationSettings.getBoolean('allow_screenshot', true)
                                  }
                              ]
                            : ([] as any)
                    )
                    .concat(
                        // securityService.biometricsAvailable
                        [
                            {
                                type: 'switch',
                                id: 'biometric_lock',
                                title: lc('biometric_lock'),
                                description: lc('biometric_lock_desc'),
                                enabled: securityService.biometricsAvailable,
                                value: securityService.biometricEnabled
                            },
                            {
                                type: 'switch',
                                id: 'biometric_auto_lock',
                                title: lc('biometric_auto_lock'),
                                description: lc('biometric_auto_lock_desc'),
                                enabled: securityService.biometricsAvailable,
                                value: securityService.biometricEnabled && securityService.autoLockEnabled
                            }
                        ]
                        // : []
                    );
            case 'document_detection':
                return [
                    {
                        type: 'switch',
                        id: SETTINGS_CROP_ENABLED,
                        title: lc('crop_enabled'),
                        value: ApplicationSettings.getBoolean(SETTINGS_CROP_ENABLED, CROP_ENABLED)
                    },
                    {
                        type: 'switch',
                        id: SETTINGS_ALWAYS_PROMPT_CROP_EDIT,
                        title: lc('always_prompt_crop_edit'),
                        description: lc('always_prompt_crop_edit_desc'),
                        value: ApplicationSettings.getBoolean(SETTINGS_ALWAYS_PROMPT_CROP_EDIT, ALWAYS_PROMPT_CROP_EDIT)
                    },
                    {
                        id: 'setting',
                        key: 'previewResizeThreshold',
                        title: lc('preview_resize_threshold'),
                        description: lc('preview_resize_threshold_desc'),
                        rightValue: () => ApplicationSettings.getNumber('previewResizeThreshold', PREVIEW_RESIZE_THRESHOLD),
                        type: 'prompt',
                        textFieldProperties: numberTextFieldProperties
                    },
                    {
                        id: 'setting',
                        key: 'documentNotDetectedMargin',
                        title: lc('document_not_detected_margin'),
                        description: lc('document_not_detected_margin_desc'),
                        rightValue: () => ApplicationSettings.getNumber('documentNotDetectedMargin', DOCUMENT_NOT_DETECTED_MARGIN),
                        type: 'prompt',
                        textFieldProperties: numberTextFieldProperties
                    },
                    {
                        id: 'setting',
                        key: SETTINGS_MAGNIFIER_SENSITIVITY,
                        min: 10,
                        max: 100,
                        step: 1,
                        formatter: (value) => (value / 100).toFixed(2),
                        transformValue: (value, item) => value / 100,
                        title: lc('magnifier_sensitivity'),
                        description: lc('magnifier_sensitivity_desc'),
                        type: 'slider',
                        rightValue: () => ApplicationSettings.getNumber(SETTINGS_MAGNIFIER_SENSITIVITY, MAGNIFIER_SENSITIVITY),
                        currentValue: () => ApplicationSettings.getNumber(SETTINGS_MAGNIFIER_SENSITIVITY, MAGNIFIER_SENSITIVITY) * 100
                    }
                ];
            case 'autoscan':
                return [
                    {
                        type: 'switch',
                        id: 'autoScan',
                        title: lc('auto_scan'),
                        value: ApplicationSettings.getBoolean('autoScan', AUTO_SCAN_ENABLED)
                    },
                    {
                        id: 'setting',
                        key: 'autoScan_distanceThreshold',
                        title: lc('auto_scan_distance_threshold'),
                        description: lc('auto_scan_distance_threshold_desc'),
                        rightValue: () => ApplicationSettings.getNumber('autoScan_distanceThreshold', AUTO_SCAN_DISTANCETHRESHOLD),
                        type: 'prompt',
                        textFieldProperties: numberTextFieldProperties
                    },
                    {
                        id: 'setting',
                        key: 'autoScan_autoScanDuration',
                        title: lc('auto_scan_duration'),
                        description: lc('auto_scan_duration_desc'),
                        rightValue: () => ApplicationSettings.getNumber('autoScan_autoScanDuration', AUTO_SCAN_DURATION),
                        type: 'prompt',
                        textFieldProperties: numberTextFieldProperties
                    },
                    {
                        id: 'setting',
                        key: 'autoScan_preAutoScanDelay',
                        title: lc('auto_scan_delay'),
                        description: lc('auto_scan_delay_desc'),
                        rightValue: () => ApplicationSettings.getNumber('autoScan_preAutoScanDelay', AUTO_SCAN_DELAY),
                        type: 'prompt',
                        textFieldProperties: numberTextFieldProperties
                    }
                ];
            case 'images':
                return [
                    {
                        id: 'setting',
                        key: SETTINGS_IMAGE_EXPORT_FORMAT,
                        title: lc('image_format'),
                        currentValue: () => ApplicationSettings.getString(SETTINGS_IMAGE_EXPORT_FORMAT, IMG_FORMAT),
                        description: lc('image_format_desc'),
                        rightValue: () => ApplicationSettings.getString(SETTINGS_IMAGE_EXPORT_FORMAT, IMG_FORMAT).toUpperCase(),
                        valueType: 'string',
                        values: [
                            { value: 'jpg', title: 'JPEG' },
                            { value: 'png', title: 'PNG' }
                        ]
                    },
                    {
                        id: 'setting',
                        key: SETTINGS_IMAGE_EXPORT_QUALITY,
                        min: 10,
                        max: 100,
                        step: 1,
                        formatter: (value) => value.toFixed(),
                        title: lc('image_quality'),
                        description: lc('image_quality_desc'),
                        type: 'slider',
                        rightValue: () => ApplicationSettings.getNumber(SETTINGS_IMAGE_EXPORT_QUALITY, IMG_COMPRESS)
                    }
                ];
            case 'pdf_import': {
                return [
                    {
                        id: 'setting',
                        key: SETTINGS_IMPORT_PDF_IMAGES,
                        title: lc('import_pdf_images'),
                        description: lc('import_pdf_images_desc'),
                        currentValue: () => ApplicationSettings.getString(SETTINGS_IMPORT_PDF_IMAGES, PDF_IMPORT_IMAGES),
                        rightValue: () => {
                            switch (ApplicationSettings.getString(SETTINGS_IMPORT_PDF_IMAGES, PDF_IMPORT_IMAGES)) {
                                case PDFImportImages.ask:
                                    return PDFImportImages.ask.toUpperCase();
                                case PDFImportImages.never:
                                    return lu('page');
                                case PDFImportImages.always:
                                    return lu('image');
                            }
                        },
                        valueType: 'string',
                        autoSizeListItem: true,
                        fontWeight: 'normal',
                        height: 250,
                        values: [
                            { value: PDFImportImages.ask, title: lc('ask_everytime') },
                            { value: PDFImportImages.never, title: lc('pdf_one_image_per_page') },
                            { value: PDFImportImages.always, title: lc('pdf_one_image_per_pdf_image') }
                        ]
                    }
                ];
            }
            case 'pdf_export':
                return (
                    (__ANDROID__
                        ? [
                              {
                                  type: 'rightIcon',
                                  key: 'pdf_export_directory',
                                  title: lc('export_folder'),
                                  defaultValue: DEFAULT_EXPORT_DIRECTORY || lc('please_choose_export_folder'),
                                  description: (item) => ApplicationSettings.getString(item.key, item.defaultValue),
                                  rightBtnIcon: 'mdi-restore',
                                  onTap: async (item) => {
                                      DEV_LOG && console.log('onTap', item);
                                      const result = await pickFolder({
                                          multipleSelection: false,
                                          forceSAF: true,
                                          permissions: { write: true, persistable: true, read: true }
                                      });
                                      if (result.folders.length) {
                                          const exportDirectory = result.folders[0];
                                          ApplicationSettings.setString(item.key, exportDirectory);
                                          return true;
                                      }
                                  },
                                  onRightIconTap: (item) => {
                                      DEV_LOG && console.log('onRightIconTap', item);
                                      ApplicationSettings.remove(item.key);
                                      return true;
                                  }
                              },
                              {
                                  type: 'sectionheader',
                                  title: lc('page_layout')
                              }
                          ]
                        : []) as any[]
                )
                    .concat([
                        {
                            id: 'store_setting',
                            storeKey: 'default_export_options',
                            storeDefault: DEFAULT_PDF_OPTIONS_STRING,
                            key: 'password',
                            valueType: 'string',
                            title: lc('optional_pdf_password'),
                            type: 'prompt',
                            default: () => getStoreSetting('default_export_options', DEFAULT_PDF_OPTIONS_STRING)['password'],
                            textFieldProperties: {
                                secure: true
                            }
                        }
                    ])
                    .concat(
                        Object.keys(PDF_OPTIONS).map((option) => ({
                            id: 'store_setting',
                            storeKey: 'default_export_options',
                            storeDefault: DEFAULT_PDF_OPTIONS_STRING,
                            key: option,
                            valueType: typeof getStoreSetting('default_export_options', DEFAULT_PDF_OPTIONS_STRING)[option],
                            title: lc(option),
                            rightValue: () => PDF_OPTIONS[option][getStoreSetting('default_export_options', DEFAULT_PDF_OPTIONS_STRING)[option]].name,
                            currentValue: () => getStoreSetting('default_export_options', DEFAULT_PDF_OPTIONS_STRING)[option],
                            values: Object.keys(PDF_OPTIONS[option]).map((k) => ({ ...PDF_OPTIONS[option][k], value: k }))
                        })) as any
                    )
                    .concat([
                        {
                            id: 'store_setting',
                            storeKey: 'default_export_options',
                            storeDefault: DEFAULT_PDF_OPTIONS_STRING,
                            key: 'page_padding',
                            title: lc('page_padding'),
                            type: 'prompt',
                            textFieldProperties: numberTextFieldProperties,
                            rightValue: () => getStoreSetting('default_export_options', DEFAULT_PDF_OPTIONS_STRING)['page_padding']
                        },
                        {
                            type: 'sectionheader',
                            title: lc('image_settings')
                        },
                        {
                            id: 'store_setting',
                            storeKey: 'default_export_options',
                            storeDefault: DEFAULT_PDF_OPTIONS_STRING,
                            key: 'imageSizeThreshold',
                            title: lc('image_size_threshold'),
                            description: lc('image_size_threshold_desc'),
                            type: 'prompt',
                            textFieldProperties: numberTextFieldProperties,
                            rightValue: () => getStoreSetting('default_export_options', DEFAULT_PDF_OPTIONS_STRING)['imageSizeThreshold']
                        },
                        {
                            id: 'store_setting',
                            storeKey: 'default_export_options',
                            storeDefault: DEFAULT_PDF_OPTIONS_STRING,
                            key: 'imageLoadScale',
                            min: 0.5,
                            max: 10,
                            step: 0.5,
                            formatter: (value) => value.toFixed(1),
                            title: lc('image_load_scale'),
                            description: lc('image_load_scale_desc'),
                            type: 'slider',
                            rightValue: () => getStoreSetting('default_export_options', DEFAULT_PDF_OPTIONS_STRING)['imageLoadScale']
                        },
                        {
                            id: 'store_setting',
                            storeKey: 'default_export_options',
                            storeDefault: DEFAULT_PDF_OPTIONS_STRING,
                            key: 'jpegQuality',
                            min: 0,
                            max: 100,
                            step: 1,
                            formatter: (value) => value.toFixed(),
                            title: lc('jpeg_quality'),
                            description: lc('pdf_export_jpeg_quality'),
                            type: 'slider',
                            rightValue: () => getStoreSetting('default_export_options', DEFAULT_PDF_OPTIONS_STRING)['jpegQuality']
                        }
                    ] as any);
            case 'operations':
                return [
                    {
                        id: 'setting',
                        key: SETTINGS_TRANSFORM_BATCH_SIZE,
                        min: 1,
                        max: 10,
                        step: 1,
                        formatter: (value) => value.toFixed(),
                        title: lc('transformer_batch_size'),
                        description: lc('transformer_batch_size_desc'),
                        type: 'slider',
                        rightValue: () => ApplicationSettings.getNumber(SETTINGS_TRANSFORM_BATCH_SIZE, TRANSFORM_BATCH_SIZE)
                    },
                    {
                        id: 'image_processing_settings',
                        title: lc('image_processing_settings'),
                        description: lc('image_processing_settings_desc')
                    }
                ];
            case 'folders':
                return [
                    {
                        type: 'switch',
                        id: SETTINGS_DRAW_FOLDERS_BACKGROUND,
                        title: lc('folder_color_as_background'),
                        description: lc('folder_color_as_background_desc'),
                        value: ApplicationSettings.getBoolean(SETTINGS_DRAW_FOLDERS_BACKGROUND, DEFAULT_DRAW_FOLDERS_BACKGROUND)
                    }
                ];
            case 'sync':
                return [
                    {
                        type: 'switch',
                        id: SETTINGS_SYNC_ON_START,
                        title: lc('sync_on_start'),
                        description: lc('sync_on_start_desc'),
                        value: ApplicationSettings.getBoolean(SETTINGS_SYNC_ON_START, false)
                    },
                    {
                        id: 'data_sync',
                        title: lc('data_sync'),
                        description: lc('data_sync_desc')
                    },
                    {
                        id: 'image_sync',
                        title: lc('image_sync'),
                        description: lc('image_sync_desc')
                    },
                    {
                        id: 'pdf_sync',
                        title: lc('pdf_sync'),
                        description: lc('pdf_sync_desc')
                    }
                ];
            case 'debugging':
                return PLAY_STORE_BUILD
                    ? [
                          {
                              type: 'switch',
                              id: SETTINGS_ENABLE_CRASH_REPORT,
                              title: lc('crash_report'),
                              description: lc('crash_report_desc'),
                              value: ApplicationSettings.getBoolean(SETTINGS_ENABLE_CRASH_REPORT, PLAY_STORE_BUILD)
                          }
                      ]
                    : [];
            case 'document_naming':
                return [
                    {
                        id: 'setting',
                        key: SETTINGS_DOCUMENT_NAME_FORMAT,
                        useHTML: true,
                        title: lc('document_name_date_format'),
                        description: lc('document_name_date_format_desc'),
                        full_description: lc('document_name_date_format_fulldesc', ...getNameFormatHTMLArgs()),
                        onLinkTap: ({ link }) => {
                            openLink(link);
                        },
                        valueType: 'string',
                        textFieldProperties: {
                            autocapitalizationType: 'none',
                            autocorrect: false
                        } as TextFieldProperties,
                        rightValue: () => ApplicationSettings.getString(SETTINGS_DOCUMENT_NAME_FORMAT, DOCUMENT_NAME_FORMAT),
                        type: 'prompt'
                    },
                    {
                        type: 'sectionheader',
                        title: lc('export_settings')
                    },
                    {
                        type: 'switch',
                        id: SETTINGS_FILE_NAME_USE_DOCUMENT_NAME,
                        title: lc('filename_use_document_name'),
                        value: ApplicationSettings.getBoolean(SETTINGS_FILE_NAME_USE_DOCUMENT_NAME, FILENAME_USE_DOCUMENT_NAME)
                    },
                    {
                        id: 'setting',
                        key: SETTINGS_FILE_NAME_FORMAT,
                        useHTML: true,
                        title: lc('filename_date_format'),
                        description: lc('filename_date_format_desc'),
                        full_description: lc('filename_date_format_fulldesc', ...getNameFormatHTMLArgs()),
                        onLinkTap: ({ link }) => {
                            openLink(link);
                        },
                        valueType: 'string',
                        textFieldProperties: {
                            autocapitalizationType: 'none',
                            autocorrect: false
                        } as TextFieldProperties,
                        rightValue: () => ApplicationSettings.getString(SETTINGS_FILE_NAME_FORMAT, FILENAME_DATE_FORMAT),
                        type: 'prompt'
                    }
                ];
            case 'data':
                return dataSettingsAvailable
                    ? [
                          {
                              id: 'setting',
                              key: 'storage_location',
                              title: lc('storage_location'),
                              currentValue: () => (documentsService.rootDataFolder === knownFolders.externalDocuments().path ? 'sdcard' : 'internal'),
                              description: () => (documentsService.rootDataFolder === knownFolders.externalDocuments().path ? lc('sdcard') : lc('internal_storage')),
                              values: [
                                  { value: 'internal', title: lc('internal_storage') },
                                  { value: 'sdcard', title: lc('sdcard') }
                              ],
                              onResult: async (data) => {
                                  try {
                                      const current = documentsService.rootDataFolder === knownFolders.externalDocuments().path ? 'sdcard' : 'internal';
                                      if (current !== data) {
                                          const confirmed = await confirm({
                                              title: lc('move_data'),
                                              message: lc('move_data_desc'),
                                              okButtonText: lc('ok'),
                                              cancelButtonText: lc('cancel')
                                          });
                                          if (confirmed) {
                                              const srcFolder = documentsService.rootDataFolder;
                                              let dstFolder: string;
                                              if (data === 'sdcard') {
                                                  dstFolder = knownFolders.externalDocuments().path;
                                              } else {
                                                  dstFolder = knownFolders.documents().path;
                                              }
                                              DEV_LOG && console.log('confirmed move data to', srcFolder, dstFolder);
                                              showLoading(lc('moving_files'));
                                              const srcDbPath = path.join(srcFolder, DocumentsService.DB_NAME);
                                              await File.fromPath(srcDbPath).copy(path.join(dstFolder, DocumentsService.DB_NAME));
                                              await copyFolderContent(path.join(srcFolder, 'data'), path.join(dstFolder, 'data'));
                                              ApplicationSettings.setString(SETTINGS_ROOT_DATA_FOLDER, dstFolder);
                                              await File.fromPath(srcDbPath).remove();
                                              await removeFolderContent(path.join(srcFolder, 'data'));
                                              await alert({
                                                  cancelable: false,
                                                  message: lc('restart_app'),
                                                  okButtonText: lc('restart')
                                              });
                                              restartApp();
                                          }
                                      }
                                  } catch (error) {
                                      showError(error);
                                  } finally {
                                      hideLoading();
                                  }
                              }
                          }
                      ]
                    : ([] as any);
            case 'appearance':
                return [
                    {
                        id: 'theme',
                        description: () => getThemeDisplayName(),
                        title: lc('theme.title')
                    },
                    {
                        id: 'color_theme',
                        description: () => getColorThemeDisplayName(),
                        title: lc('color_theme.title')
                    },
                    {
                        type: 'switch',
                        id: 'auto_black',
                        title: lc('auto_black'),
                        value: ApplicationSettings.getBoolean('auto_black', false)
                    },
                    {
                        id: 'setting',
                        key: SETTINGS_NB_COLUMNS,
                        min: 1,
                        max: 7,
                        step: 1,
                        formatter: (value) => value.toFixed(),
                        title: lc('nb_columns'),
                        description: lc('nb_columns_desc'),
                        type: 'slider',
                        rightValue: () => ApplicationSettings.getNumber(SETTINGS_NB_COLUMNS, DEFAULT_NB_COLUMNS)
                    },
                    {
                        id: 'setting',
                        key: SETTINGS_NB_COLUMNS_LANDSCAPE,
                        min: 1,
                        max: 7,
                        step: 1,
                        formatter: (value) => value.toFixed(),
                        title: lc('nb_columns_landscape'),
                        description: lc('nb_columns_landscape_desc'),
                        type: 'slider',
                        rightValue: () => ApplicationSettings.getNumber(SETTINGS_NB_COLUMNS_LANDSCAPE, DEFAULT_NB_COLUMNS_LANDSCAPE)
                    }
                ]
                    .concat(
                        CARD_APP
                            ? [
                                  {
                                      type: 'switch',
                                      id: SETTINGS_CARD_ALWAYS_SHOW_NAME,
                                      title: lc('always_show_card_name'),
                                      value: ApplicationSettings.getBoolean(SETTINGS_CARD_ALWAYS_SHOW_NAME, DEFAULT_CARD_ALWAYS_SHOW_NAME)
                                  }
                              ]
                            : [
                                  {
                                      id: 'setting',
                                      key: SETTINGS_NB_COLUMNS_VIEW,
                                      min: 1,
                                      max: 7,
                                      step: 1,
                                      formatter: (value) => value.toFixed(),
                                      title: lc('nb_columns_view'),
                                      description: lc('nb_columns_view_desc'),
                                      type: 'slider',
                                      rightValue: () => ApplicationSettings.getNumber(SETTINGS_NB_COLUMNS_VIEW, DEFAULT_NB_COLUMNS_VIEW)
                                  },
                                  {
                                      id: 'setting',
                                      key: SETTINGS_NB_COLUMNS_VIEW_LANDSCAPE,
                                      min: 1,
                                      max: 7,
                                      step: 1,
                                      formatter: (value) => value.toFixed(),
                                      title: lc('nb_columns_view_landscape'),
                                      description: lc('nb_columns_view_landscape_desc'),
                                      type: 'slider',
                                      rightValue: () => ApplicationSettings.getNumber(SETTINGS_NB_COLUMNS_VIEW_LANDSCAPE, DEFAULT_NB_COLUMNS_VIEW_LANDSCAPE)
                                  }
                              ]
                    )
                    .concat(
                        CARD_APP
                            ? [
                                  {
                                      type: 'switch',
                                      id: SETTINGS_FORCE_WHITE_BACKGROUND_QRCODE,
                                      title: lc('force_white_background_qrcode'),
                                      description: lc('force_white_background_qrcode_desc'),
                                      value: ApplicationSettings.getBoolean(SETTINGS_FORCE_WHITE_BACKGROUND_QRCODE, DEFAULT_FORCE_WHITE_BACKGROUND_QRCODE)
                                  }
                              ]
                            : ([] as any)
                    )
                    .concat(
                        __ANDROID__
                            ? [
                                  {
                                      type: 'switch',
                                      id: 'quicktoggle',
                                      title: lc('show_quicksettings_tile'),
                                      description: lc('show_quicktoggle_desc'),
                                      value: ApplicationSettings.getBoolean(SETTINGS_QUICK_TOGGLE_ENABLED, false)
                                  }
                              ]
                            : ([] as any)
                    );
            default:
                break;
        }
    }
    function refresh() {
        const newItems: any[] =
            options ||
            (
                [
                    {
                        type: 'header',
                        title: lc('donate')
                    },
                    {
                        type: 'sectionheader',
                        title: lc('general')
                    },
                    {
                        id: 'language',
                        icon: 'mdi-translate',
                        description: () => getLocaleDisplayName(),
                        title: lc('language')
                    },
                    {
                        id: 'sub_settings',
                        options: () => getSubSettings('appearance'),
                        icon: 'mdi-cards-outline',
                        description: () => lc('appearance_settings'),
                        title: lc('appearance')
                    }
                ] as any
            )
                .concat(
                    __ANDROID__ || securityService.biometricsAvailable
                        ? [
                              {
                                  id: 'sub_settings',
                                  icon: 'mdi-fingerprint',
                                  title: lc('security'),
                                  description: lc('security_settings'),
                                  options: () => getSubSettings('security')
                              }
                          ]
                        : []
                )
                .concat(
                    dataSettingsAvailable
                        ? [
                              {
                                  id: 'sub_settings',
                                  icon: 'mdi-database-outline',
                                  title: lc('data'),
                                  description: lc('data_settings'),
                                  options: () => getSubSettings('data')
                              }
                          ]
                        : []
                )

                .concat(
                    $hasCamera
                        ? [
                              {
                                  id: 'sub_settings',
                                  title: lc('camera'),
                                  description: lc('camera_settings'),
                                  icon: 'mdi-camera',
                                  options: () => getSubSettings('camera')
                              }
                          ]
                        : []
                )
                .concat([
                    {
                        id: 'sub_settings',
                        title: lc('document_detection'),
                        description: lc('document_detection_settings'),
                        icon: 'mdi-text-box-search',
                        options: () => getSubSettings('document_detection')
                    }
                ])
                .concat([
                    {
                        id: 'sub_settings',
                        title: lc('ocr'),
                        description: lc('ocr_settings'),
                        icon: 'mdi-ocr',
                        options: () => getSubSettings('ocr')
                    }
                ])
                .concat(
                    $hasCamera
                        ? [
                              {
                                  id: 'sub_settings',
                                  icon: 'mdi-file-star-four-points',
                                  title: lc('autoscan'),
                                  description: lc('autoscan_settings'),
                                  options: () => getSubSettings('autoscan')
                              }
                          ]
                        : []
                )
                .concat([
                    {
                        id: 'sub_settings',
                        icon: 'mdi-file-document-edit',
                        title: lc('document_naming_template'),
                        description: lc('document_naming_settings'),
                        options: () => getSubSettings('document_naming')
                    },
                    {
                        id: 'sub_settings',
                        icon: 'mdi-file-pdf-box',
                        title: lc('pdf_import'),
                        description: lc('pdf_import_settings'),
                        options: () => getSubSettings('pdf_import')
                    },
                    {
                        id: 'sub_settings',
                        icon: 'mdi-file-pdf-box',
                        title: lc('pdf_export'),
                        description: lc('pdf_export_settings'),
                        options: () => getSubSettings('pdf_export')
                    },
                    {
                        id: 'sub_settings',
                        icon: 'mdi-image',
                        title: lc('image_settings'),
                        description: lc('image_settings_desc'),
                        options: () => getSubSettings('images')
                    },
                    {
                        id: 'sub_settings',
                        icon: 'mdi-function',
                        title: lc('operations'),
                        description: lc('operations_settings_desc'),
                        options: () => getSubSettings('operations')
                    },
                    {
                        id: 'sub_settings',
                        icon: 'mdi-folder-multiple',
                        title: lc('folders'),
                        description: lc('folders_settings_desc'),
                        options: () => getSubSettings('folders')
                    },
                    {
                        id: 'sub_settings',
                        icon: 'mdi-sync',
                        title: lc('sync'),
                        description: lc('sync_settings_desc'),
                        options: () => getSubSettings('sync')
                    }
                ])
                .concat(
                    PLAY_STORE_BUILD
                        ? [
                              {
                                  id: 'sub_settings',
                                  icon: 'mdi-bug-outline',
                                  title: lc('debugging'),
                                  description: lc('debugging_settings_desc'),
                                  options: () => getSubSettings('debugging')
                              }
                          ]
                        : []
                )
                .concat([
                    {
                        id: 'third_party',
                        // rightBtnIcon: 'mdi-chevron-right',
                        title: lc('third_parties'),
                        description: lc('list_used_third_parties')
                    }
                ])
                .concat(
                    PLAY_STORE_BUILD
                        ? [
                              //   {
                              //       id: 'share',
                              //       rightBtnIcon: 'mdi-chevron-right',
                              //       title: lc('share_application')
                              //   },
                              {
                                  type: 'rightIcon',
                                  id: 'review',
                                  rightBtnIcon: 'mdi-chevron-right',
                                  title: lc('review_application')
                              }
                          ]
                        : []
                )
                .concat([
                    {
                        type: 'sectionheader',
                        title: lc('backup_restore')
                    },
                    {
                        id: 'create_backup',
                        title: lc('create_backup'),
                        description: lc('create_backup_desc')
                    },
                    {
                        id: 'restore_backup',
                        title: lc('restore_backup'),
                        description: lc('restore_backup_desc')
                    },
                    {
                        type: 'sectionheader',
                        title: lc('settings')
                    },
                    {
                        id: 'export_settings',
                        title: lc('export_settings'),
                        description: lc('export_settings_desc')
                        // rightBtnIcon: 'mdi-chevron-right'
                    },
                    {
                        id: 'import_settings',
                        title: lc('import_settings'),
                        description: lc('import_settings_desc')
                        // rightBtnIcon: 'mdi-chevron-right'
                    }
                ]);

        items = new ObservableArray(newItems);
    }
    refresh();

    async function onLongPress(id, event) {
        try {
            switch (id) {
                case 'version':
                    if (SENTRY_ENABLED) {
                        throw new Error('test error');
                    }
            }
        } catch (error) {
            showError(error);
        }
    }
    function updateItem(item, key = 'key') {
        const index = items.findIndex((it) => it[key] === item[key]);
        if (index !== -1) {
            items.setItem(index, item);
        }
    }
    let checkboxTapTimer;
    function clearCheckboxTimer() {
        if (checkboxTapTimer) {
            clearTimeout(checkboxTapTimer);
            checkboxTapTimer = null;
        }
    }
    async function onRightIconTap(item, event) {
        try {
            const needsUpdate = await item.onRightIconTap?.(item, event);
            if (needsUpdate) {
                updateItem(item);
            }
        } catch (error) {
            showError(error);
        }
    }
    async function onTap(item, event) {
        try {
            if (item.type === 'checkbox' || item.type === 'switch') {
                // we dont want duplicate events so let s timeout and see if we clicking diretly on the checkbox
                const checkboxView: CheckBox = ((event.object as View).parent as View).getViewById('checkbox');
                if (checkboxView.isEnabled) {
                    clearCheckboxTimer();
                    checkboxTapTimer = setTimeout(() => {
                        checkboxView.checked = !checkboxView.checked;
                    }, 10);
                }
                return;
            }
            switch (item.id) {
                case 'sub_settings': {
                    showSettings({
                        title: item.title,
                        id: `settings[${item.id}]`,
                        options: item.options(),
                        actionBarButtons: item.actionBarButtons?.() || []
                    });

                    break;
                }
                case 'create_backup':
                    try {
                        showLoading(lc('creating_backup'));
                        const backupPath = await backupWorkerService.createBackup();
                        await hideLoading();
                        if (backupPath) {
                            showSnack({ message: lc('backup_created') });
                            DEV_LOG && console.log('create_backup done', backupPath);
                        }
                    } catch (error) {
                        DEV_LOG && console.log('error while creating backup', error);
                        await hideLoading();
                        showError(error);
                    }
                    break;
                case 'restore_backup':
                    try {
                        const result = await openFilePicker({
                            extensions: ['zip'],
                            multipleSelection: false,
                            pickerMode: 0,
                            forceSAF: true
                        });

                        const zipPath = result.files[0];
                        DEV_LOG && console.log('restore_backup from file picker', zipPath, zipPath && File.exists(zipPath));

                        if (zipPath && File.exists(zipPath)) {
                            showLoading(lc('restoring_backup'));
                            await backupWorkerService.restoreBackup(zipPath);
                            await hideLoading();
                            showSnack({ message: lc('backup_restored') });

                            // if (__ANDROID__) {
                            //     const result = await confirm({
                            //         message: lc('restart_app'),
                            //         okButtonText: lc('restart'),
                            //         cancelButtonText: lc('later')
                            //     });
                            //     if (result) {
                            //         restartApp();
                            //     }
                            // } else {
                            //     showSnack({ message: lc('please_restart_app') });
                            // }
                        }
                    } catch (error) {
                        await hideLoading();
                        showError(error);
                    }
                    break;
                case 'export_settings':
                    // if (__ANDROID__ && SDK_VERSION < 29) {
                    //     const permRes = await request('storage');
                    //     if (!isPermResultAuthorized(permRes)) {
                    //         throw new Error(lc('missing_storage_perm_settings'));
                    //     }
                    // }
                    const jsonStr = ApplicationSettings.getAllJSON();
                    if (jsonStr) {
                        const result = await saveFile({
                            name: `${__APP_ID__}_settings_${dayjs().format('YYYY-MM-DD')}.json`,
                            data: jsonStr,
                            forceSAF: true
                        });
                        DEV_LOG && console.log('export_settings done', result, jsonStr);
                    }
                    break;
                case 'import_settings':
                    const result = await openFilePicker({
                        extensions: ['json'],

                        multipleSelection: false,
                        pickerMode: 0,
                        forceSAF: true
                    });
                    const filePath = result.files[0];
                    DEV_LOG && console.log('import_settings from file picker', filePath, filePath && File.exists(filePath));
                    if (filePath && File.exists(filePath)) {
                        showLoading();
                        await restoreSettings(filePath);
                        await hideLoading();
                        if (__ANDROID__) {
                            const result = await confirm({
                                message: lc('restart_app'),
                                okButtonText: lc('restart'),
                                cancelButtonText: lc('later')
                            });
                            if (result) {
                                restartApp();
                            }
                        } else {
                            showSnack({ message: lc('please_restart_app') });
                        }
                    }
                    break;
                case 'github':
                    openLink(GIT_URL);
                    break;
                case 'language':
                    await selectLanguage();
                    break;
                case 'theme':
                    await selectTheme();
                    break;
                case 'color_theme':
                    await selectColorTheme();
                    break;
                case 'share':
                    await share({
                        message: GIT_URL
                    });
                    break;
                case 'review':
                    openLink(STORE_REVIEW_LINK);
                    break;
                case 'sponsor':
                    switch (item.type) {
                        case 'librepay':
                            openLink('https://liberapay.com/farfromrefuge');
                            break;
                        case 'patreon':
                            openLink('https://patreon.com/farfromrefuge');
                            break;

                        default:
                            if (__IOS__ && PLAY_STORE_BUILD) {
                                presentInAppSponsorBottomsheet();
                            } else {
                                // Apple wants us to use in-app purchase for donations => taking 30% ...
                                // so lets just open github and ask for love...
                                openLink(__IOS__ ? GIT_URL : SPONSOR_URL);
                            }
                            break;
                    }
                    break;
                case 'third_party':
                    const ThirdPartySoftwareBottomSheet = (await import('~/components/settings/ThirdPartySoftwareBottomSheet.svelte')).default;
                    showBottomSheet({
                        skipCollapsedState: true,
                        view: ThirdPartySoftwareBottomSheet
                    });
                    break;
                case 'feedback': {
                    if (SENTRY_ENABLED) {
                        const view = createView(ScrollView);
                        const stackLayout = createView(StackLayout, {
                            padding: 10
                        });
                        const commentsTF = createView(TextView, {
                            hint: lc('comments'),
                            variant: 'outline',
                            height: 150,
                            returnKeyType: 'done'
                        });
                        const emailTF = createView(TextField, {
                            hint: lc('email'),
                            variant,
                            autocapitalizationType: 'none',
                            autocorrect: false,
                            keyboardType: 'email',
                            returnKeyType: 'next'
                        });
                        const nameTF = createView(TextField, {
                            hint: lc('name'),
                            variant,
                            returnKeyType: 'next'
                        });
                        stackLayout.addChild(nameTF);
                        stackLayout.addChild(emailTF);
                        stackLayout.addChild(commentsTF);
                        view.content = stackLayout;
                        const result = await confirm({
                            title: lc('send_feedback'),
                            okButtonText: l('send'),
                            cancelButtonText: l('cancel'),
                            view
                        });
                        if (result && nameTF.text?.length && commentsTF.text?.length) {
                            const eventId = Sentry.captureMessage('User Feedback');

                            Sentry.captureUserFeedback({
                                event_id: eventId,
                                name: nameTF.text,
                                email: emailTF.text,
                                comments: commentsTF.text
                            });
                            Sentry.flush();
                            showSnack({ message: l('feedback_sent') });
                        }
                    } else {
                        openLink(GIT_URL + '/issues');
                    }
                    break;
                }
                case 'image_processing_settings': {
                    const view = (await import('~/components/widgets/ImageProcessingSettingsBottomSheet.svelte')).default;
                    await showBottomSheet({
                        view,
                        skipCollapsedState: true
                    });
                    break;
                }
                // case 'data_location': {
                //     await requestManagePermission();
                //     const result = await pickFolder({
                //         permissions: {
                //             read: true,
                //             write: true,
                //             recursive: true,
                //             persistable: true
                //         }
                //     });
                //     const resultPath = result.folders[0];
                //     if (resultPath) {
                //         const dstFolder = getRealPath(resultPath);
                //         const srcFolder = documentsService.rootDataFolder;
                //         DEV_LOG &&
                //             console.log(
                //                 'move data location',
                //                 JSON.stringify({
                //                     srcFolder,
                //                     resultPath,
                //                     dstFolder
                //                 })
                //             );
                //         let confirmed = true;
                //         if (srcFolder !== getRealPath(resultPath, true)) {
                //             confirmed = await confirm({
                //                 title: lc('move_data'),
                //                 message: lc('move_data_desc'),
                //                 okButtonText: lc('ok'),
                //                 cancelButtonText: lc('cancel')
                //             });
                //         }
                //         if (confirmed) {
                //             DEV_LOG && console.log('confirmed move data to', srcFolder, dstFolder);
                //             showLoading(lc('moving_files'));
                //             await copyFolderContent(srcFolder, dstFolder);
                //             await removeFolderContent(srcFolder);
                //             DEV_LOG && console.log('copyFolderContent done');
                //         }
                //         ApplicationSettings.setString(SETTINGS_ROOT_DATA_FOLDER, dstFolder);
                //         // documentsService.dataFolder = Folder.fromPath(dstFolder);
                //         await alert({
                //             cancelable: false,
                //             message: lc('restart_app'),
                //             okButtonText: lc('restart')
                //         });
                //         restartApp();
                //         // item.text = dstFolder;
                //         item.description = dstFolder;
                //         updateItem(item);
                //     }
                //     break;
                // }
                case 'store_setting':
                case 'setting': {
                    if (item.type === 'prompt') {
                        const defaultValue = typeof item.rightValue === 'function' ? item.rightValue() : typeof item.default === 'function' ? item.default() : item.default;
                        const result = await prompt({
                            title: getTitle(item),
                            message: item.useHTML ? item.description : item.full_description || item.description,
                            okButtonText: l('save'),
                            cancelButtonText: l('cancel'),
                            autoFocus: true,
                            textFieldProperties: item.textFieldProperties,
                            defaultText: (defaultValue ?? '') + '',
                            view: item.useHTML
                                ? createView(
                                      Label,
                                      {
                                          padding: '10 20 0 20',
                                          textWrap: true,
                                          color: colorOnSurfaceVariant as any,
                                          html: item.full_description || item.description
                                      },
                                      item.onLinkTap
                                          ? {
                                                linkTap: item.onLinkTap
                                            }
                                          : undefined
                                  )
                                : undefined
                        });
                        Utils.dismissSoftInput();
                        DEV_LOG && console.log('result', result);
                        if (result && !!result.result) {
                            if (item.id === 'store_setting') {
                                const store = getStoreSetting(item.storeKey, item.storeDefault);
                                if (result.text.length > 0) {
                                    if (item.valueType === 'string') {
                                        store[item.key] = result.text;
                                    } else {
                                        store[item.key] = parseInt(result.text, 10);
                                    }
                                } else {
                                    delete store[item.key];
                                }
                                DEV_LOG && console.log('store_setting', store);
                                ApplicationSettings.setString(item.storeKey, JSON.stringify(store));
                            } else {
                                if (result.text.length > 0) {
                                    if (item.valueType === 'string') {
                                        ApplicationSettings.setString(item.key, result.text);
                                    } else {
                                        ApplicationSettings.setNumber(item.key, parseInt(result.text, 10));
                                    }
                                } else {
                                    ApplicationSettings.remove(item.key);
                                }
                            }
                            updateItem(item);
                        }
                    } else if (item.type === 'slider') {
                        await showSliderPopover({
                            anchor: event.object,
                            value: (item.currentValue || item.rightValue)?.(),
                            ...item,
                            onChange(value) {
                                if (item.transformValue) {
                                    value = item.transformValue(value, item);
                                } else {
                                    value = Math.round(value / item.step) * item.step;
                                }
                                if (item.id === 'store_setting') {
                                    const store = getStoreSetting(item.storeKey, item.storeDefault);
                                    if (item.valueType === 'string') {
                                        store[item.key] = value + '';
                                    } else {
                                        store[item.key] = value;
                                    }
                                    ApplicationSettings.setString(item.storeKey, JSON.stringify(store));
                                } else {
                                    if (item.valueType === 'string') {
                                        ApplicationSettings.setString(item.key, value + '');
                                    } else {
                                        ApplicationSettings.setNumber(item.key, value);
                                    }
                                }
                                updateItem(item);
                            }
                        });
                    } else {
                        let selectedIndex = -1;
                        const currentValue = (item.currentValue || item.rightValue)?.() ?? item.currentValue;
                        const options = item.values.map((k, index) => {
                            const selected = currentValue === k.value;
                            if (selected) {
                                selectedIndex = index;
                            }
                            return {
                                name: k.title || k.name,
                                data: k.value,
                                boxType: 'circle',
                                type: 'checkbox',
                                value: selected
                            };
                        });
                        const result = await showAlertOptionSelect(
                            {
                                height: Math.min(item.values.length * 56, ALERT_OPTION_MAX_HEIGHT),
                                rowHeight: item.autoSizeListItem ? undefined : 56,
                                ...item,
                                selectedIndex,
                                options
                            },
                            {
                                title: item.title,
                                message: item.full_description
                            }
                        );
                        if (result?.data !== undefined) {
                            if (item.onResult) {
                                item.onResult(result.data);
                            } else {
                                if (item.id === 'store_setting') {
                                    const store = getStoreSetting(item.storeKey, item.storeDefault);
                                    if (item.valueType === 'string') {
                                        store[item.key] = result.data;
                                    } else {
                                        store[item.key] = parseInt(result.data, 10);
                                    }
                                    ApplicationSettings.setString(item.storeKey, JSON.stringify(store));
                                } else {
                                    if (item.valueType === 'string') {
                                        ApplicationSettings.setString(item.key, result.data);
                                    } else {
                                        ApplicationSettings.setNumber(item.key, parseInt(result.data, 10));
                                    }
                                }
                                updateItem(item);
                            }
                        }
                    }

                    break;
                }
                case 'data_sync':
                case 'image_sync':
                case 'pdf_sync':
                    const component = (await import('~/components/settings/sync/SyncListSettings.svelte')).default;
                    navigate({
                        page: component,
                        props: {
                            title: item.title,
                            type: item.id.split('_')[0]
                        }
                    });
                    break;
                default: {
                    const needsUpdate = await item.onTap?.(item, event);
                    if (needsUpdate) {
                        updateItem(item);
                    }
                    break;
                }
            }
        } catch (err) {
            showError(err);
        } finally {
            hideLoading();
        }
    }
    onLanguageChanged(refresh);

    function selectTemplate(item, index, items) {
        if (item.type) {
            if (item.type === 'prompt' || item.type === 'slider') {
                return 'default';
            }
            return item.type;
        }
        if (item.icon) {
            return 'leftIcon';
        }
        return 'default';
    }

    let ignoreNextOnCheckBoxChange = false;
    async function onCheckBox(item, event) {
        if (ignoreNextOnCheckBoxChange || item.value === event.value) {
            return;
        }
        const value = event.value;
        item.value = value;
        clearCheckboxTimer();
        DEV_LOG && console.log('onCheckBox', item.id, value);
        try {
            ignoreNextOnCheckBoxChange = true;
            switch (item.id) {
                case 'biometric_lock':
                    if (value) {
                        try {
                            await securityService.enableBiometric();
                        } catch (error) {
                            console.error('enableBiometric error', error);
                            const checkboxView: CheckBox = event.object;
                            checkboxView.checked = item.value = false;
                            // showError(error);
                        }
                    } else {
                        try {
                            securityService.clear();
                            await securityService.disableBiometric();
                        } catch (error) {
                            const checkboxView: CheckBox = event.object;
                            checkboxView.checked = item.value = true;
                            // showError(error);
                        }
                    }
                    break;
                case 'biometric_auto_lock':
                    // if (value) {
                    //     if (securityService.biometricEnabled) {
                    //         const checkboxView: CheckBox = event.object;
                    //         checkboxView.checked = item.value = false;
                    //     } else {
                    securityService.autoLockEnabled = value;
                    // }
                    // } else {
                    //     securityService.clear();
                    //     securityService.disableBiometric();
                    // }
                    break;

                case 'quicktoggle': {
                    if (__ANDROID__) {
                        toggleQuickSetting(value);
                    }
                    break;
                }
                case SETTINGS_ENABLE_CRASH_REPORT: {
                    ApplicationSettings.setBoolean(item.key || item.id, value);
                    if (value) {
                        startSentry();
                    } else {
                        stopSentry();
                    }
                    break;
                }
                default:
                    ApplicationSettings.setBoolean(item.key || item.id, value);
                    break;
            }
        } catch (error) {
            showError(error);
        } finally {
            ignoreNextOnCheckBoxChange = false;
        }
    }

    function refreshCollectionView() {
        collectionView?.nativeView?.refresh();
    }

    function refreshCollectionViewVisibleItems() {
        collectionView?.nativeView?.refreshVisibleItems();
    }
    onFontScaleChanged(refreshCollectionViewVisibleItems);
    onThemeChanged(refreshCollectionView);
</script>

<page bind:this={page} {id} actionBarHidden={true}>
    <gridlayout class="pageContent" rows="auto,*">
        <collectionview bind:this={collectionView} accessibilityValue="settingsCV" itemTemplateSelector={selectTemplate} {items} row={1} android:paddingBottom={$windowInset.bottom}>
            <Template key="header" let:item>
                <gridlayout rows="auto,auto">
                    <gridlayout columns="*,auto,auto" margin="10 16 0 16">
                        <stacklayout
                            backgroundColor="#ea4bae"
                            borderRadius={10}
                            horizontalAlignment="center"
                            margin="10 16 0 16"
                            orientation="horizontal"
                            padding={10}
                            rippleColor="white"
                            verticalAlignment="center"
                            on:tap={(event) => onTap({ id: 'sponsor' }, event)}>
                            <label color="white" fontFamily={$fonts.mdi} fontSize={26} marginRight={10} text="mdi-heart" verticalAlignment="center" />
                            <label color="white" fontSize={12 * $fontScale} text={item.title} textWrap={true} verticalAlignment="center" />
                        </stacklayout>
                        {#if __ANDROID__}
                            <image
                                borderRadius={6}
                                col={1}
                                height={40}
                                margin="0 10 0 10"
                                rippleColor="white"
                                src="~/assets/images/librepay.png"
                                verticalAlignment="center"
                                on:tap={(event) => onTap({ id: 'sponsor', type: 'librepay' }, event)} />
                            <image
                                borderRadius={6}
                                col={2}
                                height={40}
                                rippleColor="#f96754"
                                src="~/assets/images/patreon.png"
                                verticalAlignment="center"
                                on:tap={(event) => onTap({ id: 'sponsor', type: 'patreon' }, event)} />
                        {/if}
                    </gridlayout>

                    <stacklayout horizontalAlignment="center" marginBottom={0} marginTop={20} row={1} verticalAlignment="center">
                        <image borderRadius={25} height={50} horizontalAlignment="center" src="res://icon" width={50} />
                        <label fontSize={13 * $fontScale} marginTop={4} text={version} on:longPress={(event) => onLongPress('version', event)} />
                    </stacklayout>
                </gridlayout>
            </Template>
            <Template key="sectionheader" let:item>
                <label class="sectionHeader" {...item.additionalProps || {}} text={item.title} />
            </Template>
            <Template key="switch" let:item>
                <ListItemAutoSize item={{ ...item, title: getTitle(item), subtitle: getDescription(item) }} on:tap={(event) => onTap(item, event)}>
                    <switch id="checkbox" checked={item.value} col={1} marginLeft={10} verticalAlignment="center" on:checkedChange={(e) => onCheckBox(item, e)} />
                </ListItemAutoSize>
            </Template>
            <Template key="checkbox" let:item>
                <ListItemAutoSize item={{ ...item, title: getTitle(item), subtitle: getDescription(item) }} on:tap={(event) => onTap(item, event)}>
                    <checkbox id="checkbox" checked={item.value} col={1} on:checkedChange={(e) => onCheckBox(item, e)} />
                </ListItemAutoSize>
            </Template>
            <Template key="rightIcon" let:item>
                <ListItemAutoSize item={{ ...item, title: getTitle(item), subtitle: getDescription(item) }} showBottomLine={false} on:tap={(event) => onTap(item, event)}>
                    <IconButton col={1} text={item.rightBtnIcon} on:tap={(event) => onRightIconTap(item, event)} />
                </ListItemAutoSize>
            </Template>
            <Template key="leftIcon" let:item>
                <ListItemAutoSize
                    columns="auto,*,auto"
                    item={{ ...item, title: getTitle(item), subtitle: getDescription(item) }}
                    mainCol={1}
                    showBottomLine={false}
                    on:tap={(event) => onTap(item, event)}>
                    <label col={0} color={colorOnBackground} fontFamily={$fonts.mdi} fontSize={24} padding="0 10 0 0" text={item.icon} verticalAlignment="center" />
                </ListItemAutoSize>
            </Template>
            <Template let:item>
                <ListItemAutoSize item={{ ...item, title: getTitle(item), subtitle: getDescription(item) }} showBottomLine={false} on:tap={(event) => onTap(item, event)}></ListItemAutoSize>
            </Template>

            <Template key="ocr_settings" let:item>
                <OCRSettingsBottomSheet onlySettings={true} showDownloadButton={true} />
            </Template>
        </collectionview>
        <CActionBar canGoBack title={title || $slc('settings.title')}>
            {#each actionBarButtons as button (button.id)}
                <mdbutton class="actionBarButton" text={button.icon} variant="text" on:tap={(event) => onTap({ id: button.id }, event)} />
            {/each}
        </CActionBar>
    </gridlayout>
</page>
