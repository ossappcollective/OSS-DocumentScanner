<script lang="ts">
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { createNativeAttributedString } from '@nativescript-community/ui-label';
    import { AWebView } from '@nativescript-community/ui-webview';
    import { Color, Page } from '@nativescript/core';
    import { showError } from '@shared/utils/showError';
    import { OCRData } from 'plugin-nativeprocessor';
    import CActionBar from '~/components/common/CActionBar.svelte';
    import { updateOCRBlocks } from '~/components/ocr/OCRDataUpdater';
    import { isEInk } from '~/helpers/theme';
    import { OCRDocument } from '~/models/OCRDocument';
    import { copyOCRToClipboard } from '~/utils/ui';
    import { colors } from '~/variables';

    let { colorBackground, colorOnBackground } = $colors;
    // technique for only specific properties to get updated on store change
    $: ({ colorBackground, colorOnBackground } = $colors);
    const visualState = isEInk ? colorBackground : 'black';
    const textColor = isEInk ? colorOnBackground : 'white';

    export let document: OCRDocument;
    export let pageIndex: number;
    export let ocrData: OCRData;
    export let imagePath: string;
    export let rotation: number = 0;
    // export let image: ImageSource;
    // export let rotation: number;
    export let imageWidth: number;
    export let imageHeight: number;
    let webView: NativeViewElementNode<AWebView>;

    // console.log('ocrData', JSON.stringify(ocrData));
    function onWebViewLoadFinished() {
        try {
            webView?.nativeView.registerLocalResource('ocrimage', imagePath);
            // iOS seems to that timeout or sometimes the first opening wont show the data
            setTimeout(() => {
                webView?.nativeView.executeJavaScript(`document.updateOCRData('x-local://ocrimage', ${imageWidth}, ${imageHeight}, ${rotation}, ${JSON.stringify(ocrData)})`);
            }, 100);
        } catch (error) {
            showError(error);
        }
    }

    let showTextView = true;
    // const showlabelsOnImage = true;
    let page: NativeViewElementNode<Page>;
    // let canvasView: NativeViewElementNode<CanvasView>;
    // const padding = 20;
    // let drawingRatio: number;
    // const currentImageMatrix: Matrix = new Matrix();
    // let currentPinchPanMatrix: Matrix = new Matrix();
    // const bitmapPaint = new Paint();
    // if (colorMatrix) {
    //     bitmapPaint.setColorFilter(new ColorMatrixColorFilter(colorMatrix));
    // }

    const showWithCustomFontSize = false;

    // ocrData.blocks.forEach((b) => {
    //     console.log(JSON.stringify(b));
    // });

    function toggleShowTextView() {
        if (showTextView) {
            showTextView = false;
        } else {
            showTextView = true;
            // TODO: reset pan/zoom Matrix
        }
    }

    // $: updateMatrix(canvasView);

    let text = ocrData.text;

    $: text = showWithCustomFontSize
        ? createNativeAttributedString({
              spans: ocrData.blocks.map((b) => ({
                  text: b.text + '\n',
                  fontSize: (b.fontSize * ocrData.imageHeight) / 300, //in pixels in image size
                  fontWeight: b.fontWeight,
                  fontStyle: b.fontStyle,
                  textDecoration: b.textDecoration
              }))
          })
        : ocrData.text;
    function copyText() {
        try {
            copyOCRToClipboard(text);
        } catch (error) {
            showError(error);
        }
    }

    // --- helper: levenshtein distance & similarity ---
    function levenshtein(a: string, b: string): number {
        const la = a.length,
            lb = b.length;
        if (la === 0) return lb;
        if (lb === 0) return la;
        const v0 = new Array(lb + 1).fill(0);
        const v1 = new Array(lb + 1).fill(0);
        for (let j = 0; j <= lb; j++) v0[j] = j;
        for (let i = 0; i < la; i++) {
            v1[0] = i + 1;
            for (let j = 0; j < lb; j++) {
                const cost = a[i] === b[j] ? 0 : 1;
                v1[j + 1] = Math.min(v1[j] + 1, v0[j + 1] + 1, v0[j] + cost);
            }
            for (let j = 0; j <= lb; j++) v0[j] = v1[j];
        }
        return v1[lb];
    }

    function similarity(a: string, b: string): number {
        const maxLen = Math.max(a.length, b.length);
        if (maxLen === 0) return 1;
        return 1 - levenshtein(a, b) / maxLen;
    }

    // find best matching substring in 'text' for 'pattern' starting at 'fromIndex'
    function findBestMatch(pattern: string, text: string, fromIndex: number, expectedLen: number) {
        const maxSearchWindow = Math.max(1000, expectedLen * 5); // tune if needed
        const searchStart = fromIndex;
        const searchEnd = Math.min(text.length, fromIndex + maxSearchWindow);
        let best = { idx: -1, len: 0, score: -1, substr: '' };

        // quick exact match first (preserves exact characters/spaces)
        if (pattern && pattern.length > 0) {
            const exactIdx = text.indexOf(pattern, fromIndex);
            if (exactIdx >= 0 && exactIdx <= searchEnd) {
                return { idx: exactIdx, len: pattern.length, score: 1, substr: text.substr(exactIdx, pattern.length) };
            }
        }

        // candidate lengths around expectedLen
        const minLen = Math.max(1, Math.floor(expectedLen * 0.5));
        const maxLen = Math.min(text.length, Math.ceil(expectedLen * 1.5));

        // full search with step=1 to avoid skipping characters
        for (let pos = searchStart; pos <= Math.max(searchStart, searchEnd - minLen); pos++) {
            // remaining too small
            if (searchEnd - pos < minLen) break;
            const limit = Math.min(maxLen, searchEnd - pos);
            for (let L = minLen; L <= limit; L++) {
                const substr = text.substr(pos, L);
                const score = similarity(pattern, substr);
                // prefer higher score, then earlier pos, then length closer to expectedLen
                const quality = score - Math.abs(L - expectedLen) * 1e-4;
                if (quality > best.score + 1e-6 || (Math.abs(quality - best.score) < 1e-6 && (best.idx < 0 || pos < best.idx))) {
                    best = { idx: pos, len: L, score, substr };
                }
            }
        }
        return best;
    }

    // Update blocks from edited full text, preserving boxes. Returns mutated ocrData.
    function updateBlocksFromEditedText(data: any, editedFullText: string) {
        if (!data || !Array.isArray(data.blocks)) return data;

        const origFull = String(data.text || '');
        const edited = String(editedFullText || '');

        const n = origFull.length;
        const m = edited.length;

        // quick path: no change
        if (origFull === edited) {
            // clone blocks to avoid mutating original object unexpectedly
            return { ...data, blocks: data.blocks.map((b) => ({ ...b })), text: edited };
        }

        // Build LCS DP table (sizes (n+1) x (m+1))
        const lcs = new Array(n + 1);
        for (let i = 0; i <= n; i++) {
            lcs[i] = new Array(m + 1).fill(0);
        }
        for (let i = 1; i <= n; i++) {
            const ci = origFull.charCodeAt(i - 1);
            for (let j = 1; j <= m; j++) {
                lcs[i][j] = ci === edited.charCodeAt(j - 1) ? lcs[i - 1][j - 1] + 1 : Math.max(lcs[i - 1][j], lcs[i][j - 1]);
            }
        }

        // backtrack to create mappings
        const origToEdit = new Array(n).fill(-1);
        const editToOrig = new Array(m).fill(-1);
        let i = n,
            j = m;
        while (i > 0 && j > 0) {
            if (origFull.charCodeAt(i - 1) === edited.charCodeAt(j - 1)) {
                origToEdit[i - 1] = j - 1;
                editToOrig[j - 1] = i - 1;
                i--;
                j--;
            } else if (lcs[i - 1][j] >= lcs[i][j - 1]) {
                i--;
            } else {
                j--;
            }
        }

        // precompute nearest mapped edited index for each edited index (left/right)
        const nearestLeft = new Array(m).fill(-1);
        let last = -1;
        for (let k = 0; k < m; k++) {
            if (editToOrig[k] !== -1) last = k;
            nearestLeft[k] = last;
        }
        const nearestRight = new Array(m).fill(-1);
        last = -1;
        for (let k = m - 1; k >= 0; k--) {
            if (editToOrig[k] !== -1) last = k;
            nearestRight[k] = last;
        }

        // assign each edited char to a block index
        const blockCount = data.blocks.length;
        const blockAssigned = new Array(blockCount).fill(0).map(() => []);
        // compute orig index ranges for blocks
        const blockRanges = [];
        let acc = 0;
        for (let b = 0; b < blockCount; b++) {
            const btxt = String(data.blocks[b].text || '');
            const start = acc;
            const end = acc + btxt.length; // exclusive
            blockRanges.push({ start, end });
            acc = end;
        }

        function origIndexToBlock(origIdx: number) {
            // binary searchish because blocks in order; do linear scan (small count)
            for (let bi = 0; bi < blockRanges.length; bi++) {
                if (origIdx >= blockRanges[bi].start && origIdx < blockRanges[bi].end) return bi;
            }
            return -1;
        }

        for (let ej = 0; ej < m; ej++) {
            const oi = editToOrig[ej];
            if (oi !== -1) {
                const bi = origIndexToBlock(oi);
                blockAssigned[bi >= 0 ? bi : blockCount - 1].push(ej);
            } else {
                // inserted char: choose nearest mapped edited index (prefer left)
                const left = nearestLeft[ej];
                const right = nearestRight[ej];
                let chosenBlock = -1;
                if (left !== -1 && right !== -1) {
                    // pick nearer; tie -> left
                    chosenBlock = ej - left <= right - ej ? origIndexToBlock(editToOrig[left]) : origIndexToBlock(editToOrig[right]);
                } else if (left !== -1) {
                    chosenBlock = origIndexToBlock(editToOrig[left]);
                } else if (right !== -1) {
                    chosenBlock = origIndexToBlock(editToOrig[right]);
                } else {
                    // no mapped characters anywhere -> fallback to last block
                    chosenBlock = blockCount - 1;
                }
                if (chosenBlock < 0) chosenBlock = blockCount - 1;
                blockAssigned[chosenBlock].push(ej);
            }
        }

        // build new blocks preserving boxes & metadata, taking assigned edited chars in order
        const newBlocks = [];
        for (let bi = 0; bi < blockCount; bi++) {
            const blk = { ...data.blocks[bi] };
            const assigned = blockAssigned[bi];
            if (assigned.length === 0) {
                blk.text = '';
            } else {
                // assigned indices are increasing because we iterated ej ascending
                const chars = [];
                for (let idx = 0; idx < assigned.length; idx++) {
                    chars.push(edited.charAt(assigned[idx]));
                }
                blk.text = chars.join('');
            }
            newBlocks.push(blk);
        }

        // ensure editedFullText preserved exactly
        const newData = { ...data, blocks: newBlocks, text: edited };
        return newData;
    }

    // Save handler called by UI action bar. Updates ocrData in-place.
    async function saveEdit() {
        try {
            const edited = String(text || '');
            DEV_LOG && console.log('ocrData', edited);
            const updated = updateOCRBlocks(ocrData, edited);
            // ensure updated.text equals editedFullText exactly
            updated.text = edited;
            // update local ocrData and push to webview
            ocrData = updated;
            webView?.nativeView.executeJavaScript(`document.updateOCRData('x-local://ocrimage', ${imageWidth}, ${imageHeight}, ${rotation}, ${JSON.stringify(ocrData)})`);

            await document.updatePage(
                pageIndex,
                {
                    ocrData
                },
                false,
                true,
                true
            );
        } catch (err) {
            showError(err);
        }
    }
