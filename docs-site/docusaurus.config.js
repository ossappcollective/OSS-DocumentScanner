// @ts-check
// `@type` JSDoc annotations allow editor autocompletion and type checking
// (when paired with `@ts-check`).
// There are various equivalent ways to declare your Docusaurus config.
// See: https://docusaurus.io/docs/api/docusaurus-config

import { themes as prismThemes } from 'prism-react-renderer';

// This runs in Node.js - Don't use client-side code here (browser APIs, JSX...)

/** @type {import('@docusaurus/types').Config} */
const config = {
    title: 'OSS Document Scanner - Best Free PDF Scanner App',
    tagline: 'Free, open source document scanner and card wallet apps for Android & iOS. No ads, 100% privacy.',
    favicon: 'img/favicon.ico',

    // Set the production url of your site here
    url: 'https://www.akylas.fr',
    // Set the /<baseUrl>/ pathname under which your site is served
    // For GitHub pages deployment, it is often '/<projectName>/'
    baseUrl: '/',

    // GitHub pages deployment config.
    // If you aren't using GitHub pages, you don't need these.
    organizationName: 'OSS-DocumentScanner', // Usually your GitHub org/user name.
    projectName: 'OSS-DocumentScanner', // Usually your repo name.
    trailingSlash: false,

    onBrokenLinks: 'throw',

    markdown: {
        format: 'mdx'
    },

    // SEO metadata
    headTags: [
        {
            tagName: 'meta',
            attributes: {
                name: 'keywords',
                content:
                    'document scanner, pdf scanner, free document scanner app, best document scanner android, best document scanner ios, open source scanner, ocr app, scan to pdf, mobile scanner, cardwallet, loyalty card app, barcode scanner'
            }
        },
        {
            tagName: 'meta',
            attributes: {
                name: 'author',
                content: 'Akylas'
            }
        }
    ],

    // Even if you don't use internationalization, you can use this field to set
    // useful metadata like html lang. For example, if your site is Chinese, you
    // may want to replace "en" with "zh-Hans".
    i18n: {
        defaultLocale: 'en',
        locales: ['en'],
        localeConfigs: {
            en: {
                label: 'English',
                htmlLang: 'en-US'
            },
            fr: {
                label: 'Français',
                htmlLang: 'fr-FR'
            },
            es: {
                label: 'Español',
                htmlLang: 'es-ES'
            },
            de: {
                label: 'Deutsch',
                htmlLang: 'de-DE'
            }
        }
    },

    presets: [
        [
            'classic',
            /** @type {import('@docusaurus/preset-classic').Options} */
            ({
                docs: {
                    routeBasePath: '/',
                    sidebarPath: './sidebars.js',
                    // Please change this to your repo.
                    // Remove this to remove the "edit this page" links.
                    editUrl: 'https://github.com/ossdocumentscanner/OSS-DocumentScanner/tree/main/docs-site/'
                },
                blog: false,
                theme: {
                    customCss: './src/css/custom.css'
                }
            })
        ]
    ],

    themeConfig:
        /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
        ({
            // Replace with your project's social card
            image: 'img/docusaurus-social-card.jpg',
            navbar: {
                title: 'OSS Apps',
                logo: {
                    alt: 'OSS Apps Logo',
                    src: 'img/logo.svg'
                },
                items: [
                    {
                        type: 'dropdown',
                        label: 'Document Scanner',
                        position: 'left',
                        items: [
                            {
                                to: '/getting-started',
                                label: 'Getting Started'
                            },
                            {
                                to: '/capture',
                                label: 'Capture'
                            },
                            {
                                to: '/edit-and-enhance',
                                label: 'Edit & Enhance'
                            },
                            {
                                to: '/export',
                                label: 'Export'
                            }
                        ]
                    },
                    {
                        type: 'dropdown',
                        label: 'CardWallet',
                        position: 'left',
                        items: [
                            {
                                to: '/cardwallet/getting-started',
                                label: 'Getting Started'
                            },
                            {
                                to: '/cardwallet/scanning-cards',
                                label: 'Scanning Cards'
                            },
                            {
                                to: '/cardwallet/managing-cards',
                                label: 'Managing Cards'
                            }
                        ]
                    },
                    {
                        to: '/sync-and-backup',
                        label: 'Sync & Backup',
                        position: 'left'
                    },
                    {
                        to: '/settings',
                        label: 'Settings',
                        position: 'left'
                    },
                    {
                        type: 'localeDropdown',
                        position: 'right'
                    },
                    {
                        href: 'https://github.com/ossdocumentscanner/OSS-DocumentScanner',
                        label: 'GitHub',
                        position: 'right'
                    }
                ]
            },
            footer: {
                style: 'dark',
                links: [
                    {
                        title: 'Document Scanner',
                        items: [
                            {
                                label: 'Getting Started',
                                to: '/getting-started'
                            },
                            {
                                label: 'Capture',
                                to: '/capture'
                            },
                            {
                                label: 'Export',
                                to: '/export'
                            }
                        ]
                    },
                    {
                        title: 'CardWallet',
                        items: [
                            {
                                label: 'Getting Started',
                                to: '/cardwallet/getting-started'
                            },
                            {
                                label: 'Scanning Cards',
                                to: '/cardwallet/scanning-cards'
                            },
                            {
                                label: 'Managing Cards',
                                to: '/cardwallet/managing-cards'
                            }
                        ]
                    },
                    {
                        title: 'Community',
                        items: [
                            {
                                label: 'GitHub Issues',
                                href: 'https://github.com/ossdocumentscanner/OSS-DocumentScanner/issues'
                            },
                            {
                                label: 'Weblate Translations',
                                href: 'https://hosted.weblate.org/engage/oss-document-scanner/'
                            },
                            {
                                label: 'Sponsor',
                                href: 'https://github.com/sponsors/farfromrefug'
                            }
                        ]
                    },
                    {
                        title: 'Download',
                        items: [
                            {
                                label: 'GitHub Releases',
                                href: 'https://github.com/ossdocumentscanner/OSS-DocumentScanner/releases'
                            },
                            {
                                label: 'Google Play (Scanner)',
                                href: 'https://play.google.com/store/apps/details?id=com.akylas.documentscanner'
                            },
                            {
                                label: 'Google Play (CardWallet)',
                                href: 'https://play.google.com/store/apps/details?id=com.akylas.cardwallet'
                            },
                            {
                                label: 'App Store (Scanner)',
                                href: 'https://apps.apple.com/us/app/oss-document-scanner/id6472918564'
                            },
                            {
                                label: 'App Store (CardWallet)',
                                href: 'https://apps.apple.com/app/oss-cardwallet/id6504414362'
                            }
                        ]
                    },
                    {
                        title: 'Privacy',
                        items: [
                            {
                                label: 'Privacy policy',
                                href: 'https://www.akylas.fr/privacy'
                            }
                        ]
                    }
                ],
                copyright: `Copyright © ${new Date().getFullYear()} OSS Document Scanner & CardWallet. Built with Docusaurus.`
            },
            prism: {
                theme: prismThemes.github,
                darkTheme: prismThemes.dracula
            },
            colorMode: {
                defaultMode: 'light',
                disableSwitch: false,
                respectPrefersColorScheme: true
            }
        })
};

export default config;
