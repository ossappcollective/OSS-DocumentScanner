const webpackConfig = require('./webpack.config.js');
const webpack = require('webpack');
const { readdirSync, readFileSync } = require('fs');
const { dirname, isAbsolute, join, relative, resolve } = require('path');
const { BundleAnalyzerPlugin } = require('webpack-bundle-analyzer');
const nsWebpack = require('@akylas/nativescript-webpack');
const CopyPlugin = require('copy-webpack-plugin');
const ForkTsCheckerWebpackPlugin = require('fork-ts-checker-webpack-plugin');
const { sentryWebpackPlugin } = require('@sentry/webpack-plugin');
const TerserPlugin = require('terser-webpack-plugin');
const IgnoreNotFoundExportPlugin = require('./tools/scripts/IgnoreNotFoundExportPlugin');
const Fontmin = require('@nativescript-community/fontmin');
const SpeedMeasurePlugin = require('speed-measure-webpack-plugin');
const WaitPlugin = require('./tools/scripts/WaitPlugin');

const ignoredSvelteWarnings = new Set(['a11y-no-onchange', 'a11y-label-has-associated-control', 'a11y-autofocus', 'illegal-attribute-character']);

function fixedFromCharCode(codePt) {
    if (codePt > 0xffff) {
        codePt -= 0x10000;
        return String.fromCharCode(0xd800 + (codePt >> 10), 0xdc00 + (codePt & 0x3ff));
    } else {
        return String.fromCharCode(codePt);
    }
}
module.exports = (env, params = {}) => {
    Object.keys(env).forEach((k) => {
        if (env[k] === 'false' || env[k] === '0') {
            env[k] = false;
        } else if (env[k] === 'true' || env[k] === '1') {
            env[k] = true;
        }
    });
    if (env.adhoc_sentry) {
        env = Object.assign(
            {},
            {
                production: true,
                sentry: true,
                uploadSentry: true,
                testlog: true,
                devlog: true,
                noconsole: false,
                sourceMap: true,
                uglify: true
            },
            env
        );
    } else if (env.adhoc) {
        env = Object.assign(
            {},
            {
                production: true,
                noconsole: true,
                sourceMap: false,
                uglify: true
            },
            env
        );
    } else if (env.timeline) {
        env = Object.assign(
            {},
            {
                production: true,
                noconsole: false,
                keep_classnames_functionnames: true,
                sourceMap: false,
                uglify: true
            },
            env
        );
    }

    nsWebpack.chainWebpack((config, env) => {
        config.module
            .rule('svelte')
            .use('svelte-loader')
            .tap((options) => {
                options.onwarn = function (warning, onwarn) {
                    return ignoredSvelteWarnings.has(warning.code) || onwarn(warning);
                };
                return options;
            });
        config.when(env.production, (config) => {
            config.module
                .rule('svelte')
                .use('string-replace-loader')
                .loader('string-replace-loader')
                .before('svelte-loader')
                .options({
                    search: 'createElementNS\\("https:\\/\\/svelte.dev\\/docs\\/special-elements#svelte-options"',
                    replace: 'createElementNS(svN',
                    flags: 'gm'
                })
                .end();
        });

        return config;
    });

    env.appComponents = env.appComponents || [];
    // env.appComponents.push('~/android/cameraactivity');
    env.appComponents.push('~/android/activity.android');
    env.appComponents.push('~/android/camera_scan_activity');
    const config = webpackConfig(env, params);
    config.entry.application = '~/android/application.android';
    const {
        // --env.fakeall
        accessibility = true,
        appId,
        appPath,
        appResourcesPath, // --env.noconsole
        devlog,
        fork = true, // --env.sourceMap
        hiddenSourceMap,
        hmr, // --env.hiddenSourceMap
        inlineSourceMap, // --env.theme
        keep_classnames_functionnames = false,
        // --env.timeline
        locale = 'en', // --env.uglify
        noconsole, // --env.accessibility
        playStoreBuild = true, // --env.hmr
        production, // --env.devlog
        profile,
        // --env.profile
        report, // --env.inlineSourceMap
        sentry = false, // --env.production
        sourceMap,
        startOnCam = false, // --env.locale
        theme = 'auto', // --env.playStoreBuild
        timeline,
        uglify,
        // --env.sentry
        uploadSentry = false
    } = env;
    console.log('env', env);
    const mode = production ? 'production' : 'development';
    const platform = env && ((env.android && 'android') || (env.ios && 'ios'));
    const projectRoot = params.projectRoot || __dirname;
    const dist = nsWebpack.Utils.platform.getDistPath();
    const appResourcesFullPath = resolve(projectRoot, appResourcesPath);
    const isIOS = platform === 'ios';
    const isAndroid = platform === 'android';
    const package = require('./package.json');

    config.resolve.conditionNames = config.resolve.conditionNames || ['import', 'require', 'node', 'default'];
    config.resolve.conditionNames.push('svelte');
    if (profile) {
        const StatsPlugin = require('stats-webpack-plugin');

        config.plugins.unshift(
            new StatsPlugin(resolve(join(projectRoot, 'webpack.stats.json')), {
                preset: 'minimal',
                chunkModules: true,
                modules: true,
                usedExports: true
            })
        );
        // config.profile = true;
        // config.parallelism = 1;
        // config.stats = { preset: 'minimal', chunkModules: true, modules: true, usedExports: true };
    }

    const supportedLocales = readdirSync(join(projectRoot, appPath, 'i18n'))
        .filter((s) => s.endsWith('.json'))
        .map((s) => s.replace('.json', ''));
    const supportedColorThemes = readdirSync(join(projectRoot, appPath, 'themes', appId))
        .filter((s) => s.endsWith('.json'))
        .map((s) => s.replace('.json', ''));
    console.log('supportedLocales', supportedLocales);
    config.externals.push('~/licenses.json');
    config.externals.push(function ({ context, request }, cb) {
        if (/i18n$/i.test(context)) {
            return cb(null, join('~/i18n/', request));
        }
        cb();
    });
    supportedLocales.forEach((l) => {
        config.externals.push(`~/i18n/${l}.json`);
    });
    if (isIOS) {
        supportedColorThemes.forEach((l) => {
            config.externals.push(`~/themes/${appId}/${l}.json`);
        });
    }

    // disable resolve of symlinks so that stack dont use real path but node_modules ones
    config.resolve.symlinks = false;
    const coreModulesPackageName = fork ? '@akylas/nativescript' : '@nativescript/core';
    if (fork) {
        config.resolve.modules = [resolve(__dirname, `node_modules/${coreModulesPackageName}`), resolve(__dirname, 'node_modules'), `node_modules/${coreModulesPackageName}`, 'node_modules'];
        Object.assign(config.resolve.alias, {
            '@nativescript/core': `${coreModulesPackageName}`,
            'tns-core-modules': `${coreModulesPackageName}`
        });
    }
    Object.assign(config.resolve.alias, {
        '@shared': resolve(__dirname, 'tools/app'),
        'kiss-orm': '@akylas/kiss-orm',
        'svelte-native': '@nativescript-community/svelte-native'
    });
    let appVersion;
    let buildNumber;
    if (platform === 'android') {
        const gradlePath = resolve(projectRoot, appResourcesPath, 'Android/app.gradle');
        const gradleData = readFileSync(gradlePath, 'utf8');
        appVersion = gradleData.match(/versionName "((?:[0-9]+\.?)+(?:-(?:[a-zA-Z0-9])+)?)"/)[1];
        console.log('appVersion', appVersion);
        buildNumber = gradleData.match(/versionCode ([0-9]+)/)[1];
    } else if (platform === 'ios') {
        const plistPath = resolve(projectRoot, appResourcesPath, 'iOS/Info.plist');
        const plistData = readFileSync(plistPath, 'utf8');
        appVersion = plistData.match(/<key>CFBundleShortVersionString<\/key>[\s\n]*<string>(.*?)<\/string>/)[1];
        buildNumber = plistData.match(/<key>CFBundleVersion<\/key>[\s\n]*<string>([0-9]*)<\/string>/)[1];
    }

    const midFontFamily = platform === 'android' ? 'materialdesignicons-webfont' : 'Material Design Icons';
    const APP_STORE_ID = process.env.IOS_APP_ID;
    const defines = {
        PRODUCTION: !!production,
        process: 'global.process',
        'global.TNS_WEBPACK': 'true',
        __UI_LABEL_USE_LIGHT_FORMATTEDSTRING__: true,
        __UI_USE_EXTERNAL_RENDERER__: true,
        __UI_USE_XML_PARSER__: false,
        __CSS_USE_CSS_TOOLS__: false,
        __ACCESSIBILITY_DEFAULT_ENABLED__: false,
        __ONLY_ALLOW_ROOT_VARIABLES__: true,
        __IOS__: isIOS,
        __ANDROID__: isAndroid,
        'global.autoLoadPolyfills': false,
        TNS_ENV: JSON.stringify(mode),
        __APP_ID__: `"${appId}"`,
        __APP_VERSION__: `"${appVersion}"`,
        __APP_BUILD_NUMBER__: `${parseInt(buildNumber, 10)}`,
        CARD_APP: appId === 'com.akylas.cardwallet',
        SUPPORTED_LOCALES: JSON.stringify(supportedLocales),
        SUPPORTED_COLOR_THEMES: JSON.stringify(supportedColorThemes),
        __INAPP_PURCHASE_ID_PREFIX__: `"1"`,
        FALLBACK_LOCALE: `"${locale}"`,
        DEFAULT_THEME: `"${theme}"`,
        START_ON_CAM: startOnCam,
        SENTRY_ENABLED: !!sentry,
        NO_CONSOLE: noconsole,
        MDI_FONT_FAMILY: `"${midFontFamily}"`,
        GOOGLE_OAUTH_CLIENT_ID: `"${process.env.GOOGLE_OAUTH_CLIENT_ID}"`,
        GOOGLE_OAUTH_CLIENT_SECRET: `"${process.env.GOOGLE_OAUTH_CLIENT_SECRET}"`,
        ONEDRIVE_OAUTH_CLIENT_ID: `"${process.env.ONEDRIVE_OAUTH_CLIENT_ID}"`,
        ONEDRIVE_OAUTH_CLIENT_SECRET: `"${process.env.ONEDRIVE_OAUTH_CLIENT_SECRET}"`,
        SENTRY_DSN: `"${process.env.SENTRY_DSN}"`,
        SENTRY_PREFIX: `"${!!sentry ? process.env.SENTRY_PREFIX : ''}"`,
        GIT_URL: `"${package.repository}"`,
        SUPPORT_URL: `"${package.bugs.url}"`,
        PLAY_STORE_BUILD: playStoreBuild,
        STORE_LINK: `"${isAndroid ? `https://play.google.com/store/apps/details?id=${appId}` : `https://itunes.apple.com/app/id${APP_STORE_ID}`}"`,
        STORE_REVIEW_LINK: `"${isIOS ? `https://itunes.apple.com/app/id${APP_STORE_ID}?action=write-review` : `market://details?id=${appId}`}"`,
        SPONSOR_URL: '"https://github.com/sponsors/farfromrefug"',
        DEV_LOG: !!devlog
    };
    Object.assign(config.plugins.find((p) => p.constructor.name === 'DefinePlugin').definitions, defines);

    const symbolsParser = require('scss-symbols-parser');
    const mdiSymbols = symbolsParser.parseSymbols(readFileSync(resolve(projectRoot, 'node_modules/@mdi/font/scss/_variables.scss')).toString());
    const mdiIcons = JSON.parse(`{${mdiSymbols.variables[mdiSymbols.variables.length - 1].value.replace(/" (F|0)(.*?)([,\n]|$)/g, '": "$1$2"$3')}}`);

    const appSymbols = symbolsParser.parseSymbols(readFileSync(resolve(projectRoot, 'css/variables.scss')).toString());
    const appIcons = {};
    appSymbols.variables
        .filter((v) => v.name.startsWith('$icon-'))
        .forEach((v) => {
            appIcons[v.name.replace('$icon-', '')] = String.fromCharCode(parseInt(v.value.slice(2, -1), 16));
        });

    const scssPrepend = `$appFontFamily: app;
    $mdiFontFamily: ${midFontFamily};
    `;
    const scssLoaderRuleIndex = config.module.rules.findIndex((r) => r.test && r.test.toString().indexOf('scss') !== -1);
    config.module.rules.splice(
        scssLoaderRuleIndex,
        1,
        {
            test: /\.scss$/,
            use: [
                { loader: 'apply-css-loader' },
                {
                    loader: 'css2json-loader',
                    options: { useForImports: true }
                }
            ]
                .concat(
                    !!production
                        ? [
                              {
                                  loader: 'postcss-loader',
                                  options: {
                                      postcssOptions: {
                                          plugins: [
                                              [
                                                  'cssnano',
                                                  {
                                                      preset: 'advanced'
                                                  }
                                              ],
                                              ['postcss-combine-duplicated-selectors', { removeDuplicatedProperties: true }]
                                          ]
                                      }
                                  }
                              }
                          ]
                        : []
                )
                .concat([
                    {
                        loader: 'sass-loader',
                        options: {
                            sourceMap: false,
                            additionalData: scssPrepend
                        }
                    }
                ])
        }
        // {
        //     test: /\.module\.scss$/,
        //     use: [
        //         { loader: 'css-loader', options: { url: false } },
        //         {
        //             loader: 'sass-loader',
        //             options: {
        //                 sourceMap: false,
        //                 additionalData: scssPrepend
        //             }
        //         }
        //     ]
        // }
    );

    const usedMDIICons = [];
    config.module.rules.push({
        // rules to replace mdi icons and not use nativescript-font-icon
        test: /\.(ts|js|scss|css|svelte)$/,
        exclude: /node_modules/,
        use: [
            {
                loader: 'string-replace-loader',
                options: {
                    search: 'mdi-([a-z0-9-_]+)',
                    replace: (match, p1, offset, str) => {
                        if (mdiIcons[p1]) {
                            const unicodeHex = mdiIcons[p1];
                            const numericValue = parseInt(unicodeHex, 16);
                            const character = fixedFromCharCode(numericValue);
                            usedMDIICons.push(numericValue);
                            return character;
                        }
                        return match;
                    },
                    flags: 'g'
                }
            },
            {
                loader: 'string-replace-loader',
                options: {
                    search: '__PACKAGE__',
                    replace: appId,
                    flags: 'g'
                }
            },
            {
                loader: 'string-replace-loader',
                options: {
                    search: 'app-([a-z0-9-_]+)',
                    replace: (match, p1, offset, str) => appIcons[p1] || match,
                    flags: 'g'
                }
            },
        ]
    });
    // we remove default rules
    config.plugins = config.plugins.filter((p) => ['CopyPlugin', 'ForkTsCheckerWebpackPlugin'].indexOf(p.constructor.name) === -1);
    // we add our rules
    const globOptions = { dot: false, ignore: [`**/${relative(appPath, appResourcesFullPath)}/**`] };

    const context = nsWebpack.Utils.platform.getEntryDirPath();
    // folders need to exist (app/fonts, app/fonts/android... ) or it will trigger webpack unwanted changes
    const copyPatterns = [
        { context, from: 'fonts/!(ios|android)/**/*', to: 'fonts/[name][ext]', noErrorOnMissing: true, globOptions },
        { context, from: 'fonts/*', to: 'fonts/[name][ext]', noErrorOnMissing: true, globOptions },
        { context, from: `fonts/${platform}/**/*`, to: 'fonts/[name][ext]', noErrorOnMissing: true, globOptions },
        { context, from: '**/*.jpg', noErrorOnMissing: true, globOptions },
        { context, from: '**/*.png', noErrorOnMissing: true, globOptions },
        { context, from: 'assets/**/*', noErrorOnMissing: true, globOptions },
        { context: 'tools', from: 'assets/**/*', noErrorOnMissing: true, globOptions },
        {
            context,
            from: 'i18n/**/*',
            globOptions,
            transform: !!production
                ? {
                      transformer: (content, path) => Promise.resolve(Buffer.from(JSON.stringify(JSON.parse(content.toString())), 'utf8'))
                  }
                : undefined
        },
        {
            from: 'node_modules/@mdi/font/fonts/materialdesignicons-webfont.ttf',
            to: 'fonts',
            globOptions,
            transform: !!production
                ? {
                      transformer(content, path) {
                          return new Promise((resolve, reject) => {
                              new Fontmin()
                                  .src(content)
                                  .use(Fontmin.glyph({ subset: usedMDIICons }))
                                  .run(function (err, files) {
                                      if (err) {
                                          reject(err);
                                      } else {
                                          resolve(files[0].contents);
                                      }
                                  });
                          });
                      }
                  }
                : undefined
        }
    ];
    config.plugins.unshift(new CopyPlugin({ patterns: copyPatterns }));

    config.plugins.unshift(
        new webpack.ProvidePlugin({
            svN: '@shared/svelteNamespace'
        })
    );

    config.plugins.push(new SpeedMeasurePlugin());

    config.plugins.unshift(
        new webpack.ProvidePlugin({
            setTimeout: [require.resolve(coreModulesPackageName + '/timer/index.' + platform), 'setTimeout'],
            clearTimeout: [require.resolve(coreModulesPackageName + '/timer/index.' + platform), 'clearTimeout'],
            setInterval: [require.resolve(coreModulesPackageName + '/timer/index.' + platform), 'setInterval'],
            clearInterval: [require.resolve(coreModulesPackageName + '/timer/index.' + platform), 'clearInterval'],
            // this breaks svelte-native transitions which uses custom override
            // requestAnimationFrame: [require.resolve(coreModulesPackageName + '/animation-frame'), 'requestAnimationFrame'],
            cancelAnimationFrame: [require.resolve(coreModulesPackageName + '/animation-frame'), 'cancelAnimationFrame']
        })
    );
    config.plugins.push(new webpack.ContextReplacementPlugin(/dayjs[\/\\]locale$/, new RegExp(`(${supportedLocales.map((l) => l.replace('_', '-').toLowerCase()).join('|')}).\js`)));

    // config.optimization.splitChunks.cacheGroups.defaultVendor.test = /[\\/](node_modules|ui-carto|ui-chart|NativeScript[\\/]dist[\\/]packages[\\/]core)[\\/]/;
    config.optimization.splitChunks.cacheGroups.defaultVendor.test = function (module) {
        const absPath = module.resource;
        if (absPath) {
            const relativePath = relative(projectRoot, absPath);
            return absPath.indexOf('node_modules') !== -1 || !(relativePath && !relativePath.startsWith('..') && !isAbsolute(relativePath));
        }
        return false;
    };
    config.plugins.push(new IgnoreNotFoundExportPlugin());

    const nativescriptReplace = '(NativeScript[\\/]dist[\\/]packages[\\/]core|@nativescript/core|@akylas/nativescript)';
    config.plugins.push(
        new webpack.NormalModuleReplacementPlugin(/http/, (resource) => {
            if (resource.context.match(nativescriptReplace) || resource.request === '@nativescript/core/http' || resource.request === '@akylas/nativescript/http') {
                resource.request = '@nativescript-community/https';
            }
        })
    );
    if (fork && production) {
        if (!accessibility) {
            config.plugins.push(
                new webpack.NormalModuleReplacementPlugin(/accessibility$/, (resource) => {
                    if (resource.context.match(nativescriptReplace)) {
                        resource.request = '~/shims/accessibility';
                    }
                })
            );
        }
        config.plugins.push(
            new webpack.NormalModuleReplacementPlugin(/action-bar$/, (resource) => {
                if (resource.context.match(nativescriptReplace)) {
                    resource.request = '~/shims/action-bar';
                }
            })
        );
    }
    // save as long as we dont use calc in css
    config.plugins.push(new webpack.IgnorePlugin({ resourceRegExp: /reduce-css-calc$/ }));
    config.plugins.push(new webpack.IgnorePlugin({ resourceRegExp: /punnycode$/ }));
    config.plugins.push(new webpack.IgnorePlugin({ resourceRegExp: /^url$/ }));

    if (!!production && !timeline) {
        console.log('removing N profiling');
        config.plugins.push(
            new webpack.NormalModuleReplacementPlugin(/profiling$/, (resource) => {
                if (resource.context.match(nativescriptReplace)) {
                    resource.request = '~/shims/profile';
                }
            })
        );
        if (!sentry) {
            config.plugins.push(
                new webpack.NormalModuleReplacementPlugin(/trace$/, (resource) => {
                    if (resource.context.match(nativescriptReplace)) {
                        resource.request = '~/shims/trace';
                    }
                })
            );
        }
        config.module.rules.push(
            {
                // rules to replace mdi icons and not use nativescript-font-icon
                test: /\.(js)$/,
                use: [
                    {
                        loader: 'string-replace-loader',
                        options: {
                            search: /__decorate\(\[((\s|\t|\n)*?)([a-zA-Z]+\.)?profile((\s|\t|\n)*?)\],.*?,.*?,.*?\);?/gm,
                            replace: (match, p1, offset, str) => '',
                            flags: 'gm'
                        }
                    }
                ]
            },
            {
                // rules to replace mdi icons and not use nativescript-font-icon
                test: /\.(ts)$/,
                use: [
                    {
                        loader: 'string-replace-loader',
                        options: {
                            search: '@profile',
                            replace: (match, p1, offset, str) => '',
                            flags: ''
                        }
                    }
                ]
            },
            // rules to clean up all Trace in production
            // we must run it for all files even node_modules
            {
                test: /\.(ts|js)$/,
                use: [
                    {
                        loader: 'string-replace-loader',
                        options: {
                            search: /if\s*\(\s*Trace.isEnabled\(\)\s*\)/gm,
                            replace: (match, p1, offset, str) => 'if (false)',
                            flags: 'g'
                        }
                    }
                ]
            }
        );
    }

    if (!!production) {
        config.plugins.push(
            new ForkTsCheckerWebpackPlugin({
                async: false
            })
        );
    }

    if (report) {
        // Generate report files for bundles content
        config.plugins.push(
            new BundleAnalyzerPlugin({
                analyzerMode: 'static',
                openAnalyzer: false,
                generateStatsFile: true,
                reportFilename: resolve(projectRoot, 'report', 'report.html'),
                statsFilename: resolve(projectRoot, 'report', 'stats.json')
            })
        );
    }

    if (hiddenSourceMap || sourceMap) {
        if (!!sentry) {
            config.devtool = false;
            config.devtool = 'source-map';
            config.plugins.push(
                new webpack.SourceMapDevToolPlugin({
                    append: `\n//# sourceMappingURL=${process.env.SOURCEMAP_REL_DIR}/[name].js.map`,
                    filename: join(process.env.SOURCEMAP_REL_DIR, '[name].js.map')
                })
            );
            if (!!uploadSentry) {
                config.plugins.push(
                    sentryWebpackPlugin({
                        telemetry: false,
                        org: process.env.SENTRY_ORG,
                        url: process.env.SENTRY_URL,
                        project: process.env.SENTRY_PROJECT,
                        authToken: process.env.SENTRY_AUTH_TOKEN,
                        release: {
                            name: `${appId}@${platform}${isAndroid ? (playStoreBuild ? '.playstore' : '.fdroid') : ''}@${appVersion}+${buildNumber}`,
                            dist: `${buildNumber}.${platform}${isAndroid ? (playStoreBuild ? '.playstore' : '.fdroid') : ''}`,
                            setCommits: {
                                auto: true,
                                ignoreEmpty: true,
                                ignoreMissing: true
                            },
                            create: true,
                            cleanArtifacts: true
                        },
                        sourcemaps: {
                            // rewriteSources: (source, map) => source.replace('webpack:///', 'webpack://'),
                            ignore: ['tns-java-classes', 'hot-update'],
                            assets: [join(dist, '**/*.js'), join(dist, process.env.SOURCEMAP_REL_DIR, '*.map')]
                        }
                    })
                );
            }
        } else {
            config.devtool = 'inline-nosources-cheap-module-source-map';
        }
    } else {
        config.devtool = false;
    }
    config.externalsPresets = { node: false };
    config.resolve.fallback = config.resolve.fallback || {};
    // config.resolve.fallback.timers = require.resolve('timers/');
    config.resolve.fallback.stream = false;
    config.resolve.fallback.timers = false;
    config.resolve.fallback.buffer = false;
    config.resolve.fallback.util = require.resolve('util/');
    config.resolve.fallback.path = false;
    config.resolve.fallback.fs = false;
    config.resolve.fallback.assert = false;
    config.resolve.fallback.tty = false;
    config.resolve.fallback.os = false;
    config.optimization.minimize = uglify !== undefined ? !!uglify : production;
    const isAnySourceMapEnabled = !!sourceMap || !!hiddenSourceMap || !!inlineSourceMap;
    const actual_keep_classnames_functionnames = keep_classnames_functionnames || platform !== 'android';
    config.optimization.usedExports = true;
    config.optimization.minimizer = [
        new TerserPlugin({
            parallel: true,
            terserOptions: {
                ecma: 2020,
                module: true,
                toplevel: false,
                keep_classnames: actual_keep_classnames_functionnames,
                keep_fnames: actual_keep_classnames_functionnames,
                output: {
                    comments: false,
                    semicolons: !isAnySourceMapEnabled
                },
                compress: {
                    booleans_as_integers: false,
                    // The Android SBG has problems parsing the output
                    // when these options are enabled
                    collapse_vars: platform !== 'android',
                    sequences: platform !== 'android',
                    passes: 3,
                    drop_console: production && noconsole
                }
            }
        })
    ];
    // return config;

    if (env.adhoc || env.adhoc_sentry) {
        config.plugins.push(new WaitPlugin(join(projectRoot, appPath, 'assets', 'webpdfviewer', 'index.html'), 100, 60000));
    }
    return [require('./webpdfviewer/webpack.config.js')(env), config];
};
