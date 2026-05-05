import { isObject, isString, wrapNativeException } from '@nativescript/core/utils';
import { Optional } from '@nativescript/core/utils/typescript-utils';
import { ApplicationSettings, EventData, File, Folder, Observable, knownFolders, path } from '@nativescript/core';
import NSQLDatabase from '@shared/db/NSQLDatabase';
import { doInBatch } from '@shared/utils/batch';
import SqlQuery from 'kiss-orm/dist/Queries/SqlQuery';
import CrudRepository from 'kiss-orm/dist/Repositories/CrudRepository';
import { DocFolder, Document, IDocFolder, OCRDocument, OCRPage, Page, Tag } from '~/models/OCRDocument';
import { PKPass, PKPassType } from '~/models/PKPass';
import { EVENT_DOCUMENT_DELETED, EVENT_DOCUMENT_USE_COUNT, SETTINGS_ROOT_DATA_FOLDER } from '~/utils/constants';
import { groupByArray } from '@shared/utils';
import DatabaseInterface from 'kiss-orm/dist/Databases/DatabaseInterface';
import QueryIdentifier from 'kiss-orm/dist/Queries/QueryIdentifier';
export const sql = SqlQuery.createFromTemplateString;

let dataFolder: Folder;

export const FOLDERS_SEPARATOR = '&&&';
export const FOLDER_COLOR_SEPARATOR = '&&';

function cleanUndefined(obj) {
    Object.keys(obj).forEach(function (key) {
        if (typeof obj[key] === 'undefined') {
            delete obj[key];
        }
    });
    return obj;
}

// helpers for accent-insensitive searches
function normalizeSearchString(value: any) {
    if (value === null || value === undefined) {
        return '';
    }
    const s = isString(value) ? value : typeof value === 'object' ? JSON.stringify(value) : String(value);
    // normalize to NFD, remove diacritics and lowercase
    const lower = s.toLowerCase();
    let normalized;
    if (__IOS__) {
        normalized = NSString.stringWithString(lower).stringByFoldingWithOptionsLocale(NSStringCompareOptions.DiacriticInsensitiveSearch, NSLocale.currentLocale);
    } else {
        normalized = java.text.Normalizer.normalize(lower, java.text.Normalizer.Form.NFD);
    }
    // some special mappings
    return normalized
        .replace(/[\u0300-\u036f]/g, '')
        .replace(/ß/g, 'ss')
        .replace(/[łŁ]/g, 'l')
        .replace(/[øØ]/g, 'o')
        .replace(/œ/g, 'oe')
        .replace(/æ/g, 'ae');
}

function escapeLike(val: string) {
    if (!val) {
        return val;
    }
    // escape wildcard chars for LIKE patterns
    return val.replace(/[\\%_]/g, (m) => '\\' + m);
}

function tableColumnAlterPromise(query: SqlQuery) {
    return async (sequenceDb: DatabaseInterface) => {
        try {
            await sequenceDb.query(query);
        } catch (err) {
            const error = wrapNativeException(err);
            if (error.message.indexOf('duplicate column name') !== -1) {
                return;
            }
            throw error;
        }
    };
}

export class BaseRepository<T, U = T, V = any> extends CrudRepository<T, U, V> {
    constructor(data) {
        super(data);
    }
    migrations = {
        // addGroupName: sql`ALTER TABLE Groups ADD COLUMN name TEXT`,
        // addGroupOnMap: sql`ALTER TABLE Groups ADD COLUMN onMap INTEGER`
    };
    async applyMigrations() {
        const { migrations } = this;
        if (!migrations) {
            return;
        }
        try {
            await this.database.migrate(migrations);
        } catch (error) {
            console.error(error, error.stack);
        }
    }
}

export class TagRepository extends BaseRepository<Tag, Tag> {
    constructor(database: NSQLDatabase) {
        super({
            database,
            table: 'Tag',
            primaryKey: 'id',
            model: Tag
        });
    }

    async createTables() {
        return this.database.query(sql`
        CREATE TABLE IF NOT EXISTS "Tag" (
            id BIGINT PRIMARY KEY NOT NULL,
            name TEXT NOT NULL
        );
        `);
    }
}
export class FolderRepository extends BaseRepository<DocFolder, IDocFolder> {
    constructor(database: NSQLDatabase) {
        super({
            database,
            table: 'Folder',
            primaryKey: 'id',
            model: DocFolder
        });
    }
    migrations = {
        addModifDate: sql`ALTER TABLE Folder ADD COLUMN modifiedDate BIGINT;`,
        fillNullModifDate: sql`UPDATE Folder SET modifiedDate = (round((julianday('now') - 2440587.5)*86400000)) WHERE modifiedDate IS NULL;`
    };

