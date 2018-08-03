class Host():
  def __init__(self, name, url):
    self.name = name
    self.url = url
host_chromium = Host('chromium_git', 'https://chromium.googlesource.com')
host_github = Host('github_git', 'https://github.com')
hosts = [
  host_chromium,
  host_github,
]

class Dep():
  def __init__(self, dest, host, repo, standalone_only, ref):
    self.dest = dest
    self.host = host
    self.repo = repo
    self.standalone_only = standalone_only
    self.ref = ref

dawn_deps = [
  # Dependencies required to use GN/Clang in standalone
  Dep('build', host_chromium, 'chromium/src/build.git', True,
    'b944b99e72923c5a6699235ed858e725db21f81f'),
  Dep('buildtools', host_chromium, 'chromium/buildtools.git', True,
    '94288c26d2ffe3aec9848c147839afee597acefd'),
  Dep('tools/clang', host_chromium, 'chromium/src/tools/clang.git', True,
    'c893c7eec4706f8c7fc244ee254b1dadd8f8d158'),
  # GTest and GMock
  Dep('testing', host_chromium, 'chromium/src/testing.git', True,
    '4d706fd80be9e8989aec5235540e7b46d0672826'),
  # SPIRV-Cross
  Dep('third_party/spirv-cross', host_github, 'Kangz/SPIRV-Cross.git', False,
    '694cad533296df02b4562f4a5a20cba1d1a9dbaf'),
]

def dawn_add_hosts(vars, hosts):
  for host in hosts:
    vars[host.name] = host.url

def dawn_add_deps(deps, dawn_deps):
  for dawn_dep in dawn_deps:
    dep = {
      'url': '{{{0.host.name}}}/{0.repo}@{0.ref}'.format(dawn_dep),
    }
    if dawn_dep.standalone_only:
      dep['condition'] = 'dawn_standalone'
    deps['{dawn_root}/' + dawn_dep.dest] = dep

vars = {
  'dawn_root': '.',
  'dawn_standalone': True,
}
dawn_add_hosts(vars, hosts)

deps = {
}
dawn_add_deps(deps, dawn_deps)

hooks = [
  # Pull clang-format binaries using checked-in hashes.
  {
    'name': 'clang_format_win',
    'pattern': '.',
    'condition': 'host_os == "win" and dawn_standalone',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-clang-format',
                '-s', '{dawn_root}/buildtools/win/clang-format.exe.sha1',
    ],
  },
  {
    'name': 'clang_format_mac',
    'pattern': '.',
    'condition': 'host_os == "mac" and dawn_standalone',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=darwin',
                '--no_auth',
                '--bucket', 'chromium-clang-format',
                '-s', '{dawn_root}/buildtools/mac/clang-format.sha1',
    ],
  },
  {
    'name': 'clang_format_linux',
    'pattern': '.',
    'condition': 'host_os == "linux" and dawn_standalone',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=linux*',
                '--no_auth',
                '--bucket', 'chromium-clang-format',
                '-s', '{dawn_root}/buildtools/linux64/clang-format.sha1',
    ],
  },

  # Pull GN binaries using checked-in hashes.
  {
    'name': 'gn_win',
    'pattern': '.',
    'condition': 'host_os == "win" and dawn_standalone',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-gn',
                '-s', '{dawn_root}/buildtools/win/gn.exe.sha1',
    ],
  },
  {
    'name': 'gn_mac',
    'pattern': '.',
    'condition': 'host_os == "mac" and dawn_standalone',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=darwin',
                '--no_auth',
                '--bucket', 'chromium-gn',
                '-s', '{dawn_root}/buildtools/mac/gn.sha1',
    ],
  },
  {
    'name': 'gn_linux64',
    'pattern': '.',
    'condition': 'host_os == "linux" and dawn_standalone',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--platform=linux*',
                '--no_auth',
                '--bucket', 'chromium-gn',
                '-s', '{dawn_root}/buildtools/linux64/gn.sha1',
    ],
  },

  # Pull the compilers and system libraries for hermetic builds
  {
    'name': 'sysroot_x86',
    'pattern': '.',
    'condition': 'checkout_linux and ((checkout_x86 or checkout_x64) and dawn_standalone)',
    'action': ['python', '{dawn_root}/build/linux/sysroot_scripts/install-sysroot.py',
               '--arch=x86'],
  },
  {
    'name': 'sysroot_x64',
    'pattern': '.',
    'condition': 'checkout_linux and (checkout_x64 and dawn_standalone)',
    'action': ['python', '{dawn_root}/build/linux/sysroot_scripts/install-sysroot.py',
               '--arch=x64'],
  },
  {
    # Update the Windows toolchain if necessary.  Must run before 'clang' below.
    'name': 'win_toolchain',
    'pattern': '.',
    'condition': 'checkout_win and dawn_standalone',
    'action': ['python', '{dawn_root}/build/vs_toolchain.py', 'update', '--force'],
  },
  {
    # Note: On Win, this should run after win_toolchain, as it may use it.
    'name': 'clang',
    'pattern': '.',
    'action': ['python', '{dawn_root}/tools/clang/scripts/update.py'],
    'condition': 'dawn_standalone',
  },
  {
    # Pull rc binaries using checked-in hashes.
    'name': 'rc_win',
    'pattern': '.',
    'condition': 'checkout_win and (host_os == "win" and dawn_standalone)',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-browser-clang/rc',
                '-s', '{dawn_root}/build/toolchain/win/rc/win/rc.exe.sha1',
    ],
  },
]
