Run in a user namespace in which /nix points to some other path (e.g. ~/.nix) in the "real" filesystem.

Need this since root directory / isn't writeable from a user account.

NOTE: user namespace cannot be nested,  so for example,  cannot use nix
features that rely on user namespace (e.g. to put /nix/store somewhere else)

Modelled after lethalman's version https://github.com/lethalman/nix-user-chroot

clone:
```
$ git clone ...
$ cd nix-user-chroot
```

build (after editing `Makefile`,  it's a two-liner)
```
$ make
```

configure
```
$ mkdir -p ~/.nix
$ ./nix-user-chroot
```

`nix-user-chroot` sets `NIX_CONF_DIR` to `/nix/etc/nix` (instead of `/etc/nix`),
since if you can't write to `/` you probably can't write to `/etc` either