    async createTables() {
        return this.database.query(sql`
        CREATE TABLE IF NOT EXISTS "Folder" (
            id BIGINT PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            color TEXT
        );
        `);
    }
    async existsById(itemId: string) {
        const result = await this.exists({
            where: sql`id = ${new QueryIdentifier(itemId)}`
        });
        return result;
    }

    async findFolders(rootFolder?: DocFolder) {
        const folders = await this.search({
            select: sql`f.*, 
COUNT(df.document_id) AS count`,
            from: sql`Folder f`,
            postfix: new SqlQuery([
                `
                LEFT JOIN DocumentsFolders df ON f.id = df.folder_id`,
                rootFolder
                    ? `
                WHERE f.name LIKE "${rootFolder.name}/%"`
                    : ``,
                `
                GROUP BY f.id;`
            ])
        });

        // there we group folders but first component like test/test
        // but the issue is that we might have mulitple folders with the same name.
        // in this case we need to return them all or some docs might appear like "lost"
        const grouped = groupByArray(folders, (folder) => [folder.name.split('/')[0]]);
        // DEV_LOG && console.log('grouped', JSON.stringify(grouped));
        return Object.keys(grouped)
            .map((k) => {
                const array = grouped[k].sort((a, b) => a.name.length - b.name.length || a.count - b.count);
                const startIndex = array.findIndex((f) => f.name.replace(new RegExp(`^${rootFolder?.name || ''}/`), '').indexOf('/') !== -1);
                const toReturn = startIndex !== -1 ? array.slice(0, startIndex) : array;

                if (startIndex !== -1 && toReturn.length) {
                    const first = toReturn[0];
                    let count = first.count;
                    let size = first.size;
                    for (let index = startIndex; index < array.length; index++) {
                        const f = array[index];
                        count += f.count;
                        size += f.size;
                    }
                    first.count = count;
                    first.size = size;
                }
                return toReturn;
            })
            .flat();
    }
    async findFolderById(id: number) {
        return (
            await this.search({
                select: sql`f.*, 
COUNT(pf.document_id) AS count`,
                from: sql`Folder f`,
                where: sql`f.id = ${id}`,
                postfix: sql`
LEFT JOIN DocumentsFolders pf ON f.id = pf.folder_id`
            })
        )[0];
    }

    async createModelFromAttributes(attributes: DocFolder): Promise<any> {
        const model = new DocFolder();
        Object.assign(model, attributes);
        return model;
    }
}

export class PKPassRepository extends BaseRepository<PKPass, PKPass> {
    constructor(database: NSQLDatabase) {
        super({
            database,
            table: 'PKPass',
            primaryKey: 'id',
            model: PKPass
        });
    }

    migrations = {
        addPassType: sql`ALTER TABLE PKPass ADD COLUMN passType TEXT NOT NULL DEFAULT 'pkpass'`
    };

    async createTables() {
        return this.database.query(sql`
        CREATE TABLE IF NOT EXISTS "PKPass" (
            id TEXT PRIMARY KEY NOT NULL,
            page_id TEXT NOT NULL,
            passData TEXT NOT NULL,
            images TEXT,
            passJsonPath TEXT,
            imagesPath TEXT,
            createdDate BIGINT NOT NULL DEFAULT (round((julianday('now') - 2440587.5)*86400000)),
            modifiedDate BIGINT,
            FOREIGN KEY(page_id) REFERENCES Page(id) ON DELETE CASCADE
        );
        `);
    }

    async createPKPass(pkpass: PKPass): Promise<PKPass> {
        const createdDate = Date.now();
        return this.create(
            cleanUndefined({
                id: pkpass.id,
                page_id: pkpass.page_id,
                passType: pkpass.passType || PKPassType.PKPass,
                passData: JSON.stringify(pkpass.passData),
                images: JSON.stringify(pkpass.images),
                // passJsonPath: pkpass.passJsonPath,
                // imagesPath: pkpass.imagesPath,
                createdDate,
                modifiedDate: createdDate
            })
        );
    }

    async update(pkpass: PKPass, data?: Partial<PKPass>) {
        const toUpdate: any = {};
        if (data) {
            Object.keys(data).forEach((k) => {
                const value = data[k];
                if (k === 'passData' || k === 'images') {
                    toUpdate[k] = JSON.stringify(value);
                } else {
                    toUpdate[k] = value;
                }
            });
        }

        if (!toUpdate.modifiedDate) {
            toUpdate.modifiedDate = Date.now();
        }

        await super.update(pkpass, toUpdate);
        if (data) {
            Object.assign(pkpass, data);
        }
        return pkpass;
    }

