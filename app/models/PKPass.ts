import { Observable } from '@nativescript/core';
import { formatCurrency } from '@shared/helpers/format';
import dayjs from 'dayjs';

/**
 * PKPass model representing a wallet pass (Apple PKPass or ESpass)
 * Based on Apple's PassKit specification and ESpass open standard
 */

export enum PKPassType {
    PKPass = 'pkpass',
    ESpass = 'espass'
}

export enum PKPassStyle {
    BoardingPass = 'boardingPass',
    Coupon = 'coupon',
    EventTicket = 'eventTicket',
    Generic = 'generic',
    StoreCard = 'storeCard'
}

export enum PKPassTransitType {
    Air = 'PKTransitTypeAir',
    Boat = 'PKTransitTypeBoat',
    Bus = 'PKTransitTypeBus',
    Generic = 'PKTransitTypeGeneric',
    Train = 'PKTransitTypeTrain'
}

export enum PKBarcodeFormat {
    QR = 'PKBarcodeFormatQR',
    PDF417 = 'PKBarcodeFormatPDF417',
    Aztec = 'PKBarcodeFormatAztec',
    Code128 = 'PKBarcodeFormatCode128'
}

export interface PKPassBarcode {
    format: PKBarcodeFormat;
    message: string;
    messageEncoding: string;
    altText?: string;
}

export interface PKPassField {
    key: string;
    label?: string;
    value: string | number;
    textAlignment?: 'PKTextAlignmentLeft' | 'PKTextAlignmentCenter' | 'PKTextAlignmentRight' | 'PKTextAlignmentNatural';
    changeMessage?: string;
    dataDetectorTypes?: string[];
    // Date/time formatting
    dateStyle?: 'PKDateStyleNone' | 'PKDateStyleShort' | 'PKDateStyleMedium' | 'PKDateStyleLong' | 'PKDateStyleFull';
    timeStyle?: 'PKDateStyleNone' | 'PKDateStyleShort' | 'PKDateStyleMedium' | 'PKDateStyleLong' | 'PKDateStyleFull';
    isRelative?: boolean;
    ignoresTimeZone?: boolean;
    // Number formatting
    numberStyle?: 'PKNumberStyleDecimal' | 'PKNumberStylePercent' | 'PKNumberStyleScientific' | 'PKNumberStyleSpellOut';
    currencyCode?: string;
}

export interface PKPassLocation {
    latitude: number;
    longitude: number;
    altitude?: number;
    relevantText?: string;
}

export interface PKPassStructure {
    headerFields?: PKPassField[];
    primaryFields?: PKPassField[];
    secondaryFields?: PKPassField[];
    auxiliaryFields?: PKPassField[];
    backFields?: PKPassField[];
    transitType?: PKPassTransitType;
}

export interface PKPassData {
    // Standard keys
    formatVersion: number;
    passTypeIdentifier: string;
    serialNumber: string;
    teamIdentifier: string;
    organizationName: string;
    description: string;

    // Visual appearance
    logoText?: string;
    foregroundColor?: string;
    backgroundColor?: string;
    labelColor?: string;

    // Relevance
    locations?: PKPassLocation[];
    relevantDate?: string;
    expirationDate?: string;
    voided?: boolean;

    // Barcode
    barcode?: PKPassBarcode;
    barcodes?: PKPassBarcode[];

    // Pass structure (one of these will be present based on style)
    boardingPass?: PKPassStructure;
    coupon?: PKPassStructure;
    eventTicket?: PKPassStructure;
    generic?: PKPassStructure;
    storeCard?: PKPassStructure;

    // Web service
    webServiceURL?: string;
    authenticationToken?: string;

    // Localization support (added by parser)
    _allLocalizations?: { [languageCode: string]: { [key: string]: string } }; // All available localizations
}

