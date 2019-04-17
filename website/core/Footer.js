/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

const React = require('react');

class Footer extends React.Component {
  docUrl(doc, language) {
    const baseUrl = this.props.config.baseUrl;
    return baseUrl + 'docs/' + (language ? language + '/' : '') + doc;
  }

  pageUrl(doc, language) {
    const baseUrl = this.props.config.baseUrl;
    return baseUrl + (language ? language + '/' : '') + doc;
  }

  render() {
    const currentYear = new Date().getFullYear();
    return (
      <footer className="nav-footer" id="footer">
        <section className="sitemap">
          <a href={this.props.config.baseUrl} className="nav-home">
            {this.props.config.footerIcon && (
              <img
                src={this.props.config.baseUrl + this.props.config.footerIcon}
                alt={this.props.config.title}
                width="66"
                height="58"
              />
            )}
          </a>
          <div>
            <h5>Docs</h5>
            <a href={this.docUrl('install-linux.html', this.props.language)}>
              Getting Started (Linux)
            </a>
            <a href={this.docUrl('install-windows.html', this.props.language)}>
              Getting Started (Windows)
            </a>
            <a href={this.docUrl('install-macos.html', this.props.language)}>
              Getting Started (Mac)
            </a>
            <a href="reference/index.html">
              API Reference
            </a>
          </div>
          <div>
            <h5>Community</h5>
            <a href="https://discordapp.com/invite/w9wRRrF">Starcraft AI Discord</a>
            <a href="https://www.facebook.com/groups/bwapi/">Starcraft AI Facebook group</a>
            <a href="https://github.com/TorchCraft/TorchCraftAI">TorchCraftAI on GitHub</a>
          </div>
          <div>
            <h5>More</h5>
            <a href="https://github.com/TorchCraft/TorchCraft">TorchCraft on GitHub</a>
            <a href="https://github.com/TorchCraft/StarData">StarData on GitHub</a>
            <a href={this.props.config.baseUrl + 'blog'}>Blog</a>
          </div>
        </section>

        <a
          href="https://code.facebook.com/projects/"
          target="_blank"
          rel="noreferrer noopener"
          className="fbOpenSource">
          <img
            src={this.props.config.baseUrl + 'img/oss_logo.png'}
            alt="Facebook Open Source"
            width="170"
            height="45"
          />
        </a>
        <section className="copyright">{this.props.config.copyright}</section>
      </footer>
    );
  }
}

module.exports = Footer;
