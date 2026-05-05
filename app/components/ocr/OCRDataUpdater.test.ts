import { OCRData } from 'plugin-nativeprocessor';
import { updateOCRBlocks } from './OCRDataUpdater';
import { expect, test } from 'vitest';

const ocrData: OCRData = {
    blocks: [
        { box: { height: 21, width: 544, x: 117, y: 22 }, confidence: 93.31, fontSize: 5, text: "CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET" },
        { box: { height: 33, width: 265, x: 17, y: 63 }, confidence: 78.35, fontSize: 6, text: 'Nom G vile' },
        { box: { height: 19, width: 83, x: 16, y: 140 }, confidence: 90.4, fontSize: 6, text: 'Prénom' },
        { box: { height: 60, width: 246, x: 13, y: 210 }, confidence: 79.58, fontSize: 5, text: 'Réglement lu et approuvé\nSignature' },
        { box: { height: 22, width: 94, x: 199, y: 432 }, confidence: 96.24, fontSize: 7, text: '100003' },
        {
            box: { height: 139, width: 259, x: 492, y: 82 },
            confidence: 93.48,
            fontSize: 5,
            text: "Changement d'adresse\nou carte perdue\nprevenir votre bibliothèque.\nCarte trouvee a retourner :\nBibliotheque Kateb Yacine\nService informatique"
        },
        { box: { height: 17, width: 153, x: 492, y: 223 }, confidence: 67.02, fontSize: 5, text: '202, Grand Place' },
        { box: { height: 16, width: 143, x: 491, y: 247 }, confidence: 96.64, fontSize: 5, text: '38100 Grenoble' },
        { box: { height: 31, width: 210, x: 491, y: 302 }, confidence: 90.11, fontSize: 7, text: 'bm-grenoble.fr' },
        { box: { height: 73, width: 241, x: 490, y: 356 }, confidence: 96.04, fontSize: 4, text: 'INFORMATIONS PRATIQUES\nCATALOGUE EN LIGNE\nBIBLIOTHÈQUE NUMÉRIQUE' }
    ],
    imageHeight: 474,
    imageWidth: 770,
    text: "CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET\n\nNom G vile\n\nPrénom\n\nRéglement lu et approuvé\nSignature\n\n100003\n\nChangement d'adresse\nou carte perdue\nprevenir votre bibliothèque.\nCarte trouvee a retourner :\nBibliotheque Kateb Yacine\nService informatique\n\n202, Grand Place\n\n38100 Grenoble\n\nbm-grenoble.fr\n\nINFORMATIONS PRATIQUES\nCATALOGUE EN LIGNE\nBIBLIOTHÈQUE NUMÉRIQUE"
};

function blockText(r: OCRData, search: string): string | undefined {
    return r.blocks.find((b) => b.text.includes(search))?.text;
}

// ── Multi-step chained edits (the bug scenario) ──────────────────────────────

test('add 1111 to block[1], then 2222 to block[7] (chained)', () => {
    const step1 = updateOCRBlocks(ocrData, ocrData.text.replace('Nom G vile', 'Nom G vile 1111'));
    const step2 = updateOCRBlocks(step1, step1.text.replace('38100 Grenoble', '38100 Grenoble 2222'));
    expect(step2.blocks[1].text).toBe('Nom G vile 1111');
    expect(blockText(step2, 'Grenoble')).toBe('38100 Grenoble 2222');
    expect(step2.blocks[0].text).toBe("CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET");
});

test('add 1111 to block[1], then 2222 to adjacent block[0] (chained)', () => {
    const step1 = updateOCRBlocks(ocrData, ocrData.text.replace('Nom G vile', 'Nom G vile 1111'));
    const step2 = updateOCRBlocks(step1, step1.text.replace("CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET", "CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET 2222"));
    expect(step2.blocks[0].text).toBe("CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET 2222");
    expect(step2.blocks[1].text).toBe('Nom G vile 1111');
    expect(step2.blocks[2].text).toBe('Prénom');
});