</script>

<page bind:this={page} actionBarHidden={true} backgroundColor={visualState} statusBarStyle={isEInk ? undefined : 'dark'}>
    <gridlayout class="pageContent" rows="auto,*">
        <awebview
            bind:this={webView}
            backgroundColor={visualState}
            debugMode={!PRODUCTION}
            displayZoomControls={false}
            mediaPlaybackRequiresUserAction={false}
            normalizeUrls={false}
            row={1}
            src={`~/assets/webpdfviewer/index.html?textColor=${textColor}`}
            webConsoleEnabled={!PRODUCTION}
            on:loadFinished={onWebViewLoadFinished} />

        <textview
            backgroundColor={new Color(visualState).setAlpha(200).hex}
            color={textColor}
            fontSize={16}
            row={1}
            {text}
            variant="none"
            visibility={showTextView ? 'visible' : 'hidden'}
            on:textChange={(e) => (text = e.value)} />
        <CActionBar backgroundColor="transparent" buttonsDefaultVisualState={visualState} modalWindow={true} title={null}>
            <mdbutton class="actionBarButton" color={textColor} text="mdi-content-save" variant="text" visibility={showTextView ? 'visible' : 'hidden'} on:tap={saveEdit} />
            <mdbutton class="actionBarButton" color={textColor} text="mdi-content-copy" variant="text" on:tap={copyText} />
            <mdbutton class="actionBarButton" color={textColor} text="mdi-image-text" variant="text" on:tap={toggleShowTextView} />
        </CActionBar>
    </gridlayout>
</page>