    async createModelFromAttributes(attributes: any): Promise<PKPass> {
        const { images, passData, ...other } = attributes;
        const model = new PKPass(attributes.id, attributes.page_id || attributes.document_id, attributes.passType || PKPassType.PKPass); // Support both for migration
        Object.assign(model, {
            ...other,
            passData: typeof passData === 'string' ? JSON.parse(passData) : passData,
            images: typeof images === 'string' ? JSON.parse(images) : images
        });
        return model;
    }

    async getByPageId(pageId: string): Promise<PKPass | null> {
        const results = await this.search({
            where: sql`page_id=${pageId}`
        });
        return results.length > 0 ? results[0] : null;
    }

    // Keep for backward compatibility during migration
    async getByDocumentId(documentId: string): Promise<PKPass | null> {
        // This will be removed after migration is complete
        const results = await this.search({
            where: sql`page_id=${documentId}` // Try page_id first (might be old data)
        });
        return results.length > 0 ? results[0] : null;
    }
}

export class PageRepository extends BaseRepository<OCRPage, Page> {
    constructor(database: NSQLDatabase) {
        super({
            database,
            table: 'Page',
            primaryKey: 'id',
            model: OCRPage
        });
    }
    migrations = Object.assign(
        {
            addPageExtra: sql`ALTER TABLE Page ADD COLUMN extra TEXT`,
            addPageName: sql`ALTER TABLE Page ADD COLUMN name TEXT`,
            addPageBrightness: sql`ALTER TABLE Page ADD COLUMN brightness INTEGER`,
            addPageContrasts: sql`ALTER TABLE Page ADD COLUMN contrast INTEGER`,
            transformsSplit: sql`UPDATE Page SET transforms = replace( transforms, ',', '|' )`,
            removeDataPath: () => sql`UPDATE Page SET imagePath = replace( imagePath, ${dataFolder.path}, '' ), sourceImagePath = replace( sourceImagePath, ${dataFolder.path}, '' )`,
            addSourceImageWidth: sql`ALTER TABLE Page ADD COLUMN sourceImageWidth INTEGER`,
            addSourceImageHeight: sql`ALTER TABLE Page ADD COLUMN sourceImageHeight INTEGER`,
            addSourceImageRotation: sql`ALTER TABLE Page ADD COLUMN sourceImageRotation INTEGER`,

            updatePageSearchAccentInsensitive: (sequenceDb: DatabaseInterface) =>
                new Promise<void>(async (resolve, reject) => {
                    try {
                        await sequenceDb.query(sql`ALTER TABLE Page ADD COLUMN nameSearch TEXT`);
                        await sequenceDb.query(sql`ALTER TABLE Page ADD COLUMN ocrDataSearch TEXT`);
                        const pages = await this.search();
                        DEV_LOG && console.log('updateSearchAccentInsensitive for Pages', pages.length);
                        await doInBatch(
                            pages,
                            async (p: OCRPage) => {
                                const updates: any = {};
                                if (p.name && !p.nameSearch) {
                                    updates.nameSearch = normalizeSearchString(p.name);
                                }
                                if (p.ocrData && !p.ocrDataSearch) {
                                    const ocrStr = p.ocrData.text;
                                    updates.ocrDataSearch = normalizeSearchString(ocrStr);
                                }
                                if (Object.keys(updates).length) {
                                    await super.update(p, updates);
                                }
                            },
                            10
                        );
                        await sequenceDb.query(sql`CREATE INDEX IF NOT EXISTS idx_page_nameSearch ON Page(nameSearch)`);
                        await sequenceDb.query(sql`CREATE INDEX IF NOT EXISTS idx_page_ocrDataSearch ON Page(ocrDataSearch)`);
                        resolve();
                    } catch (e) {
                        console.error('Error filling Page search indexes', e);
                        reject(e);
                    }
                })
        },
        CARD_APP
            ? {
                  addQRCode: sql`ALTER TABLE Page ADD COLUMN qrcode TEXT`,
                  addColors: sql`ALTER TABLE Page ADD COLUMN colors TEXT`,
                  addPKPass: sql`ALTER TABLE Page ADD COLUMN pkpass_id TEXT REFERENCES PKPass(id)`
              }
            : {}
    );

