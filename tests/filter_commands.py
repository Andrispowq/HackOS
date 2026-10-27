#!/usr/bin/env python3
"""Run the real six filter programs with hosted syscall transport and sanitizers."""
import argparse
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
from common import ROOT, OUT, Report, command, require_success

PROGRAMS = ('grep', 'wc', 'head', 'tail', 'tee', 'more')


def test(sanitizers):
    directory = OUT / ('filter-commands' if sanitizers == 'address,undefined' else 'filter-commands-ubsan')
    shutil.rmtree(directory, ignore_errors=True)
    report = Report('filter-commands', directory)
    compiler = os.environ.get('CXX', 'clang++')
    flags = ['-std=c++17', '-g', '-O1', '-fno-omit-frame-pointer', '-fno-builtin',
             '-fsanitize=' + sanitizers, '-fno-sanitize-recover=all']
    user_flags = ['-include', ROOT / 'tests/host/user_rename.h', '-I', ROOT / 'include', '-I', ROOT / 'common/include']

    def build():
        objects = []
        sources = ('tests/host/filters_native.cpp', 'tests/host/filters_adapter.cpp',
                   'libs/libc/src/user.cpp', 'common/src/string_ops.cpp')
        for source in sources:
            obj = directory / (Path(source).stem + '.o')
            native = source.endswith('filters_native.cpp')
            result = command([compiler, *flags, *([] if native else user_flags), '-c', ROOT / source, '-o', obj])
            require_success(result, source)
            objects.append(obj)
        for program in PROGRAMS:
            obj = directory / (program + '.o')
            require_success(command([compiler, *flags, *user_flags, '-Dmain=FilterMain', '-c',
                                     ROOT / 'libs/commands/src' / (program + '.cpp'), '-o', obj]), program)
            require_success(command([compiler, *flags, obj, *objects, '-o', directory / program]), 'link ' + program)
        return command([compiler, '--version']).stdout

    report.check('build', build)
    if report.results[-1]['status'] != 'pass':
        return report.finish()

    def run(program, args=(), data=b'', status=0, expected=None, env=None):
        settings = dict(os.environ, LC_ALL='C', ASAN_OPTIONS='detect_leaks=1:halt_on_error=1')
        settings.update(env or {})
        result = subprocess.run([directory / program, *map(str, args)], input=data,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=directory,
                                env=settings, timeout=10)
        assert result.returncode == status, (program, args, result.returncode, result.stderr)
        if expected is not None:
            assert result.stdout == expected, (program, args, result.stdout[:300], expected[:300])
        assert b'runtime error:' not in result.stderr and b'Sanitizer' not in result.stderr, result.stderr
        return result

    data = b'Alpha one\nbeta TWO\nalpha three\nlast'
    (directory / 'one').write_bytes(data)
    (directory / 'two').write_bytes(b'other\n')
    (directory / '-dash').write_bytes(b'dash\n')

    def basic():
        run('grep', ['-in', 'alpha'], data, expected=b'1:Alpha one\n3:alpha three\n')
        run('grep', ['-v', '-F', 'alpha'], data, expected=b'Alpha one\nbeta TWO\nlast\n')
        run('grep', ['missing'], data, status=1, expected=b'')
        run('grep', ['-c', 'a', 'one', 'two'], expected=b'one:4\ntwo:0\n')
        run('grep', ['-l', 'a', 'one', 'two'], expected=b'one\n')
        run('grep', ['-q', 'a'], data, expected=b'')
        run('grep', ['-Hn', 'last'], data, expected=b'(standard input):4:last\n')
        run('grep', ['-e', '-x'], b'-x\nx\n', expected=b'-x\n')
        run('grep', ['-F', '[x].*'], b'[x].*\nx\n', expected=b'[x].*\n')
        run('grep', [''], b'\nabc\nlast', expected=b'\nabc\nlast\n')
        run('grep', ['-F', 'a'], b'\0a\xff\n', expected=b'\0a\xff\n')
        run('wc', data=data, expected=b'3 7 35\n')
        run('wc', ['-wl'], data, expected=b'3 7\n')
        run('wc', ['-c', 'one', 'two'], expected=b'35 one\n6 two\n41 total\n')
        run('wc', ['-l'], b'last', expected=b'0\n')
        run('wc', ['-w'], b'a\tb\nc\rd\ve\ff g\0h', expected=b'7\n')
        run('head', ['-n', '2'], data, expected=b'Alpha one\nbeta TWO\n')
        run('head', ['-c5'], data, expected=b'Alpha')
        run('tail', ['-n2'], data, expected=b'alpha three\nlast')
        run('tail', ['-c4'], data, expected=b'last')
        run('tail', ['-n', '+2'], data, expected=b'beta TWO\nalpha three\nlast')
        run('tail', ['-c', '+7'], data, expected=data[6:])
        for program in ('head', 'tail', 'more'):
            run(program, data=b'\x00\xffx\nlast', expected=b'\x00\xffx\nlast')
        run('head', ['-qn1', '-', '-', 'two', '-'], data, expected=b'Alpha one\nbeta TWO\nother\nalpha three\n')
        for program in ('head', 'tail'):
            run(program, ['--', '-dash'], expected=b'dash\n')
            run(program, ['-n0'], data, expected=b'')
            run(program, ['-n1', 'two', 'two'], expected=b'==> two <==\nother\n\n==> two <==\nother\n')
            run(program, ['-q', 'two', 'two'], expected=b'other\nother\n')
            run(program, ['-v', 'two'], expected=b'==> two <==\nother\n')
        for program in PROGRAMS:
            run(program, ['--help'])
            run(program, ['--bad'], status=2)
            run(program, expected=b'0 0 0\n' if program == 'wc' else b'',
                args=['x'] if program == 'grep' else [], status=1 if program == 'grep' else 0)

    report.check('options-and-bytes', basic)

    def differential():
        rng = random.Random(1820)
        for _ in range(100):
            source = bytes(rng.choice(b'abc \t\r\n\x00\xff') for _ in range(rng.randrange(5000)))
            count = rng.randrange(25)
            parts = source.split(b'\n')
            if parts[-1] == b'':
                parts.pop()
            lines = [part + (b'\n' if i < source.count(b'\n') else b'') for i, part in enumerate(parts)]
            run('head', ['-n', count], source, expected=b''.join(lines[:count]))
            run('tail', ['-n', count], source, expected=b''.join(lines[-count:]) if count else b'')
            run('head', ['-c', count], source, expected=source[:count])
            run('tail', ['-c', count], source, expected=source[-count:] if count else b'')
            words = len(re.findall(rb'[^ \t\n\r\v\f]+', source))
            run('wc', data=source, expected=f'{source.count(bytes([10]))} {words} {len(source)}\n'.encode())
        return '100 deterministic binary streams; 500 independent output comparisons.'

    report.check('random-streams', differential)

    def patterns():
        rng = random.Random(1821)
        patterns = ['^$', '^a.*b$', 'a*b*', '[a-c]*x', '[^a-c]', '[]-]', r'\.', r'a\*b', '.*a.*a.*b',
                    'a*a*a*a*a*a*a*a*b', '^a', 'b$', '[A-Z]']
        atoms = ['a', 'b', '.', '[a-c]', '[^ab]', '[0-9]', r'\.', 'x']
        for _ in range(100):
            patterns.append(('^' if rng.randrange(2) else '') + ''.join(
                rng.choice(atoms) + ('*' if rng.randrange(3) == 0 else '') for _ in range(rng.randrange(1, 8)))
                           + ('$' if rng.randrange(2) else ''))
        source = b'\n'.join(bytes(rng.choice(b'abBcCx012 .\x00') for _ in range(rng.randrange(80))) for _ in range(100))
        for pattern in patterns:
            for insensitive in (False, True):
                compiled = re.compile(pattern.encode(), re.ASCII | (re.I if insensitive else 0))
                chosen = [line for line in source.split(b'\n') if compiled.search(line)]
                run('grep', [*(['-i'] if insensitive else []), pattern], source,
                    status=0 if chosen else 1, expected=b''.join(line + b'\n' for line in chosen))
        for pattern in ('[abc', '[z-a]', '[[:digit:]]', '\\', r'\(a\)', r'a\{2\}', r'\1', 'a**', 'x' * 129, 'a\nb', '[a\nb]'):
            run('grep', [pattern], b'abc\n', status=2)
        return 'BRE subset checked against Python regex across 228 pattern/case combinations.'

    report.check('pattern-state-machine', patterns)

    def bounds():
        run('grep', ['x'], b'x' * 65536 + b'\n', expected=b'x' * 65536 + b'\n')
        run('grep', ['x'], b'x' * 65537 + b'\n', status=2, expected=b'')
        # A large discarded prefix must not cause a false tail overflow.
        source = b'x' * 300000 + b'\na\nb\nlast'
        run('tail', ['-n3'], source, expected=b'a\nb\nlast')
        run('tail', ['-n4'], source, status=1, expected=b'')
        run('tail', ['-n1'], b'a' * 131072, expected=b'a' * 131072)
        run('tail', ['-n1'], b'a' * 131073, status=1)
        run('tail', ['-n1'], b'prefix\n' + b'a' * 131071 + b'\n', expected=b'a' * 131071 + b'\n')
        run('tail', ['-c131072'], source, expected=source[-131072:])
        run('tail', ['-c131073'], source, status=1)
        run('tail', ['-c131073'], b'abc', expected=b'abc')
        for program in ('head', 'tail'):
            for value in ('', '-1', '1x', '18446744073709551616'):
                run(program, ['-n', value], status=2)
            run(program, ['-n', '18446744073709551615'], b'x', expected=b'x')
        for program in PROGRAMS:
            args = ['x'] if program == 'grep' else []
            status = 2 if program == 'grep' else 1
            run(program, args, b'xyz\n', status=status, env={'FILTER_READ_FAIL': '0'})
            run(program, args, b'xyz\n', status=status, env={'FILTER_WRITE_FAIL': '1'})
        for program in ('grep', 'wc', 'head', 'tail', 'more'):
            args = ['a'] if program == 'grep' else []
            run(program, args + ['one'], status=2 if program == 'grep' else 1, env={'FILTER_CLOSE_FAIL': '3'})
        run('grep', ['x', 'missing'], status=2)
        run('grep', ['-q', 'a', 'missing', 'one'], status=0)
        run('wc', ['missing', 'two'], status=1, expected=b'1 1 6 two\n1 1 6 total\n')

    report.check('bounds-and-errors', bounds)

    def short_io():
        source = bytes(range(256)) * 33
        env = {'FILTER_READ_CHUNK': '7', 'FILTER_WRITE_CHUNK': '3'}
        for program in ('head', 'tail', 'more', 'tee'):
            args = ['-c', len(source)] if program in ('head', 'tail') else []
            run(program, args, source, expected=source, env=env)
        run('wc', ['-c'], source, expected=f'{len(source)}\n'.encode(), env=env)
        run('grep', ['-in', 'alpha'], data, expected=b'1:Alpha one\n3:alpha three\n', env=env)
        for program in PROGRAMS:
            read_fd, write_fd = os.pipe()
            os.close(read_fd)
            try:
                args = ['x'] if program == 'grep' else []
                result = subprocess.run([directory / program, *args], input=b'x\n', stdout=write_fd,
                                        stderr=subprocess.PIPE, timeout=10)
                assert result.returncode == (2 if program == 'grep' else 1), (program, result)
                assert result.stderr == b'', (program, result.stderr)
            finally:
                os.close(write_fd)

    report.check('partial-io-and-broken-pipes', short_io)

    def tee():
        source = bytes(range(256)) * 9
        run('tee', ['out1', 'out2'], source, expected=source)
        assert (directory / 'out1').read_bytes() == source
        assert (directory / 'out2').read_bytes() == source
        run('tee', ['-a', 'out1'], b'extra', expected=b'extra')
        assert (directory / 'out1').read_bytes() == source + b'extra'
        run('tee', ['out1'], b'new', expected=b'new')
        assert (directory / 'out1').read_bytes() == b'new'
        run('tee', ['missing/dir', 'out2'], b'kept', status=1, expected=b'kept')
        assert (directory / 'out2').read_bytes() == b'kept'
        run('tee', ['out1'], b'flush', status=1, env={'FILTER_CLOSE_FAIL': '3'})
        run('tee', ['out1', 'out2'], b'survives', status=1, env={'FILTER_WRITE_FAIL': '3'})
        assert (directory / 'out2').read_bytes() == b'survives'
        run('tee', ['--', '-'], b'literal', expected=b'literal')
        assert (directory / '-').read_bytes() == b'literal'

    report.check('tee-files-and-failures', tee)

    def paging():
        source = b'one\ntwo\nthree\nfour\nfive\nsix\nseven\n'
        prompt = b'--More--\r' + b' ' * 8 + b'\r'
        run('more', data=source, expected=b'one\ntwo\nthree\n' + prompt,
            env={'FILTER_TERMINAL': 'foreground', 'FILTER_KEYS': 'q'})
        run('more', data=source, expected=b'one\ntwo\nthree\n' + prompt + b'four\n' + prompt,
            env={'FILTER_TERMINAL': 'foreground', 'FILTER_KEYS': '\nq'})
        run('more', data=source, expected=b'one\ntwo\nthree\n' + prompt + b'four\nfive\nsix\n' + prompt + b'seven\n',
            env={'FILTER_TERMINAL': 'foreground', 'FILTER_KEYS': '  '})
        run('more', ['-n', '1'], b'last\n', expected=b'last\n', env={'FILTER_TERMINAL': 'foreground'})
        run('more', ['-n1'], b'last\n', expected=b'last\n', env={'FILTER_TERMINAL': 'foreground'})
        run('more', ['-n', '1'], b'a' * 40, expected=b'a' * 16 + prompt,
            env={'FILTER_TERMINAL': 'foreground', 'FILTER_KEYS': 'q'})
        run('more', data=b'a\t\x01\x7f\xff\n', expected=b'a   ^A^??\n', env={'FILTER_TERMINAL': 'foreground'})
        run('more', data=source, status=1, expected=b'', env={'FILTER_TERMINAL': 'background'})
        for value in ('0', '-1', '10001', 'x'):
            run('more', ['-n', value], status=2)

    report.check('pager-keys-wrap-and-redirection', paging)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitizers', default='address,undefined', choices=('address,undefined', 'undefined'))
    args = parser.parse_args()
    sys.exit(test(args.sanitizers))
