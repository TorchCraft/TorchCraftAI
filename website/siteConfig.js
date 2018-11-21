/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// See https://docusaurus.io/docs/site-config.html for all the possible
// site configuration options.

const baseUrl = '/TorchCraftAI/'

const siteConfig = {
  title: 'TorchCraftAI' /* title for your website */,
  url: 'https://torchcraft.github.io',
  baseUrl: baseUrl,
  // For github.io type URLs, you would set the url and baseUrl like:
  //   url: 'https://facebook.github.io',
  //   baseUrl: '/test-site/',
  tagline: 'A bot platform for machine learning research on StarCraft®: Brood War®',

  repoUrl: 'https://github.com/TorchCraft/TorchCraftAI',

  // Used for publishing and more
  projectName: 'TorchCraftAI',
  organizationName: 'facebook',
  disableTitleTagline: true,
  disableHeaderTitle: true,
  // For top-level user or org sites, the organization is still the same.
  // e.g., for the https://JoelMarcey.github.io site, it would be set like...
  //   organizationName: 'JoelMarcey'

  // For no header links in the top nav bar -> headerLinks: [],
  headerLinks: [
    {doc: 'bptut-intro', label: 'Tutorials'},
    {
        href: baseUrl + "reference/index.html", // TODO absolute from github.io when migrating to that
        label: 'API'
    },
    {blog: true, label: 'Blog'},
    {
        href: 'https://github.com/TorchCraft/TorchCraftAI',
        label: 'GitHub',
        external: true
    },
  ],

  // If you have users set above, you add it here:
  //users,

  /* path to images for header/footer */
  headerIcon: 'img/tclogosqlightgrey.png',
  footerIcon: 'img/tclogosqsmall.png',
  favicon: 'img/favicon/favicon.ico',

  /* colors for website */
  colors: {
    secondaryColor: '#606060',
    primaryColor: '#182337',  // color of the logo
  },

  /* custom fonts for website */
  /*fonts: {
    myFont: [
      "Times New Roman",
      "Serif"
    ],
    myOtherFont: [
      "-apple-system",
      "system-ui"
    ]
  },*/

  // This copyright info is used in /core/Footer.js and blog rss/atom feeds.
  copyright:
    'Copyright © ' +
    new Date().getFullYear() +
    ' Facebook',

  highlight: {
    // Highlight.js theme to use for syntax highlighting in code blocks
    theme: 'default',
  },

  // Add custom scripts here that would be placed in <script> tags
  scripts: [
    'https://buttons.github.io/buttons.js',
    '/js/code-blocks-buttons.js',
    'https://cdnjs.cloudflare.com/ajax/libs/mathjax/2.7.5/MathJax.js?config=TeX-MML-AM_CHTML'
  ],

  /* On page navigation for the current documentation page */
  onPageNav: 'separate',

  /* Open Graph and Twitter card images */
  opImage: 'img/tclogolinesmall.png',
  twitterImage: 'img/tclogolinesmall.png',

  // You may provide arbitrary config keys to be used as needed by your
  // template. For example, if you need your repo's URL...
  //   repoUrl: 'https://github.com/facebook/test-site',
};

module.exports = siteConfig;