    async createTables() {
        return this.database.query(
            sql`
        CREATE TABLE IF NOT EXISTS "Page" (
            id TEXT PRIMARY KEY NOT NULL,
            createdDate BIGINT NOT NULL DEFAULT (round((julianday('now') - 2440587.5)*86400000)),
            modifiedDate NOT NULL DEFAULT (round((julianday('now') - 2440587.5)*86400000)),
            pageIndex INTEGER,
            colorType TEXT,
            colorMatrix TEXT,
            transforms TEXT,
            rotation INTEGER DEFAULT 0,
            scale INTEGER DEFAULT 1,
            crop TEXT,
            ocrData TEXT,
            width INTEGER,
            height INTEGER,
            size INTEGER,
            sourceImagePath TEXT,
            imagePath TEXT,
            document_id TEXT,
            FOREIGN KEY(document_id) REFERENCES Document(id) ON DELETE CASCADE ON UPDATE CASCADE
        );`
        );
    }

    async createPage(page: OCRPage, dataFolder: string) {
        const { extra, ...others } = page;
        const createdDate = Date.now();
        return this.create(
            cleanUndefined({
                modifiedDate: createdDate,
                createdDate,
                ...others,
                imagePath: page.imagePath?.replace(dataFolder, ''),
                sourceImagePath: page.sourceImagePath?.replace(dataFolder, ''),
                extra: isObject(extra) ? JSON.stringify(extra) : extra,
                pageIndex: -1,
                rotation: page.rotation && !isNaN(page.rotation) ? page.rotation : 0,
                scale: page.scale ?? 1,
                crop: page.crop ? JSON.stringify(page.crop) : undefined,
                colorMatrix: page.colorMatrix ? JSON.stringify(page.colorMatrix) : undefined,
                ocrData: page.ocrData ? JSON.stringify(page.ocrData) : undefined,
                nameSearch: normalizeSearchString(page.name),
                ocrDataSearch: page.ocrData ? normalizeSearchString(page.ocrData.text) : undefined,
                ...(CARD_APP
                    ? {
                          qrcode: page.qrcode ? JSON.stringify(page.qrcode) : undefined,
                          colors: page.colors ? JSON.stringify(page.colors) : undefined
                      }
                    : {})
            })
        );
    }

    async update(page: OCRPage, data?: Partial<OCRPage>, updateModifiedDate = true) {
        if (!data) {
            const toUpdate: Partial<OCRPage> = {};
            if (updateModifiedDate) {
                toUpdate.modifiedDate = Date.now();
            }
            await this.update(page, toUpdate);
            return page;
        }
        if (updateModifiedDate && !data.modifiedDate) {
            data.modifiedDate = Date.now();
        }
        const toSave: Partial<Document> = {};
        const toUpdate: any = {};
        Object.keys(data).forEach((k) => {
            const value = data[k];
            toSave[k] = value;
            if (k === 'extra') {
                toUpdate[k] = JSON.stringify(Object.assign(page.extra || {}, value));
            } else if (k === 'ocrData') {
                toUpdate[k] = JSON.stringify(value);
                toUpdate.ocrDataSearch = normalizeSearchString(value.text);
            } else if (k === 'name') {
                toUpdate[k] = value;
                toUpdate.nameSearch = normalizeSearchString(value);
            } else if (typeof value === 'object' || Array.isArray(value)) {
                toUpdate[k] = JSON.stringify(value);
            } else {
                toUpdate[k] = value;
            }
        });

        await super.update(page, toUpdate);
        Object.assign(page, toSave);
        return page;
    }
    async createModelFromAttributes(attributes: OCRPage): Promise<any> {
        const { colorMatrix, colors, crop, document_id, extra, id, imagePath, ocrData, qrcode, sourceImagePath, ...other } = attributes as any;
        const model = new OCRPage(id, document_id);
        Object.assign(model, {
            ...other,
            // imagePath,
            // sourceImagePath,
            imagePath: imagePath ? dataFolder.path + imagePath : null,
            sourceImagePath: sourceImagePath ? dataFolder.path + sourceImagePath : null,
            extra: isString(extra) ? JSON.parse(extra) : extra,
            crop: typeof crop === 'string' ? JSON.parse(crop) : crop,
            colorMatrix: typeof colorMatrix === 'string' ? JSON.parse(colorMatrix) : colorMatrix,
            ocrData: typeof ocrData === 'string' ? JSON.parse(ocrData) : ocrData,
            ...(CARD_APP
                ? {
                      qrcode: typeof qrcode === 'string' ? JSON.parse(qrcode) : qrcode,
                      colors: typeof colors === 'string' ? JSON.parse(colors) : colors
                  }
                : {})
        });
        return model;
    }
}

export class DocumentRepository extends BaseRepository<OCRDocument, Document> {
    constructor(
        database: NSQLDatabase,
        public pagesRepository: PageRepository,
        public tagsRepository: TagRepository,
        public foldersRepository: FolderRepository,
        public pkpassRepository?: PKPassRepository
    ) {
        super({
            database,
            table: 'Document',
            primaryKey: 'id',
            model: OCRDocument
        });
    }

