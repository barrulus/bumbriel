{
  pkgs,
  umbriel,
}:
pkgs.mkShell {
  inputsFrom = [ umbriel ];

  # The debug and ASan recipes use -O0. Fortify requires optimization and
  # otherwise emits a warning in every translation unit. Packaged builds keep
  # their normal hardening; this setting only affects the development shell.
  hardeningDisable = [ "fortify" ];

  nativeBuildInputs = with pkgs; [
    just
    lefthook
    meson
    ninja
    pkg-config
    wayland-scanner
    llvmPackages_22.clang-tools
    llvmPackages_22.libclang
    gnugrep
    gnused
    findutils
    gdb
    grim
    jq
    foot
    python3
    imagemagick
    procps
    xwayland-satellite
  ];

  shellHook = ''
    echo " Umbriel dev-shell | 'just --list' to see available tasks"
  '';
}
