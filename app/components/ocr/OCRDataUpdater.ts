import { OCRData } from 'plugin-nativeprocessor';

/**
 * Updates OCRData blocks to reflect user edits made to the joined `text` field.
 * Only `block.text` is updated — boxes and all other properties are preserved.
 *
 * Rules:
 *  - Editing text inside a block → that block's text is updated
 *  - Adding/removing whitespace between blocks → no effect on block contents
 *  - Adding text adjacent to a block → merged into that block
 *  - Adding text in a separator gap → merged into the nearest block
 *  - Deleting an entire block → block is removed from the array
 *  - The last block is never removed (kept with empty text so user can type into it)
 *
 * Algorithm:
 *  1. Locate each block's verbatim text inside `originalText` (in order).
 *  2. Search for each block's original text verbatim inside `updatedText` (in order).
 *     Only matches at end-of-line boundaries are valid anchors (prevents
 *     "Nom G vile" from anchoring inside "Nom G vile 1111").
 *  3. Process gaps between anchors:
 *     - Gap contains non-anchored blocks → gap text is their new content.
 *     - Gap contains only anchored blocks → orphan text goes to nearest block
 *       based on whether it's on the same line as the next anchor.
 */
export function updateOCRBlocks(ocrData: OCRData, updatedText: string): OCRData {
    const { text: originalText, blocks } = ocrData;
    if (blocks.length === 0) return { ...ocrData, text: updatedText };

    // ── 1. Locate each block's span in originalText ──────────────────────────────

    interface BlockSpan {
        blockIndex: number;
        start: number;
        end: number;
    }
    const blockSpans: BlockSpan[] = [];
    {
        let pos = 0;
        for (let bi = 0; bi < blocks.length; bi++) {
            const bt = blocks[bi].text;
            if (!bt) continue;
            const found = originalText.indexOf(bt, pos);
            if (found === -1) continue;
            blockSpans.push({ blockIndex: bi, start: found, end: found + bt.length });
            pos = found + bt.length;
        }
    }
    if (blockSpans.length === 0) return { ...ocrData, text: updatedText };
    const N = blockSpans.length;

    // ── 2. Find anchors in updatedText ───────────────────────────────────────────
    // A match is only a valid anchor if the remainder of the line after the match
    // is whitespace-only. This prevents "Nom G vile" from anchoring inside
    // "Nom G vile 1111" (where " 1111" follows on the same line).

    const aStart = new Array<number>(N).fill(-1);
    const aEnd = new Array<number>(N).fill(-1);
    {
        let uSearch = 0;
        for (let i = 0; i < N; i++) {
            const bt = blocks[blockSpans[i].blockIndex].text;
            let searchFrom = uSearch;
            while (true) {
                const found = updatedText.indexOf(bt, searchFrom);
                if (found === -1) break;
                const after = found + bt.length;
                // Check: from `after` to next '\n' (or end), must be whitespace-only
                const nextNl = updatedText.indexOf('\n', after);
                const lineRemainder = nextNl === -1 ? updatedText.slice(after) : updatedText.slice(after, nextNl);
                if (/^\s*$/.test(lineRemainder)) {
                    // Valid anchor
                    aStart[i] = found;
                    aEnd[i] = after;
                    uSearch = after;
                    break;
                }
                // Not valid (more content on same line) — try next occurrence
                searchFrom = found + 1;
            }
        }
    }

    // ── 3. Process gaps between consecutive anchors ──────────────────────────────

    const newText = new Map<number, string>();

    // Initialize anchored blocks with their verbatim text
    for (let i = 0; i < N; i++) {
        if (aStart[i] !== -1) {
            newText.set(blockSpans[i].blockIndex, blocks[blockSpans[i].blockIndex].text);
        }
    }

    // Build ordered list of anchored span indices
    const anchored = blockSpans.map((_, i) => i).filter((i) => aStart[i] !== -1);

    // Build gap descriptors
    interface Gap {
        uStart: number;
        uEnd: number;
        prevSpanIdx: number;
        nextSpanIdx: number;
    }
    const gaps: Gap[] = [];

    if (anchored.length === 0) {
        gaps.push({ uStart: 0, uEnd: updatedText.length, prevSpanIdx: -1, nextSpanIdx: -1 });
    } else {
        const first = anchored[0];
        const last = anchored[anchored.length - 1];
        gaps.push({ uStart: 0, uEnd: aStart[first], prevSpanIdx: -1, nextSpanIdx: first });
        for (let k = 0; k < anchored.length - 1; k++) {
            const i = anchored[k],
                j = anchored[k + 1];
            gaps.push({ uStart: aEnd[i], uEnd: aStart[j], prevSpanIdx: i, nextSpanIdx: j });
        }
        gaps.push({ uStart: aEnd[last], uEnd: updatedText.length, prevSpanIdx: last, nextSpanIdx: -1 });
    }

    for (const gap of gaps) {
        const gapText = updatedText.slice(gap.uStart, gap.uEnd);
        const lo = gap.prevSpanIdx === -1 ? 0 : gap.prevSpanIdx + 1;
        const hi = gap.nextSpanIdx === -1 ? N - 1 : gap.nextSpanIdx - 1;

        // Collect non-anchored spans in this gap
        const nonAnchored: number[] = [];
        for (let j = lo; j <= hi; j++) {
            if (aStart[j] === -1) nonAnchored.push(j);
        }

        if (nonAnchored.length > 0) {
            // ── Case A: gap contains edited/deleted blocks ────────────────────────
            const parts = splitGapAmongSpans(gapText, nonAnchored.length);
            nonAnchored.forEach((j, k) => {
                newText.set(blockSpans[j].blockIndex, (parts[k] ?? '').trim());
            });
        } else {
            // ── Case B: all anchored — handle orphan text ─────────────────────────
            const trimmed = gapText.trim();
            if (!trimmed) continue;

            // Decide whether orphan goes to prev or next block:
            // If orphan is on the same line as the next anchor (no newline between
            // end of orphan and next anchor), it's a prefix → goes to next block.
            // Otherwise it's a suffix of prev block → goes to prev.
            let goToNext = false;
            if (gap.prevSpanIdx !== -1 && gap.nextSpanIdx !== -1) {
                // Find last newline in gapText before the orphan content
                const lastNewlineInGap = gapText.lastIndexOf('\n');
                const orphanStartInGap = gapText.search(/\S/);
                // If the last newline comes before the orphan starts, then orphan
                // is on the same line as the next anchor
                goToNext = lastNewlineInGap < orphanStartInGap;
            }

            if (goToNext && gap.nextSpanIdx !== -1) {
                const bj = blockSpans[gap.nextSpanIdx].blockIndex;
                // Decide whether to insert a separating space based on the actual
                // character immediately before the next anchor in updatedText.
                const charBeforeNext = aStart[gap.nextSpanIdx] > 0 ? updatedText[aStart[gap.nextSpanIdx] - 1] : undefined;
                const needSpace = charBeforeNext === undefined ? true : /\s/.test(charBeforeNext);
                const existing = newText.get(bj) ?? blocks[bj].text;
                newText.set(bj, needSpace ? trimmed + ' ' + existing : trimmed + existing);
            } else if (gap.prevSpanIdx !== -1) {
                const bi = blockSpans[gap.prevSpanIdx].blockIndex;
                // Decide whether to insert a separating space based on the actual
                // character immediately after the previous anchor in updatedText.
                const charAfterPrev = aEnd[gap.prevSpanIdx] < updatedText.length ? updatedText[aEnd[gap.prevSpanIdx]] : undefined;
                const needSpace = charAfterPrev === undefined ? true : /\s/.test(charAfterPrev);
                const existing = newText.get(bi) ?? blocks[bi].text;
                newText.set(bi, needSpace ? existing + ' ' + trimmed : existing + trimmed);
            } else if (gap.nextSpanIdx !== -1) {
                const bj = blockSpans[gap.nextSpanIdx].blockIndex;
                const charBeforeNext = aStart[gap.nextSpanIdx] > 0 ? updatedText[aStart[gap.nextSpanIdx] - 1] : undefined;
                const needSpace = charBeforeNext === undefined ? true : /\s/.test(charBeforeNext);
                const existing = newText.get(bj) ?? blocks[bj].text;
                newText.set(bj, needSpace ? trimmed + ' ' + existing : trimmed + existing);
            }
        }
    }

    // ── 4. Rebuild blocks ────────────────────────────────────────────────────────
    const newBlocks = blocks
        .map((block, bi) => {
            const raw = newText.get(bi);
            const text = raw !== undefined ? raw.trim() : block.text;
            return { ...block, text };
        })
        .filter((block, idx, arr) => block.text.length > 0 || idx === arr.length - 1);

    if (newBlocks.length === 0) {
        return { ...ocrData, text: updatedText, blocks: [{ ...blocks[blocks.length - 1], text: '' }] };
    }
    return { ...ocrData, text: updatedText, blocks: newBlocks };
}

/**
 * Split a gap string into n parts for n edited blocks.
 * Splits on newlines first (preserving multi-line content), then whitespace.
 */
function splitGapAmongSpans(gapText: string, n: number): string[] {
    if (n === 1) return [gapText];
    const trimmed = gapText.trim();
    if (!trimmed) return new Array(n).fill('');

    // Try splitting on blank-line or newline boundaries first
    const parts = trimmed.split(/\n\s*\n|\n/).filter((p) => p.trim());
    if (parts.length >= n) {
        const result = parts.slice(0, n - 1);
        result.push(parts.slice(n - 1).join('\n'));
        return result;
    }

    // Fall back to whitespace tokens
    const tokens = trimmed.split(/\s+/).filter(Boolean);
    if (tokens.length <= n) return [...tokens, ...new Array(n - tokens.length).fill('')];
    const result: string[] = [];
    for (let i = 0; i < n - 1; i++) result.push(tokens[i]);
    result.push(tokens.slice(n - 1).join(' '));
    return result;
}
