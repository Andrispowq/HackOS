#!/usr/bin/env python3
"""Run the real filter programs with hosted syscall transport and sanitizers."""
import argparse
from decimal import Decimal
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
from common import ROOT, OUT, Report, command, require_success

PROGRAMS = ('grep', 'wc', 'head', 'tail', 'tee', 'more', 'sort', 'uniq', 'cut', 'tr')


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
        # Apple's ASan has no LeakSanitizer; retain leak checks on Linux CI.
        leaks = 0 if sys.platform == 'darwin' else 1
        settings = dict(os.environ, LC_ALL='C', ASAN_OPTIONS=f'detect_leaks={leaks}:halt_on_error=1')
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

    def default_args(program):
        return {'grep': ['x'], 'cut': ['-b1-'], 'tr': ['a-z', 'A-Z']}.get(program, [])

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
                args=default_args(program), status=1 if program == 'grep' else 0)

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

    def ordering():
        source = b'z\nBeta\nalpha\nBeta\nALPHA\n\nlast'
        run('sort', data=source, expected=b'\nALPHA\nBeta\nBeta\nalpha\nlast\nz\n')
        run('sort', ['-rfu'], source, expected=b'z\nlast\nBeta\nalpha\n\n')
        run('sort', ['-b'], b'  z\n a\na\n\tB\n', expected=b'\tB\n a\na\n  z\n')
        numbers = b'10\n2\n-2.1\n-2.01\n0002\n.09\n-.001\n0\n-0\nword\n+1\n1e3\n'
        run('sort', ['-n'], numbers,
            expected=b'-2.1\n-2.01\n-.001\n+1\n-0\n0\nword\n.09\n1e3\n0002\n2\n10\n')
        run('sort', ['-nru'], numbers, expected=b'10\n2\n1e3\n.09\n0\n-.001\n-2.01\n-2.1\n')
        run('sort', ['-n'], b'999999999999999999999999\n1000000000000000000000000\n.00000000000000000002\n.00000000000000000001\n',
            expected=b'.00000000000000000001\n.00000000000000000002\n999999999999999999999999\n1000000000000000000000000\n')
        run('sort', ['two', '-', 'one', '-'], b'first', expected=b'Alpha one\nalpha three\nbeta TWO\nfirst\nlast\nother\n')
        run('sort', ['--', '-dash'], expected=b'dash\n')
        run('sort', ['missing', 'one'], status=1, expected=b'')
        repeated = b'a\na\nb\na\nA\nA'
        run('uniq', data=repeated, expected=b'a\nb\na\nA\n')
        run('uniq', ['-ci'], repeated, expected=b'      2 a\n      1 b\n      3 a\n')
        run('uniq', ['-d'], repeated, expected=b'a\nA\n')
        run('uniq', ['-u'], repeated, expected=b'b\na\n')
        run('uniq', ['-du'], repeated, expected=b'')
        run('uniq', ['-f1', '-s1', '-w2'], b'one abX\ntwo abY\nthree acY\n', expected=b'one abX\nthree acY\n')
        run('uniq', ['-w0'], b'a\nb\n', expected=b'a\n')
        run('uniq', ['-s18446744073709551615'], b'a\nb\n', expected=b'a\n')
        run('uniq', ['-', 'unique'], repeated, expected=b'')
        assert (directory / 'unique').read_bytes() == b'a\nb\na\nA\n'
        run('uniq', ['-', '-'], b'a\na', expected=b'a\n')
        run('uniq', ['--', '-dash'], expected=b'dash\n')
        run('uniq', ['one', 'missing/output'], status=1)
        run('uniq', ['-', 'unique'], repeated, status=1, env={'FILTER_CLOSE_FAIL': '3'})
        run('uniq', ['-', 'unique'], repeated, status=1, env={'FILTER_WRITE_FAIL': '3'})
        rng = random.Random(1822)
        for _ in range(50):
            lines = [bytes(rng.choice(b'abAB \t\x00\xff') for _ in range(rng.randrange(40))) for _ in range(80)]
            source = b'\n'.join(lines) + b'\n'
            for reverse in (False, True):
                run('sort', ['-r'] if reverse else [], source,
                    expected=b''.join(line + b'\n' for line in sorted(lines, reverse=reverse)))
            run('sort', ['-u'], source, expected=b''.join(line + b'\n' for line in sorted(set(lines))))
            adjacent = [line for i, line in enumerate(lines) if not i or line != lines[i - 1]]
            run('uniq', data=source, expected=b''.join(line + b'\n' for line in adjacent))
        for _ in range(30):
            lines = [(rng.choice(['', '-', '  -', '\t']) + str(rng.randrange(10**40)) + '.' +
                      str(rng.randrange(10**40))).encode() for _ in range(80)]
            source = b'\n'.join(lines) + b'\n'
            ordered = sorted(lines, key=lambda line: (Decimal(line.decode().strip()), line))
            run('sort', ['-n'], source, expected=b''.join(line + b'\n' for line in ordered))
        return 'Lexical/numeric ordering, equality keys, stable unique representatives, binary lines and output operands.'

    report.check('sort-and-uniq', ordering)

    def selection_translation():
        source = b'a:b:c:d\nplain\n:a::\n\nlast:field'
        run('cut', ['-d:', '-f3,1-2,2,4-'], source, expected=source + b'\n')
        run('cut', ['-sf2,4', '-d', ':'], source, expected=b'b:d\na:\nfield\n')
        run('cut', ['-d:', '-f9'], source, expected=b'\nplain\n\n\n\n')
        run('cut', ['-b', '3,1-2,2,5-'], b'abcdef\nxy\nlast', expected=b'abcef\nxy\nlas\n')
        run('cut', ['-c', '-2'], b'\0\xffz\n', expected=b'\0\xff\n')
        run('cut', ['-d', '', '-f2'], b'a\0b\0c\nx', expected=b'b\nx\n')
        run('cut', ['-f2'], b'a\tb\tc\nplain', expected=b'b\nplain\n')
        run('cut', ['-b1', 'two', '-', 'one'], b'partial', expected=b'o\np\nA\nb\na\nl\n')
        run('tr', ['a-z', 'A-Z'], b'abcXYZ\0\xff', expected=b'ABCXYZ\0\xff')
        run('tr', ['[:upper:]', '[:lower:]'], b'ABC def\n', expected=b'abc def\n')
        run('tr', ['abc', 'x'], b'abcd', expected=b'xxxd')
        run('tr', ['-t', 'abc', 'x'], b'abcd', expected=b'xbcd')
        run('tr', ['-t', 'abc', ''], b'abcd', expected=b'abcd')
        run('tr', ['aba', 'xyz'], b'abc', expected=b'zyc')
        run('tr', ['-s', '[:space:]'], b'a  \t\tb\n\n', expected=b'a \tb\n')
        run('tr', ['-s', 'a-z', 'x'], b'abcaad', expected=b'x')
        run('tr', ['-ds', 'x', 'a'], b'axaxbaax', expected=b'aba')
        run('tr', ['-cd', '[:digit:]'], b'x01\xff\x00y2\n', expected=b'012')
        run('tr', ['-cs', 'a'], b'aa\0\0bba\xff\xff', expected=b'aa\0ba\xff')
        run('tr', ['-c', 'a', 'x'], b'a\x00\xffbc', expected=b'axxxx')
        run('tr', [r'\000-\377', r'\377-\377'], bytes(range(256)), expected=b'\xff' * 256)
        run('tr', [r'\a\b\f\n\r\t\v\\', '12345678'], b'\a\b\f\n\r\t\v\\', expected=b'12345678')
        run('tr', ['--', '-a', 'xy'], b'-ab', expected=b'xyb')
        classes = {'alnum': b'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789',
                   'alpha': b'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ', 'blank': b' \t',
                   'cntrl': bytes(range(32)) + b'\x7f', 'digit': b'0123456789', 'graph': bytes(range(33, 127)),
                   'lower': b'abcdefghijklmnopqrstuvwxyz', 'print': bytes(range(32, 127)),
                   'punct': b'!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~', 'space': b' \t\n\r\v\f',
                   'upper': b'ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'xdigit': b'0123456789abcdefABCDEF'}
        for name, members in classes.items():
            run('tr', ['-cd', '[:' + name + ':]'], bytes(range(256)), expected=bytes(sorted(members)))
        rng = random.Random(1823)
        for _ in range(50):
            source = bytes(rng.randrange(256) for _ in range(2048))
            table = bytes((ch - 32 if 97 <= ch <= 122 else ch) for ch in range(256))
            run('tr', ['a-z', 'A-Z'], source, expected=source.translate(table))
            lines = source.split(b'\n')
            if not lines[-1]:
                lines.pop()
            run('cut', ['-b2-5,9-'], source, expected=b''.join(line[1:5] + line[8:] + b'\n' for line in lines))
        return 'Field/byte selections, classes, octal/ranges, complement, delete/translate/squeeze and binary streams.'

    report.check('cut-and-tr', selection_translation)

    def new_bounds():
        for options in (['-b', ''], ['-b0'], ['-b-'], ['-b2-1'], ['-b1,'], ['-b,1'], ['-b1,,2'],
                        ['-b1-2-3'], ['-b18446744073709551616'], ['-b1', '-f1'], ['-b1', '-s'],
                        ['-f1', '-dxx'], ['-f'], ['-f1', '-d'], ['-b' + ','.join(['1'] * 129)]):
            run('cut', options, status=2)
        for options in (['-f'], ['-s-1'], ['-w1x'], ['-w18446744073709551616'], ['one', 'two', 'one']):
            run('uniq', options, status=2)
        for options in ([], ['a'], ['-d', 'a', 'b'], ['-ds', 'a'], ['a', ''], ['z-a', 'x'],
                        ['\\', 'x'], [r'\400', 'x'], ['[:bad:]', 'x'], ['[:alpha', 'x'],
                        ['[=a=]', 'x'], ['[a*3]', 'x'], ['a', '[:digit:]'], ['a-z', '[:lower:]'],
                        ['a' * 1025, 'x']):
            run('tr', options, status=2)
        run('sort', data=b'a' * 131071, expected=b'a' * 131071 + b'\n')
        run('sort', data=b'a' * 131072, status=1, expected=b'')
        run('sort', data=b'\n' * 4096, expected=b'\n' * 4096)
        run('sort', data=b'\n' * 4097, status=1, expected=b'')
        for program, args in [('cut', ['-b1-']), ('uniq', [])]:
            run(program, args, b'x' * 65536 + b'\n', expected=b'x' * 65536 + b'\n')
            run(program, args, b'x' * 65537 + b'\n', status=1, expected=b'')
        env = {'FILTER_READ_CHUNK': '7', 'FILTER_WRITE_CHUNK': '3'}
        source = b'cc\naa\naa\nbb'
        run('sort', data=source, expected=b'aa\naa\nbb\ncc\n', env=env)
        run('uniq', data=source, expected=b'cc\naa\nbb\n', env=env)
        run('cut', ['-b2'], source, expected=b'c\na\na\nb\n', env=env)
        run('tr', ['a-z', 'A-Z'], source, expected=source.upper(), env=env)
        run('tr', ['-d', 'x'], b'x' * 300000 + b'y', expected=b'y')

    report.check('new-filter-bounds-and-io', new_bounds)

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
            args = default_args(program)
            status = 2 if program == 'grep' else 1
            run(program, args, b'xyz\n', status=status, env={'FILTER_READ_FAIL': '0'})
            run(program, args, b'xyz\n', status=status, env={'FILTER_WRITE_FAIL': '1'})
        for program in ('grep', 'wc', 'head', 'tail', 'more', 'sort', 'uniq', 'cut'):
            args = ['a'] if program == 'grep' else default_args(program)
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
                args = default_args(program)
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