test('add 1111 to block[1], then 2222 to adjacent block[2] (chained)', () => {
    const step1 = updateOCRBlocks(ocrData, ocrData.text.replace('Nom G vile', 'Nom G vile 1111'));
    const step2 = updateOCRBlocks(step1, step1.text.replace('Prénom', 'Prénom 2222'));
    expect(step2.blocks[1].text).toBe('Nom G vile 1111');
    expect(step2.blocks[2].text).toBe('Prénom 2222');
    expect(step2.blocks[0].text).toBe("CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET");
});

test('add 1111 to block[0], then 2222 to block[1] (both adjacent, chained)', () => {
    const step1 = updateOCRBlocks(ocrData, ocrData.text.replace("CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET", "CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET 1111"));
    const step2 = updateOCRBlocks(step1, step1.text.replace('Nom G vile', 'Nom G vile 2222'));
    expect(step2.blocks[0].text).toBe("CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET 1111");
    expect(step2.blocks[1].text).toBe('Nom G vile 2222');
    expect(step2.blocks[2].text).toBe('Prénom');
});

// ── Add text at boundaries ────────────────────────────────────────────────────

test('add text at END of a block', () => {
    const u = ocrData.text.replace('Nom G vile', 'Nom G vile Jr');
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks[1].text).toBe('Nom G vile Jr');
    expect(r.blocks[2].text).toBe('Prénom');
    expect(r.blocks.length).toBe(ocrData.blocks.length);
});

test('add text at START of a block', () => {
    const u = ocrData.text.replace('Nom G vile', 'Mr Nom G vile');
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks[1].text).toBe('Mr Nom G vile');
    expect(r.blocks[2].text).toBe('Prénom');
    expect(r.blocks.length).toBe(ocrData.blocks.length);
});

// ── Orphan text in separator gaps ────────────────────────────────────────────

test('text added in separator gap merges into preceding block', () => {
    const u = ocrData.text.replace('Nom G vile\n\nPrénom', 'Nom G vile\n tata \nPrénom');
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks[1].text).toBe('Nom G vile tata');
    expect(r.blocks[2].text).toBe('Prénom');
    expect(r.blocks.length).toBe(ocrData.blocks.length);
});

test('text added before first block is prepended to block 0', () => {
    const u = 'HEADER\n\n' + ocrData.text;
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks[0].text).toBe("HEADER CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET");
    expect(r.blocks[1].text).toBe('Nom G vile');
});

test('orphan with more leading than trailing whitespace still goes to prev block', () => {
    const u = ocrData.text.replace('Nom G vile\n\nPrénom', 'Nom G vile\n\n\norphan\nPrénom');
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks[1].text).toBe('Nom G vile orphan');
    expect(r.blocks[2].text).toBe('Prénom');
});

// ── Empty text / last-block survival ─────────────────────────────────────────

test('clearing all text keeps exactly one empty block', () => {
    const r = updateOCRBlocks(ocrData, '');
    expect(r.blocks.length).toBe(1);
    expect(r.blocks[0].text).toBe('');
});

test('clearing a single-block document keeps the block (empty)', () => {
    const single: OCRData = { ...ocrData, blocks: [ocrData.blocks[4]], text: '100003' };
    const r = updateOCRBlocks(single, '');
    expect(r.blocks.length).toBe(1);
    expect(r.blocks[0].text).toBe('');
});

// ── Editing block content ─────────────────────────────────────────────────────

test('edit text inside a block', () => {
    const u = ocrData.text.replace('Nom G vile', 'Nom Granville');
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks[1].text).toBe('Nom Granville');
    expect(r.blocks[0].text).toBe("CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET");
    expect(r.blocks.length).toBe(ocrData.blocks.length);
});

test('remove characters from a block', () => {
    const u = ocrData.text.replace('38100 Grenoble', '38100 Grenble');
    const r = updateOCRBlocks(ocrData, u);
    expect(blockText(r, 'Gren')).toBe('38100 Grenble');
    expect(r.blocks.length).toBe(ocrData.blocks.length);
});