    async createTables() {
        return Promise.all([
            this.database.query(sql`
            CREATE TABLE IF NOT EXISTS "Document" (
                id TEXT PRIMARY KEY NOT NULL,
                createdDate BIGINT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now')),
                modifiedDate BIGINT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now')),
                name TEXT,
                _synced INTEGER DEFAULT 0,
                favorite INTEGER NOT NULL DEFAULT 0,
                usedDate BIGINT,
                useCount INTEGER NOT NULL DEFAULT 0
                );
        `),
            this.database.query(sql`
        CREATE TABLE IF NOT EXISTS "DocumentsTags" (
            document_id TEXT,
            tag_id TEXT,
            PRIMARY KEY(document_id, tag_id),
            FOREIGN KEY(document_id) REFERENCES Document(id) ON DELETE CASCADE,
            FOREIGN KEY(tag_id) REFERENCES Tag(id) ON DELETE CASCADE
        );
    `),
            this.database.query(sql`
        CREATE TABLE IF NOT EXISTS "DocumentsFolders" (
            document_id TEXT,
            folder_id TEXT,
            PRIMARY KEY(document_id, folder_id),
            FOREIGN KEY(document_id) REFERENCES Document(id) ON DELETE CASCADE,
            FOREIGN KEY(folder_id) REFERENCES Folder(id) ON DELETE CASCADE
        );
    `)
        ]);
    }

    migrations = {
        addExtra: sql`ALTER TABLE Document ADD COLUMN extra TEXT`,
        addPagesOrder: sql`ALTER TABLE Document ADD COLUMN pagesOrder TEXT`,
        addFavorite: tableColumnAlterPromise(sql`ALTER TABLE Document ADD COLUMN favorite INTEGER NOT NULL DEFAULT 0`),
        addUsedDate: tableColumnAlterPromise(sql`ALTER TABLE Document ADD COLUMN usedDate BIGINT`),
        addUseCount: tableColumnAlterPromise(sql`ALTER TABLE Document ADD COLUMN useCount INTEGER NOT NULL DEFAULT 0`),

        updateDocSearchAccentInsensitive: (sequenceDb: DatabaseInterface) =>
            new Promise<void>(async (resolve, reject) => {
                try {
                    await sequenceDb.query(sql`ALTER TABLE Document ADD COLUMN nameSearch TEXT`);
                    const docs = await this.search();
                    DEV_LOG && console.log('updateSearchAccentInsensitive docs', docs.length);
                    await doInBatch(
                        docs,
                        async (d: OCRDocument) => {
                            const updates: any = {};
                            if (d.name && !d.nameSearch) {
                                updates.nameSearch = normalizeSearchString(d.name);
                            }
                            if (Object.keys(updates).length) {
                                await super.update(d, updates);
                            }
                        },
                        10
                    );
                    await sequenceDb.query(sql`CREATE INDEX IF NOT EXISTS idx_doc_nameSearch ON Document(nameSearch)`);
                    resolve();
                } catch (e) {
                    console.error('Error filling Page search indexes', e);
                    reject(e);
                }
            })
    };

    async createDocument(document: Document) {
        const { extra, folders, ...others } = document;
        const createdDate = Date.now();
        const doc = await this.create(
            cleanUndefined({
                createdDate,
                modifiedDate: createdDate,
                ...others,
                extra: isObject(extra) ? JSON.stringify(extra) : extra,
                _synced: 0,
                pagesOrder: others.pagesOrder ? JSON.stringify(others.pagesOrder) : undefined,
                nameSearch: normalizeSearchString(others.name)
            })
        );
        if (folders) {
            for (let index = 0; index < folders.length; index++) {
                await doc.setFolder({ folderId: folders[index], notify: false });
            }
            doc.folders = folders;
        }
        doc.pages = [];
        return doc;
    }

