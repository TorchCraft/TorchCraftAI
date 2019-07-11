"""
Copyright (c) 2017-present, Facebook, Inc.

This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
"""
import glob, os, time
import zstd, json
from django.shortcuts import render
from django.http import HttpResponse, JsonResponse, Http404, FileResponse
from django.conf import settings

def get_replay_values(rep_file, cvis_dir, pattern=None):
    display_name = rep_file
    if pattern is not None:
        # Try to shorten files by removing prefix already in pattern
        display_parts = os.path.normpath(display_name).split(os.sep)
        pattern_parts = os.path.normpath(pattern).split(os.sep)
        for i, d in enumerate(display_parts):
            if i < len(pattern_parts) and pattern_parts[i] == d:
                continue
            if i == 0:
                break
            display_name = '/'.join(display_parts[i:])
            break
    return {
        'display_name': display_name,
        'path': rep_file,
        'abspath': os.path.abspath(rep_file),
        'cvis': cvis_dir,
        'name': os.path.basename(rep_file),
        'has_cvis_data': cvis_dir != '',
        'last_change': os.stat(rep_file).st_mtime,
        'local_time': time.time(),
    }

def is_valid_replay(path):
    return os.path.splitext(path)[1] == '.rep'

def get_cvis_file(cvis_path, file):
    if ('.cvis' in os.path.basename(cvis_path)
      and os.path.isdir(cvis_path)):
        return os.path.join(cvis_path, os.path.basename(file))
    return None

def get_all_available_replays(pattern=''):
    if pattern == '':
      pattern = settings.REPLAYS_FILES_GLOB
    all_files_names = set(glob.iglob(pattern, recursive=True))
    for f in settings.REPLAY_ADDITIONAL_FILES:
        if f['rep'] in all_files_names:
            all_files_names.remove(f['rep'])
    all_files = []
    for filename in all_files_names:
        if is_valid_replay(filename):
            cvis_dir_name = filename + '.cvis'
            if not os.path.isdir(cvis_dir_name):
                cvis_dir_name = ''
            all_files.append(get_replay_values(
                rep_file=filename, cvis_dir=cvis_dir_name, pattern=pattern
            ))
    for f in settings.REPLAY_ADDITIONAL_FILES:
        all_files.append(get_replay_values(
            rep_file=f['rep'], cvis_dir=f['cvis']
        ))
    return {
      'replays': all_files,
      'pattern': pattern,
    }

def index(request):
    template_vars = {
        'REPLAYS_FILES_GLOB': settings.REPLAYS_FILES_GLOB,
        'rep': request.GET.get('rep', ''),
    }
    return render(request, 'replays/index.html', template_vars)

def list_replays(request):
    return JsonResponse(
      get_all_available_replays(request.GET.get('pattern', '')),
      safe=False
    )

def get_replay(request):
    path = request.GET.get('rep', '')
    if path == '' or not os.path.isfile(path) or not is_valid_replay(path):
        raise Http404("No such replay: %s" % path)
    return FileResponse(open(path, 'rb'))

def get_cvis(request):
    file = get_cvis_file(
        request.GET.get('cvis', ''),
        request.GET.get('f', 'trace.json')
    )
    if file is None:
        raise Http404("No such file")
    fh = open(file, 'rb')
    cctx = zstd.ZstdDecompressor()
    reader = cctx.stream_reader(fh)
    if not file.endswith('.zstd.stream'):
        return FileResponse(reader)

    result = {}
    decoder = json.JSONDecoder()
    data = reader.read().decode('ascii')
    while len(data) > 0:
        it, end = decoder.raw_decode(data)
        data = data[end:]
        result[it['key']] = it['value']
    return JsonResponse(result)

def get_replay_info(request):
    rep = request.GET.get('rep', '')
    if not is_valid_replay(rep):
        return Http404("No such file")
    rep += '.cvis.'
    multis = list(glob.glob(rep + '*'))
    multis = {
        f[len(rep):]: f for f in multis
    }
    return JsonResponse({
        'multi': multis
    })
