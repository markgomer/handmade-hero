{
  description = "Handmade Hero development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      # Systems to support
      systems = [ "x86_64-linux" ];

      # Helper to generate an attribute set for each system
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShellNoCC {
            packages = [
              pkgs.clang-tools
              pkgs.libx11
              pkgs.neovim
            ];
          };
        }
      );
    };
}