    async update(document: OCRDocument, data?: Partial<OCRDocument>, updateModifiedDate = true) {
        // DEV_LOG && console.log('doc update', data);
        if (!data) {
            const toUpdate: Partial<OCRDocument> = {
                _synced: 0
            };
            if (updateModifiedDate) {
                toUpdate.modifiedDate = Date.now();
            }
            await this.update(document, toUpdate);
            return document;
        }
        if (updateModifiedDate && !data.modifiedDate) {
            data.modifiedDate = Date.now();
        }
        if (data._synced === undefined) {
            data._synced = 0;
        }
        const toSave: Partial<Document> = {};
        const toUpdate: any = {};
        Object.keys(data).forEach((k) => {
            const value = data[k];
            toSave[k] = value;
            if (k === 'extra') {
                toUpdate[k] = JSON.stringify(Object.assign(document.extra || {}, value));
            } else if (k === 'name') {
                toUpdate[k] = value;
                toUpdate.nameSearch = normalizeSearchString(value);
            } else if (k === 'favorite' || k === 'usedDate' || k === 'useCount') {
                toUpdate[k] = value;
            } else if (typeof value === 'object' || Array.isArray(value)) {
                toUpdate[k] = JSON.stringify(value);
            } else {
                toUpdate[k] = value;
            }
        });

        await super.update(document, toUpdate);
        Object.assign(document, toSave);
        // DEV_LOG && console.log('update doc', toSave, document._synced);
        return document;
    }

    async incrementUsage(document: OCRDocument) {
        const usedDate = Date.now();
        const useCount = (document.useCount || 0) + 1;
        await super.update(document, { usedDate, useCount });
        document.usedDate = usedDate;
        document.useCount = useCount;
        documentsService.notify({ eventName: EVENT_DOCUMENT_USE_COUNT, documents: [document] } as SingleDocumentEventData);
    }
    async addTag(document: OCRDocument, tagId: string) {
        try {
            let tag: Tag;
            try {
                tag = await this.tagsRepository.get(tagId);
            } catch (error) {}
            // console.log('addGroupToItem', group);
            if (!tag) {
                tag = await this.tagsRepository.create({ id: tagId, name: tagId });
            }
            const relation = await this.database.query(sql` SELECT * FROM DocumentsTags WHERE "document_id" = ${document.id} AND "tag_id" = ${tagId}`);
            if (relation.length === 0) {
                await this.database.query(sql` INSERT INTO DocumentsTags ( document_id, tag_id ) VALUES(${document.id}, ${tagId})`);
            }
            document.tags = document.tags || [];
            document.tags.push(tagId);
        } catch (error) {
            console.error(error, error.stack);
        }
    }

    async getItem(itemId: string) {
        const element = await this.get(itemId);
        return element;
    }
    async search(args: { end?: SqlQuery; from?: SqlQuery; postfix?: SqlQuery; select?: SqlQuery; where?: SqlQuery; orderBy?: SqlQuery } = {}) {
        const result = await super.search({ ...args });
        // remove all documents with no Page, it is a bug and should never happen

        return result.filter((d) => d.pages?.length > 0);
    }
    async existsById(itemId: string) {
        const result = await this.exists({
            where: sql`id = ${new QueryIdentifier(itemId)}`
        });
        return result;
    }
    async findDocuments({ filter, folder, omitThoseWithFolders = false, order = 'id DESC' }: { filter?: string; folder?: DocFolder; omitThoseWithFolders?: boolean; order?: string } = {}) {
        // Build the secondary sort expression. All columns are on the Document alias "d".
        const secondaryOrder = `d.${order}`;
        // Always sort favorites first, then by the user-selected key
        const orderBy = new SqlQuery([`d.favorite DESC, ${secondaryOrder}`]);

        const args = {
            select: new SqlQuery([
                `d.*,
            group_concat(f.id, '${FOLDERS_SEPARATOR}') AS folders`
            ]),
            from: sql`Document d`,
            orderBy,
            groupBy: sql`d.id`
        } as any;

        const foldersPostfix = `LEFT JOIN 
    DocumentsFolders df ON d.id = df.document_id
LEFT JOIN 
    Folder f ON df.folder_id = f.id`;

        if (filter?.length || folder) {
            if (filter?.length) {
                const normalizedFilter = normalizeSearchString(filter);
                const escaped = escapeLike(normalizedFilter);
                const where = `p.nameSearch LIKE '%${escaped}%' OR d.nameSearch LIKE '%${escaped}%' OR p.ocrDataSearch LIKE '%${escaped}%'`;
                if (folder) {
                    args.postfix = sql` LEFT JOIN Page p ON p.document_id = d.id `;
                    args.where = new SqlQuery([`df.folder_id = ${folder.id} AND (${where})`]);
                } else {
                    args.postfix = sql` LEFT JOIN Page p ON p.document_id = d.id `;
                    args.where = new SqlQuery([where]);
                }
            } else {
                args.where = new SqlQuery([`df.folder_id = ${folder.id}`]);
            }
            args.postfix = new SqlQuery((args.postfix ? [args.postfix] : []).concat([foldersPostfix]));
        } else {
            if (omitThoseWithFolders) {
                args.select = sql`d.*`;
                args.from = sql`Document d`;
                args.where = sql`d.id NOT IN(SELECT document_id FROM DocumentsFolders)`;
            } else {
                args.postfix = new SqlQuery((args.postfix ? [args.postfix] : []).concat([foldersPostfix]));
            }
        }
        return this.search(args);
    }
    async createModelFromAttributes(attributes: Required<any> | OCRDocument): Promise<any> {
        const { extra, folders, id, pagesOrder, ...others } = attributes;
        // DEV_LOG && console.log('createModelFromAttributes', id, folders, typeof folders);
        const document = new OCRDocument(id);
        Object.assign(document, {
            id,
            ...others,
            folders: (typeof folders === 'string' ? folders.split(FOLDERS_SEPARATOR) : folders)?.map((f) => parseInt(f, 10)),
            extra: isString(extra) ? JSON.parse(extra) : extra,
            pagesOrder: typeof pagesOrder === 'string' && pagesOrder.length ? JSON.parse(pagesOrder) : pagesOrder
        });

        let pages = await this.pagesRepository.search({ where: sql`document_id = ${document.id}` });
        if (pages.length) {
            const { pagesOrder } = document;
            if (pagesOrder) {
                pages = pages.sort(function (a, b) {
                    return pagesOrder.indexOf(a.id) - pagesOrder.indexOf(b.id);
                });
                document.pages = pages;
            } else {
                // pagesOrder was not existing before let s create it.
                pages = pages.sort(function (a, b) {
                    return a['pageIndex'] - b['pageIndex'];
                });
                document.pages = pages;
                await document.save({}, true);
            }
            if (CARD_APP) {
                // Load PKPass data for each page
                for (const page of pages) {
                    if (page.pkpass_id) {
                        page.pkpass = await this.pkpassRepository.getByPageId(page.id);
                        const images = page.pkpass.images;
                        const docFolder = document.folderPath;
                        const pageFolder = docFolder.getFolder(page.id);
                        const pkpassFolder = pageFolder.getFolder('pkpass');
                        // DEV_LOG && console.log('loading pkpass', images, pkpassFolder.path);
                        Object.keys(images).map((key) => (images[key] = path.join(pkpassFolder.path, images[key])));
                        // DEV_LOG && console.log('loading pkpass1', images, pkpassFolder.path);
                    }
                }
            }
        }

        return document;
    }
}

