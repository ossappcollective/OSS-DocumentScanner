<script context="module" lang="ts">
    import { Template } from '@nativescript-community/svelte-native/components';
    import { NativeViewElementNode } from '@nativescript-community/svelte-native/dom';
    import { CheckBox } from '@nativescript-community/ui-checkbox';
    import { CollectionView } from '@nativescript-community/ui-collectionview';
    import { Label } from '@nativescript-community/ui-label';
    import { prompt } from '@nativescript-community/ui-material-dialogs';
    import { TextField } from '@nativescript-community/ui-material-textfield';
    import { ObservableArray, View } from '@nativescript/core';
    import { showError } from '@shared/utils/showError';
    import { Writable } from 'svelte/store';
    import ListItemAutoSize from '@shared/components/ListItemAutoSize.svelte';
    import { l, lc } from '~/helpers/locale';
    import { ALERT_OPTION_MAX_HEIGHT } from '~/utils/constants';
    import { createView, pickColor, showAlertOptionSelect, showSliderPopover } from '~/utils/ui';
    import { colors, windowInset } from '~/variables';
</script>

<script lang="ts">
    // technique for only specific properties to get updated on store change
    $: ({ colorOnSurfaceVariant, colorOutline } = $colors);

    let collectionView: NativeViewElementNode<CollectionView>;
    export let store: Writable<any>;
    // let folderPathName = data.folderPathName;
    const variant = 'outline';

    export let items: ObservableArray<any>;

    function getTitle(item) {
        return item.title;
    }
    function getDescription(item) {
        return typeof item.description === 'function' ? item.description(item) : item.description;
    }
    export function updateItem(item, key = 'id') {
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
    let ignoreNextOnCheckBoxChange = false;
    async function onCheckBox(item, event) {
        if (ignoreNextOnCheckBoxChange || item.value === event.value) {
            return;
        }
        const value = event.value;
        item.value = value;
        clearCheckboxTimer();
        try {
            ignoreNextOnCheckBoxChange = true;
            $store[item.key || item.id] = value;
        } catch (error) {
            showError(error);
        } finally {
            ignoreNextOnCheckBoxChange = false;
        }
    }
    async function onTap(item, event) {
        try {
            if (item.type === 'checkbox' || item.type === 'switch') {
                // we dont want duplicate events so let s timeout and see if we clicking diretly on the checkbox
                const checkboxView: CheckBox = ((event.object as View).parent as View).getViewById('checkbox');
                clearCheckboxTimer();
                checkboxTapTimer = setTimeout(() => {
                    checkboxView.checked = !checkboxView.checked;
                }, 10);
                return;
            }
            switch (item.id) {
                case 'store_setting':
                case 'setting': {
                    if (item.type === 'prompt') {
                        const result = await prompt({
                            title: getTitle(item),
                            message: item.useHTML ? item.description : item.full_description || item.description,
                            okButtonText: l('save'),
                            cancelButtonText: l('cancel'),
                            autoFocus: true,
                            textFieldProperties: item.textFieldProperties,
                            defaultText: (typeof item.rightValue === 'function' ? item.rightValue() : item.default) + '',
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
                        DEV_LOG && console.log('prompt result', item.key, item.valueType, result);
                        if (result && !!result.result && result.text.length > 0) {
                            if (item.valueType === 'string') {
                                $store[item.key] = result.text;
                            } else {
                                $store[item.key] = parseInt(result.text, 10);
                            }
                            updateItem(item);
                        }
                    } else if (item.type === 'slider') {
                        DEV_LOG && console.log('showSlidersPopover', event.object, item.currentValue());
                        await showSliderPopover({
                            anchor: event.object,
                            value: item.currentValue(),
                            ...item,
                            onChange(value) {
                                if (item.transformValue) {
                                    value = item.transformValue(value, item);
                                } else {
                                    value = Math.round(value / item.step) * item.step;
                                }
                                if (item.valueType === 'string') {
                                    $store[item.key] = value + '';
                                } else {
                                    $store[item.key] = value;
                                }
                                updateItem(item);
                            }
                        });
                    } else {
                        const result = await showAlertOptionSelect(
                            {
                                height: Math.min(item.values.length * 56, ALERT_OPTION_MAX_HEIGHT),
                                rowHeight: item.autoSizeListItem ? undefined : 56,
                                ...item,
                                options: item.values.map((k) => ({
                                    name: k.title || k.name,
                                    data: k.value,
                                    boxType: 'circle',
                                    type: 'checkbox',
                                    value: (item.currentValue?.() ?? item.currentValue) === k.value
                                }))
                            },
                            {
                                title: item.title,
                                message: item.full_description
                            }
                        );
                        DEV_LOG && console.log('result?.data', result?.data);
                        if (result?.data !== undefined) {
                            if (item.onResult) {
                                item.onResult(result.data);
                            } else {
                                if (item.valueType === 'string') {
                                    $store[item.key] = result?.data;
                                } else {
                                    $store[item.key] = parseInt(result?.data, 10);
                                }
                            }
                            updateItem(item);
                        }
                    }

                    break;
                }
            }
        } catch (err) {
            showError(err);
        }
    }
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

    async function changeColor(item, event) {
        try {
            const newColor = await pickColor($store.color, { anchor: event.object });
            if (newColor) {
                $store.color = newColor.hex;
                updateItem(item, 'type');
            }
        } catch (error) {
            showError(error);
        }
    }
    function onTextChange(e) {
        if (e.object.text) {
            (e.object as TextField).setSelection(e.object.text.length);
        }
    }

    // async function onFolderSelect(item, event) {
    //     item.text = $store.localFolderPath = event.text;
    //     updateItem(item);
    // }
</script>

<collectionview bind:this={collectionView} ios:autoReloadItemOnLayout={true} itemTemplateSelector={selectTemplate} {items} android:paddingBottom={$windowInset.bottom} {...$$restProps}>
    <Template key="sectionHeader" let:item>
        <label class="sectionHeader" text={item.title} />
    </Template>
    <Template key="color" let:item>
        <ListItemAutoSize item={{ subtitle: lc('sync_service_color_desc'), title: lc('color') }} on:tap={(event) => changeColor(item, event)}>
            <absolutelayout backgroundColor={$store.color} borderColor={colorOutline} borderRadius="50%" borderWidth={2} col={1} height={40} marginLeft={10} width={40} />
        </ListItemAutoSize>
    </Template>
    <Template key="textfield" let:item>
        <gridlayout columns="*" margin={5} row={3} rows="auto" on:tap={(e) => item.onTap(item, e)}>
            <textfield isUserInteractionEnabled={false} text={item.text} {variant} {...item.textFieldProperties} on:loaded={onTextChange} />
            <mdbutton
                class="icon-btn"
                color={colorOnSurfaceVariant}
                horizontalAlignment="right"
                isUserInteractionEnabled={false}
                text={item.icon}
                variant="text"
                verticalAlignment="middle"
                visibility={item.icon ? 'visible' : 'hidden'} />
        </gridlayout>
    </Template>
    <Template key="switch" let:item>
        <ListItemAutoSize item={{ ...item, title: getTitle(item), subtitle: getDescription(item) }} on:tap={(event) => onTap(item, event)}>
            <switch id="checkbox" checked={item.value} col={1} marginLeft={10} verticalAlignment="center" on:checkedChange={(e) => onCheckBox(item, e)} />
        </ListItemAutoSize>
    </Template>
    <Template let:item>
        <ListItemAutoSize item={{ ...item, title: getTitle(item), subtitle: getDescription(item) }} showBottomLine={false} on:tap={(event) => onTap(item, event)}></ListItemAutoSize>
    </Template>
    <slot />
</collectionview>