test('edit a multi-line block', () => {
    const u = ocrData.text.replace('Réglement lu et approuvé\nSignature', 'Règlement lu et approuvé\nSignature OK');
    const r = updateOCRBlocks(ocrData, u);
    expect(blockText(r, 'Règlement')).toBe('Règlement lu et approuvé\nSignature OK');
});

test('edit the first block', () => {
    const u = ocrData.text.replace("CARTE D'ABONNÉE", "CARTE D'ABONNÉ");
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks[0].text).toBe("CARTE D'ABONNÉ / INDISPENSABLE POUR LE PRET");
});

test('edit the last block', () => {
    const u = ocrData.text.replace('BIBLIOTHÈQUE NUMÉRIQUE', 'BIBLIOTHÈQUE NUMERIQUE');
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks[r.blocks.length - 1].text).toBe('INFORMATIONS PRATIQUES\nCATALOGUE EN LIGNE\nBIBLIOTHÈQUE NUMERIQUE');
});

// ── Deleting blocks ───────────────────────────────────────────────────────────

test('delete an entire block', () => {
    const u = ocrData.text.replace('\n\n100003', '');
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks.some((b) => b.text === '100003')).toBe(false);
    expect(r.blocks.length).toBe(ocrData.blocks.length - 1);
});

test('delete the first block', () => {
    const u = ocrData.text.replace("CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET\n\n", '');
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks.some((b) => b.text.startsWith('CARTE'))).toBe(false);
    expect(r.blocks.length).toBe(ocrData.blocks.length - 1);
});

// ── Whitespace separator changes ──────────────────────────────────────────────

test('reducing separator whitespace leaves block texts unchanged', () => {
    const u = ocrData.text.replace('PRET\n\nNom', 'PRET\nNom');
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks[0].text).toBe("CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET");
    expect(r.blocks[1].text).toBe('Nom G vile');
});

test('removing all separator whitespace leaves block texts unchanged', () => {
    const u = ocrData.text.replace('PRET\n\nNom', 'PRETNom');
    const r = updateOCRBlocks(ocrData, u);
    expect(r.blocks[0].text).toBe("CARTE D'ABONNÉE / INDISPENSABLE POUR LE PRET");
    expect(r.blocks[1].text).toBe('Nom G vile');
});

test('removing separator between middle blocks leaves both texts unchanged', () => {
    const u = ocrData.text.replace('202, Grand Place\n\n38100 Grenoble', '202, Grand Place\n38100 Grenoble');
    const r = updateOCRBlocks(ocrData, u);
    expect(blockText(r, 'Grand')).toBe('202, Grand Place');
    expect(blockText(r, 'Grenoble')).toBe('38100 Grenoble');
    expect(r.blocks.length).toBe(ocrData.blocks.length);
});

test('failing edit', () => {
    const u = ocrData.text.replace('Prénom', 'aaaPrénom');
    const r = updateOCRBlocks(ocrData, u);
    expect(blockText(r, 'Prénom')).toBe('aaaPrénom');
    expect(r.blocks.length).toBe(ocrData.blocks.length);
});

test('multiple edits failing', () => {
    let u = ocrData.text.replace('Prénom', 'aaaPrénom');
    let r = updateOCRBlocks(ocrData, u);
    u = r.text.replace('38100 Grenoble', '38100 Grenobleaaa');
    r = updateOCRBlocks(r, u);
    expect(blockText(r, 'Nom G vile')).toBe('Nom G vile');
    expect(blockText(r, 'Prénom')).toBe('aaaPrénom');
    expect(blockText(r, '38100 Grenoble')).toBe('38100 Grenobleaaa');
    expect(r.blocks.length).toBe(ocrData.blocks.length);
});

// ── No-op ─────────────────────────────────────────────────────────────────────

test('no change leaves all blocks identical', () => {
    const r = updateOCRBlocks(ocrData, ocrData.text);
    expect(r.blocks.every((b, i) => b.text === ocrData.blocks[i].text)).toBe(true);
});
