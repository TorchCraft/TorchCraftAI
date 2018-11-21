#!/usr/bin/env python3
"""
Copyright (c) 2017-present, Facebook, Inc.
 

This source code is licensed under the MIT license found in the

LICENSE file in the root directory of this source tree.
"""

import argparse
import os
import shlex
import subprocess
import sys
import tempfile
import glob
import random
import itertools

from os import path

sys.path.append(path.join(os.getcwd(), 'slurm'))
from slurmutils import workspace, job

parser = argparse.ArgumentParser('Slurm submission for BOS sample collection')
parser.add_argument('-i', '--image', type=str,
        help='Use this image instead of reusing the workspace one')
parser.add_argument('-w', '--workspace', type=str, default=os.getcwd(), help='Workspace directory')
parser.add_argument('-p', '--partition', type=str, default='scavenge',
        help='slurm partition')
parser.add_argument('-r', '--reservation', type=str, help='slurm reservation')
parser.add_argument('-n', '--num-jobs', type=int, default=1000, help='Number of jobs')
parser.add_argument('-j', '--num-parallel-jobs', type=int, default=1000, help='Max number of jobs running at once')
parser.add_argument('-J', '--job-name', type=str, default='bos-rlv3-samples')
args = parser.parse_args()

array = [''] * args.num_jobs

binary = '/workspace/build/experimental/bo-switch/bos-train-supervised-online'
cmd = [binary]
cmd += '-mode online -v -1 -vmodule bos-train-supervised-online=1'.split()
cmd += '-bos_model_type idle'.split()
cmd += '-playoutput /workspace/playoutput'.split() # XXX hack for using local scratch
cmd += '-num_game_threads 12'.split()
cmd += '-bandit ucb1rolling -strategy training'.split()

job_name = args.job_name
images = workspace.create_snapshot(job_name=job_name, workspace=args.workspace,
        workspace_image=args.image)
jobdir = job.create_jobdir(images, job_name=job_name)
print('>> Job directory: {}'.format(jobdir))

sbatch_args = {
    'partition': args.partition,
    'time': '24:00:00',
    'nodes': '1',
    'ntasks-per-node': 1,
    'cpus-per-task': 20,
    'mem-per-cpu': '6G',
    'job-name': job_name,
    'output': '{}/%a/%A_%a.out'.format(jobdir),
    'error': '{}/%a/%A_%a.out'.format(jobdir),
}
if args.reservation:
    sbatch_args['reservation'] = args.reservation
sbatch_script_path = job.create_sbatch_script_array(array=array, jobdir=jobdir,
    images=images, cmd_args=cmd, sbatch_args=sbatch_args, link_aux_dlls=True,
    restart=False)
sbatch_cmd = ['sbatch',
        '--workdir', jobdir,
        '--array', '1-{}%{}'.format(len(array), args.num_parallel_jobs), sbatch_script_path]

print('>> Exec {}'.format(' '.join(sbatch_cmd)))
subprocess.check_call(sbatch_cmd)

