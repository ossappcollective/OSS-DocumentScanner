<script lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import ListItemAutoSize from '@shared/components/ListItemAutoSize.svelte';
    import { openLink } from '~/utils/ui';
    // technique for only specific properties to get updated on store change

    const licences = require('~/licenses.json');

    const items = [
        {
            moduleName: 'OpenCV 4.8.0',
            moduleUrl: 'https://github.com/opencv/opencv'
        },
        {
            moduleName: 'tesseract 5.5.1',
            moduleUrl: 'https://github.com/tesseract-ocr/tesseract'
        },
        {
            moduleName: 'Leptonica 1.85.0',
            moduleUrl: 'https://github.com/DanBloomberg/leptonica'
        },
        {
            moduleName: 'libjpeg v9f',
            moduleUrl: 'http://libjpeg.sourceforge.net'
        },
        {
            moduleName: 'libpng 1.6.48',
            moduleUrl: 'http://www.libpng.org/pub/png/libpng.html'
        },
        {
            moduleName: 'Material Design Icons',
            moduleUrl: 'https://pictogrammers.com/library/mdi/'
        },
        {
            moduleName: 'NativeScript',
            moduleUrl: 'https://github.com/NativeScript/NativeScript'
        }
    ]
        .concat(
            __ANDROID__ && CARD_APP
                ? [
                      {
                          moduleName: 'ZXing-C++',
                          moduleUrl: 'https://github.com/zxing-cpp/zxing-cpp'
                      }
                  ]
                : []
        )
        .concat(
            __ANDROID__
                ? [
                      {
                          moduleName: 'jsoncons',
                          moduleUrl: 'https://github.com/danielaparker/jsoncons'
                      }
                  ]
                : []
        )
        .concat(licences.dependencies);

    function onTap(item) {
        if (item.moduleUrl) {
            openLink(item.moduleUrl);
        }
    }
</script>

<gesturerootview rows="auto">
    <collectionView id="trackingScrollView" height="300" ios:contentInsetAdjustmentBehavior={2} itemIdGenerator={(item, i) => i} {items}>
        <Template let:item>
            <ListItemAutoSize item={{ itle: item.moduleName, subtitle: item.moduleUrl }} on:tap={() => onTap(item)} />
        </Template>
    </collectionView>
</gesturerootview>
