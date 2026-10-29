# Graphical Settings

[System index](index.md) · [Desktop](desktop.md) · [Accounts](accounts.md)

Launch **Settings** from the desktop launcher, or run `settings` in a terminal.
The application uses the shared GUI library and closes with its title-bar button
or Alt-F4. Tab/Shift-Tab navigate controls; Enter activates buttons and lists.

## Information pages

System, CPU, Memory, Devices, Display and Firmware present copied `sysinfo`
records in scrollable lists. Memory includes the firmware map; Devices includes
storage and PCI/USB controller discovery; Display includes PS/2 state; Firmware
lists ACPI tables. Switch pages to refresh the snapshot. Hardware queries retain
the existing UID-0 requirement. Ordinary users see a permission explanation.
Enumeration is bounded to 512 display rows. Discovery does not imply a working
USB driver, GPU acceleration or active multiprocessing.

## Accounts and passwords

All users can inspect the users, groups, primary GID and supplementary GIDs.
Only UID 0 can create or rename users/groups, add/remove supplementary membership,
or change a user's primary group. Select a user and group, then use the controls
below the lists. Creation uses the selected primary group. New users remain
locked until an administrator sets their password in the Password page.

Names follow the existing account syntax and must be unique within their type.
Renaming preserves the UID/GID, password and filesystem ownership. The initial
administrator remains UID 0 and must retain primary GID 0. Removing a primary
membership is rejected; choose another primary group first. Running processes
retain their credential snapshot: group changes take effect on the next login.
There is no account/group deletion in this slice.

The Password page changes the caller's password after checking their current
password. Administrators may instead reset the user selected in Accounts without
knowing that user's old password. New and confirmation values must match and use
8–128 printable ASCII characters. Fields are masked and cleared after submission,
when switching pages and on close. Wrong current passwords are rate limited.
Changes use the existing PBKDF2, entropy and two-slot durable account store; disk
failure leaves the previous in-memory account registry intact.

Syscall 95, `account_edit(operation, id, value, name)`, is root-only. Operations
0/1 rename a user/group; 2 sets a user's primary GID; 3 removes a supplementary
GID. Only rename operations read `name`. Success returns 0, permission denial
`HACKOS_DENIED`, and invalid input/storage failure `HACKOS_ERROR`.
Syscall 96, `password_change(old, new)`, changes only the caller's password and
returns 0, `HACKOS_DENIED`, `HACKOS_RETRY`, `HACKOS_NO_ENTROPY` or `HACKOS_ERROR`.
The existing root-only `password_set` API remains unchanged.

## Terminal appearance

The Terminal page exposes the supported recovery-console font height and RGBA
colours, plus reset. Values use even font heights 8–64 and exactly eight hex
colour digits, RRGGBBAA. Changes clear the recovery console and last until reboot.
The existing foreground-owner checks apply: launch `settings` in the foreground
from a recovery console to change these values. Desktop terminal windows retain
their fixed font and palette; a desktop-launched Settings app cannot change the
recovery console. Session-wide graphical themes are not implemented.

## Validation

`make -C tests test-settings FIRMWARE=bios` (and `uefi`) exercises information
pages, account/group creation and renaming, membership changes, duplicate-name
rejection, read-only guest controls, verified password changes and reboot
persistence. Guest probes directly test kernel authorization independently of
disabled GUI controls. Registry host tests cover stable IDs, primary-group
invariants and serialized account validation.