export interface PKPassImages {
    icon?: string;
    icon2x?: string;
    icon3x?: string;
    logo?: string;
    logo2x?: string;
    logo3x?: string;
    background?: string;
    background2x?: string;
    background3x?: string;
    footer?: string;
    footer2x?: string;
    footer3x?: string;
    strip?: string;
    strip2x?: string;
    strip3x?: string;
    thumbnail?: string;
    thumbnail2x?: string;
    thumbnail3x?: string;
}

export class PKPass extends Observable {
    id: string;
    page_id: string; // Changed from document_id to page_id

    // Pass type identifier (pkpass or espass)
    passType: PKPassType = PKPassType.PKPass;

    // PKPass data
    passData: PKPassData;
    images: PKPassImages;

    // Storage paths
    // passJsonPath?: string;
    // imagesPath?: string;

    // Metadata
    createdDate: number;
    modifiedDate?: number;

    constructor(id: string, pageId: string, passType: PKPassType = PKPassType.PKPass) {
        super();
        this.id = id;
        this.page_id = pageId;
        this.passType = passType;
        this.createdDate = Date.now();
    }

    getPassStyle(): PKPassStyle {
        if (this.passData.boardingPass) return PKPassStyle.BoardingPass;
        if (this.passData.coupon) return PKPassStyle.Coupon;
        if (this.passData.eventTicket) return PKPassStyle.EventTicket;
        if (this.passData.storeCard) return PKPassStyle.StoreCard;
        return PKPassStyle.Generic;
    }

    getPassStructure(): PKPassStructure | undefined {
        const style = this.getPassStyle();
        switch (style) {
            case PKPassStyle.BoardingPass:
                return this.passData.boardingPass;
            case PKPassStyle.Coupon:
                return this.passData.coupon;
            case PKPassStyle.EventTicket:
                return this.passData.eventTicket;
            case PKPassStyle.StoreCard:
                return this.passData.storeCard;
            case PKPassStyle.Generic:
                return this.passData.generic;
        }
    }

    getPrimaryBarcode(): PKPassBarcode | undefined {
        // iOS 9+ uses barcodes array, older uses barcode
        if (this.passData.barcodes && this.passData.barcodes.length > 0) {
            return this.passData.barcodes[0];
        }
        return this.passData.barcode;
    }

    getAllBarcodes(): PKPassBarcode[] {
        const barcodes: PKPassBarcode[] = [];
        if (this.passData.barcodes) {
            barcodes.push(...this.passData.barcodes);
        } else if (this.passData.barcode) {
            barcodes.push(this.passData.barcode);
        }
        return barcodes;
    }

    isExpired(): boolean {
        if (!this.passData.expirationDate) {
            return false;
        }
        return new Date(this.passData.expirationDate) < new Date();
    }

    isVoided(): boolean {
        return this.passData.voided === true;
    }

    getLocalizedFieldLabel(field: any, currentLang: string): string {
        if (!field) return null;
        const label = field.label || field.key;
        return this.getLocalizedValue(label, currentLang);
    }
    /**
     * Get localized value for a given key based on current app language
     * @param key The key to localize
     * @param currentLang Current app language (from ~/helpers/locale)
     * @returns Localized value or the original key if no localization found
     */
    getLocalizedValue(key: string, currentLang: string): string {
        if (!key || !this.passData._allLocalizations) {
            return key;
        }

        const localizations = this.passData._allLocalizations;

        // Normalize current language (handle both 'en_US' and 'en-US' formats)
        const normalizedLang = currentLang.replace('_', '-').toLowerCase();
        const langCode = normalizedLang.split('-')[0]; // Get base language code (e.g., 'en' from 'en-US')

        // Try exact match first (e.g., 'en-US')
        if (localizations[normalizedLang] && localizations[normalizedLang][key]) {
            return localizations[normalizedLang][key];
        }

        // Try base language code (e.g., 'en')
        if (localizations[langCode] && localizations[langCode][key]) {
            return localizations[langCode][key];
        }

        // Try locale-specific variants (e.g., 'en_US', 'en-GB')
        for (const localeKey of Object.keys(localizations)) {
            const keyLower = localeKey.toLowerCase();
            if ((keyLower.startsWith(langCode + '-') || keyLower.startsWith(langCode + '_')) && localizations[localeKey][key]) {
                return localizations[localeKey][key];
            }
        }

        // Fall back to 'en' if available
        if (localizations['en'] && localizations['en'][key]) {
            return localizations['en'][key];
        }

        // Fall back to first available localization
        for (const localeKey of Object.keys(localizations)) {
            if (localizations[localeKey][key]) {
                return localizations[localeKey][key];
            }
        }

        // Return original key if no localization found
        return key;
    }

