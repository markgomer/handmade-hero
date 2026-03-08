{
    description = "Handmade Hero development environment";

    inputs = {
        nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    };

    outputs = { self, nixpkgs }:
    let
        systems = [ "x86_64-linux" ];
        forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
        devShells = forAllSystems (system:
        let
            pkgs = nixpkgs.legacyPackages.${system};
        in
        {
            default = pkgs.mkShell {
                packages = [ ];

                # dependencies needed at runtime
                buildInputs = [
                    pkgs.libx11
                    pkgs.alsa-lib
                    pkgs.SDL2
                ];
                # tools needed only during the build process
                nativeBuildInputs = [
                    pkgs.gcc
                    pkgs.clang-tools # for neovim's LSP
                ];
            };
        });
    };
}