export type DocumentEventData = Optional<EventData<Observable>, 'object'> & { fromWorker?: true };

export interface SingleDocumentEventData extends DocumentEventData {
    doc?: OCRDocument;
    folder?: DocFolder;
}
export type DocumentAddedEventData = SingleDocumentEventData;
export interface DocumentMovedFolderEventData extends SingleDocumentEventData {
    oldFolderId?: number;
}
export interface DocumentFolderAddedEventData extends DocumentEventData {
    folder?: DocFolder;
}
export interface FolderUpdatedEventData extends DocumentEventData {
    folder?: DocFolder;
    changedProps: Set<string>;
}
export interface DocumentPagesAddedEventData extends SingleDocumentEventData {
    pages?: OCRPage[];
}
export interface DocumentPageDeletedEventData extends SingleDocumentEventData {
    pageIndex?: number;
}
export interface DocumentPageUpdatedEventData extends SingleDocumentEventData {
    pageIndex?: number;
    imageUpdated?: boolean;
}
export interface DocumentUpdatedEventData extends SingleDocumentEventData {
    updateModifiedDate?: boolean;
    changedProps: Set<string>;
}
export interface DocumentDeletedEventData extends DocumentEventData {
    documents?: OCRDocument[];
    folders?: number[];
}

export type DocumentEvents =
    | DocumentAddedEventData
    | DocumentDeletedEventData
    | DocumentUpdatedEventData
    | DocumentPagesAddedEventData
    | DocumentPageDeletedEventData
    | DocumentPageUpdatedEventData
    | DocumentMovedFolderEventData;

let ID = 0;
export class DocumentsService extends Observable {
    static DB_NAME = 'db.sqlite';
    rootDataFolder: string;
    dataFolder: Folder;
    id: number;
    // connection: Connection;
    started = false;
    db: NSQLDatabase;
    pageRepository: PageRepository;
    tagRepository: TagRepository;
    folderRepository: FolderRepository;
    documentRepository: DocumentRepository;
    pkpassRepository?: PKPassRepository;

