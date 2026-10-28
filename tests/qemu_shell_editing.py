"""Exercise shell insertion, deletion, wrap, completion, caret blinking and listing ABI."""
from pathlib import Path
import argparse, os, shutil
from common import OUT, command, require_success, Report
from qemu_accounts import boot, ADMIN_PASSWORD
from qemu_shell import build_probe
from session import provision_imports

def test(firmware, build_directory=None):
    directory = OUT/('qemu-shell-editing-'+firmware)
    report = Report('qemu-shell-editing-'+firmware, directory)
    built = build_directory or OUT/('build-'+firmware)
    def run():
        probe = build_probe(directory, built/'source', extra=('console_probe.cpp','CONPROBE'))
        image = directory/'HackOS.img'
        shutil.copy2(built/'HackOS.img', image)
        volume = str(image)+('@@433152' if firmware=='bios' else '')
        require_success(command(['mcopy','-o','-i',volume,probe,'::/USR/BIN/CONPROBE']), 'install console probe')
        provision_imports(image,firmware)
        with boot(directory,image,firmware,'editing',120) as guest:
            guest.fresh=True
            guest.setup()
            guest.enter('admin','New password: ')
            guest.password(ADMIN_PASSWORD,'Action: ')
            guest.login('admin',ADMIN_PASSWORD)
            script=directory/'caret.gdb'
            script.write_text('set pagination off\nset confirm off\ntarget remote '+str(guest.gdb)+'''\npython
import gdb
seen=set()
bp=gdb.Breakpoint('Display::DrawBackbuffer()',type=gdb.BP_HARDWARE_BREAKPOINT)
for i in range(4):
    gdb.execute('continue')
    bp.enabled=False
    gdb.execute('finish')
    d=gdb.parse_and_eval('display')
    visible=int(d['caretVisible'])
    seen.add(visible)
    if visible:
        x=int(d['caretX'])*int(d['console']['char_width'])
        y=int(d['caretY'])*int(d['console']['char_height'])
        pixel=int(d['framebuffer']['address'][y*int(d['framebuffer']['pitch'])+x])
        assert pixel == 0xffffffff, 'Caret not drawn'
    bp.enabled=True
assert seen == {0,1}, 'Caret does not blink'
bp.delete()
print('CARET_BLINK_OK')
end
detach
quit
''')
            result=command([os.environ.get('GDB','gdb'),'--batch','--nx','-q',built/'kernel.elf','-x',script],timeout=30)
            (directory/'caret.log').write_text(result.stdout)
            require_success(result,'blinking caret')
            guest.qmp.call('cont')
            def edit(text, keys, insert, expected):
                guest.qmp.type(text)
                for key in keys: guest.qmp.key(key)
                guest.qmp.type(insert)
                offset=len(guest.output())
                guest.qmp.key('ret')
                guest.wait('\n'+expected+'\n',offset)
                guest.wait('admin@root:~$ ',offset)
            guest.qmp.type('echo ac')
            guest.qmp.key('left')
            script.write_text(script.read_text().replace("print('CARET_BLINK_OK')", "assert int(gdb.parse_and_eval('display.caretBack')) == 1, 'Caret did not move left'\nprint('CARET_LEFT_OK')"))
            result=command([os.environ.get('GDB','gdb'),'--batch','--nx','-q',built/'kernel.elf','-x',script],timeout=30)
            (directory/'caret-left.log').write_text(result.stdout)
            require_success(result,'caret follows left arrow')
            guest.qmp.call('cont')
            offset=len(guest.output())
            guest.qmp.type('b\n')
            guest.wait('\nabc\n',offset)
            guest.wait('admin@root:~$ ',offset)
            edit('echo axbc',['left','left','backspace'],'','abc')
            edit('echo axbc',['left','left','left','delete'],'','abc')
            edit('echo '+ 'a'*140,['left']*70,'Z','a'*70+'Z'+'a'*70)
            guest.shell('echo old','\nold\n')
            edit('', ['up','left','left','left','delete','delete','delete'], 'new','new')
            edit('ec tail',['left']*5+['tab'],'','tail')
            guest.shell('CONPROBE','CONSOLE_ABI_OK')
            guest.shell('CONPROBE background &','CURSOR_DENIED_OK')
            guest.shell('ls -la /USR/BIN > /tmp/list','admin@root:~$ ')
            guest.screen('editing')
            require_success(command(['mcopy','-i',volume,'::/TMP/LIST',directory/'listing.txt']), 'read listing')
            listing=(directory/'listing.txt').read_text()
            assert 'MODE       UID GID ATTR       BYTES NAME' in listing and 'CONPROBE' in listing
        return 'Caret blink pixels, middle insertion/backspace/delete, wrapped lines, history, completion, directory pages and cursor ownership.'
    report.check('shell_editing_and_listing',run)
    return report.finish()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios','uefi'),required=True)
    parser.add_argument('--build-directory', type=Path)
    args = parser.parse_args()
    raise SystemExit(test(args.firmware, args.build_directory))