    /**
     * Format field value based on style attributes
     */
    public formatFieldValue(field: PKPassField, currentLang: string): string {
        if (!field) {
            return;
        }
        let value = String(field.value);
        // Handle date/time formatting
        if (field.dateStyle || field.timeStyle) {
            let date = dayjs(field.value as string);
            if (!isNaN(date.valueOf())) {
                const options: Intl.DateTimeFormatOptions = {};

                // Map PKPass date styles to Intl.DateTimeFormat options
                if (field.dateStyle && field.dateStyle !== 'PKDateStyleNone') {
                    const dateStyleMap = {
                        PKDateStyleShort: 'L',
                        PKDateStyleMedium: 'll',
                        PKDateStyleLong: 'LL',
                        PKDateStyleFull: 'dddd, MMMM D, YYYY'
                    };
                    options.dateStyle = dateStyleMap[field.dateStyle] as any;
                }

                if (field.timeStyle && field.timeStyle !== 'PKDateStyleNone') {
                    const timeStyleMap = {
                        PKDateStyleShort: 'LT',
                        PKDateStyleMedium: 'LTS',
                        PKDateStyleLong: 'LTS',
                        PKDateStyleFull: 'LTS'
                    };
                    options.timeStyle = timeStyleMap[field.timeStyle] as any;
                }
                if (field.ignoresTimeZone) {
                    date = dayjs.utc(field.value);
                }
                return date.format(options.timeStyle);
            }
        }
        value = this.getLocalizedValue(value, currentLang);

        // Handle number formatting
        if (field.numberStyle && typeof field.value === 'number') {
            try {
                const options: Intl.NumberFormatOptions = {};

                if (field.numberStyle === 'PKNumberStylePercent') {
                    value = value + '%';
                } else if (field.numberStyle === 'PKNumberStyleScientific') {
                    // options.notation = 'scientific';
                } else if (field.currencyCode) {
                    options.style = 'currency';
                    value = formatCurrency(field.value, field.currencyCode);
                } else {
                }
            } catch (e) {
                console.warn('Error formatting number:', e);
            }
        }

        return value;
    }

    static fromJSON(jsonObj: any): PKPass {
        const pass = new PKPass(jsonObj.id, jsonObj.page_id || jsonObj.document_id, jsonObj.passType || PKPassType.PKPass); // Support both old and new schema
        Object.assign(pass, {
            passData: typeof jsonObj.passData === 'string' ? JSON.parse(jsonObj.passData) : jsonObj.passData,
            images: typeof jsonObj.images === 'string' ? JSON.parse(jsonObj.images) : jsonObj.images,
            // passJsonPath: jsonObj.passJsonPath,
            // imagesPath: jsonObj.imagesPath,
            createdDate: jsonObj.createdDate,
            modifiedDate: jsonObj.modifiedDate
        });
        return pass;
    }

    toJSON() {
        return {
            id: this.id,
            page_id: this.page_id,
            passType: this.passType,
            passData: this.passData,
            images: this.images,
            // passJsonPath: this.passJsonPath,
            // imagesPath: this.imagesPath,
            createdDate: this.createdDate,
            modifiedDate: this.modifiedDate
        };
    }

    toString() {
        return JSON.stringify(this.toJSON());
    }
}
