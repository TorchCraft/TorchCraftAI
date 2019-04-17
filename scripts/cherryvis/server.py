#!/usr/bin/env python
"""
Copyright (c) 2017-present, Facebook, Inc.

This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
"""

import os, sys
import argparse

def start_cherryvis_server(listen_addr, **kwargs):
    os.environ.setdefault("DJANGO_SETTINGS_MODULE", "cherryvis.settings")
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    import django
    from django.conf import settings
    from django.core.management import call_command
    from django.core.wsgi import get_wsgi_application
    settings.REPLAYS_FILES_GLOB = kwargs.get('REPLAYS_FILES_GLOB', '')
    settings.REPLAY_ADDITIONAL_FILES = kwargs.get('REPLAY_ADDITIONAL_FILES', [])
    settings.INDEX_REDIRECT_TO = kwargs.get('INDEX_REDIRECT_TO', '')
    django.setup()
    application = get_wsgi_application()
    call_command('runserver', listen_addr)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Starts a CherryPi visualization webserver.')
    sample_replays_pattern = os.path.join(
      os.path.dirname(os.path.abspath(__file__)),
      'replays', 'samples', '*.rep'
    )
    parser.add_argument('--replays_pattern', type=str,
                        default=sample_replays_pattern,
                        help='Where are the replays - search pattern. Supports recursive search (a/**/*.rep)')
    parser.add_argument('--host', dest='host_port', default="127.0.0.1:8770",
                        help='Listen address/port for the server')

    args = parser.parse_args()
    start_cherryvis_server(
        listen_addr=args.host_port,
        REPLAYS_FILES_GLOB=args.replays_pattern,
    )
