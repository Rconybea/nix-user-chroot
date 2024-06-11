nix-user-chroot: nixuserchroot_main.cpp
	g++ -arch arm64 -mmacosx-version-min=11.0 -std=gnu++20 -o nix-user-chroot nixuserchroot_main.cpp