    constructor() {
        super();
        this.id = ID++;
    }
    async start(db?) {
        if (this.started) {
            return;
        }
        let rootDataFolder;
        if (__ANDROID__) {
            rootDataFolder = ApplicationSettings.getString(SETTINGS_ROOT_DATA_FOLDER);
            if (rootDataFolder && !Folder.exists(rootDataFolder)) {
                rootDataFolder = null;
                ApplicationSettings.remove(SETTINGS_ROOT_DATA_FOLDER);
            }
            if (!rootDataFolder) {
                rootDataFolder = knownFolders.externalDocuments().path;
                if (__ANDROID__) {
                    //we need to check if the sdcard is actually readable. it could be an ext3/ext4 where we can't write
                    // in this case we wan't to use internal storage
                    try {
                        Folder.fromPath(rootDataFolder).getFolder('data');
                    } catch (error) {
                        rootDataFolder = knownFolders.documents().path;
                    }
                }
                ApplicationSettings.setString(SETTINGS_ROOT_DATA_FOLDER, rootDataFolder);
            }
        } else {
            // on iOS we cant store any knownFolders cause their path can change upon app upgrade
            rootDataFolder = knownFolders.externalDocuments().path;
        }
        this.rootDataFolder = rootDataFolder;
        dataFolder = this.dataFolder = Folder.fromPath(rootDataFolder).getFolder('data');
        DEV_LOG && console.info('DocumentsService', 'start', this.id, rootDataFolder, !!db, dataFolder.path);

        if (db) {
            this.db = new NSQLDatabase(db, {
                // for now it breaks
                // threading: true,
                transformBlobs: false
            } as any);
        } else {
            const filePath = path.join(rootDataFolder, DocumentsService.DB_NAME);
            DEV_LOG && console.log('DocumentsService', 'dbFileName', filePath, File.exists(filePath));

            this.db = new NSQLDatabase(filePath, {
                // for now it breaks
                // threading: true,
                transformBlobs: false,
                //TODO: it seems ocrData can be sometimes too big. Maybe the best would be not to store it in db
                cursorWindowSize: 4 * 1024 * 1024
            } as any);
        }

        this.pageRepository = new PageRepository(this.db);
        this.tagRepository = new TagRepository(this.db);
        this.folderRepository = new FolderRepository(this.db);
        if (CARD_APP) {
            this.pkpassRepository = new PKPassRepository(this.db);
        }
        this.documentRepository = new DocumentRepository(this.db, this.pageRepository, this.tagRepository, this.folderRepository, this.pkpassRepository);
        if (!db) {
            await this.documentRepository.createTables();
            await this.pageRepository.createTables();
            await this.tagRepository.createTables();
            await this.folderRepository.createTables();
            if (CARD_APP) {
                await this.pkpassRepository.createTables();
            }
            try {
                await this.db.migrate(
                    Object.assign(
                        {},
                        this.documentRepository.migrations,
                        this.pageRepository.migrations,
                        this.tagRepository.migrations,
                        this.folderRepository.migrations,
                        CARD_APP ? this.pkpassRepository.migrations : {}
                    )
                );
            } catch (error) {
                console.error('error applying migrations', error, error.stack);
            }
        }

        this.notify({ eventName: 'started' });
        this.started = true;
    }
    async deleteDocuments(documents: OCRDocument[]) {
        DEV_LOG &&
            console.log(
                'deleteDocuments',
                documents.map((d) => d.id)
            );

        await doInBatch<OCRDocument, void>(
            documents,
            async (d: OCRDocument) => {
                const id = d.id;
                DEV_LOG && console.log('deleteDocument', id);
                await Promise.all(d.pages.map((p) => this.pageRepository.delete(p)));
                await this.documentRepository.delete(d);
                await d.removeFromFolder();
                await d.removeFromDisk();
                // we notify on each delete so that UI updates fast
                documentsService.notify({ eventName: EVENT_DOCUMENT_DELETED, documents: [d], folders: d.folders } as DocumentDeletedEventData);
            },
            1
        );
        // await this.documentRepository.delete(model);
        // await Promise.all(documents.map((d) => Promise.all(d.pages.map((p) => this.pageRepository.delete(p)).concat(this.documentRepository.delete(d), d.removeFromFolder()))));
        // await OCRDocument.delete(docs.map((d) => d.id));
        // documents.forEach((doc) => doc.removeFromDisk());
        // this.notify({ eventName: EVENT_DOCUMENT_DELETED, documents } as DocumentDeletedEventData);
    }
    stop() {
        DEV_LOG && console.log('DocumentsService stop');
        if (!this.started) {
            return;
        }
        this.started = false;
        this.db?.disconnect();
        this.db = null;
    }

    async saveDocument(doc: OCRDocument) {
        this.documentRepository.update(doc);
        // doc.save();
    }
}
export const documentsService = new DocumentsService();
