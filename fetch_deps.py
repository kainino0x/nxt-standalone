#!/usr/bin/env python2
import os
import sys
import subprocess

DAWN_ROOT = os.path.dirname(os.path.realpath(__file__))

import imp
deps = imp.load_source('deps', os.path.join(DAWN_ROOT, 'DEPS'));

def call(*args, **kwargs):
  try:
    subprocess.check_call(args, **kwargs)
    return True
  except subprocess.CalledProcessError:
    return False

def git(stderr, *args):
  stderr_file = None
  if not stderr:
    stderr_file = open(os.devnull, 'wb')
  return call('git', *args, stderr=stderr_file)
def git_at(stderr, at, *args):
  return git(stderr, '-C', at, *args)

failed = False
for dep in deps.dawn_deps:
  dest = os.path.join(DAWN_ROOT, os.path.normpath(dep.dest))
  try:
    os.makedirs(dest)
  except OSError:
    pass

  print 'Updating ' + dest + ':'
  repo = dep.host.url + '/' + dep.repo
  print '  ' + repo + ' @ ' + dep.ref
  if git(False, 'clone', '--no-checkout', repo, dest):
    git_at(True, dest, 'checkout', dep.ref)
  else:
    if git_at(False, dest, 'diff-index', '--quiet', 'HEAD'):
      git_at(True, dest, 'fetch', '--quiet', repo)
      git_at(True, dest, 'merge', '--ff-only', dep.ref)
    else:
      print '** Skipped update due to uncommitted changes. **'
      failed = True
  print

if failed:
  print '** Some dep was not updated! **'
sys.exit(failed)
