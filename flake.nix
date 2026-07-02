{
  description = "ESP32 development environment";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";
    esp-devkit = {
      url = "git+file:./esp-devkit";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
    };
  };

  outputs = { self, nixpkgs, flake-utils, esp-devkit }:
    flake-utils.lib.eachDefaultSystem (system:
      let pkgs = import nixpkgs { inherit system; };
      in {
        devShells.default = pkgs.mkShell {
          inputsFrom = [ esp-devkit.devShells.${system}.default ];
          packages = [ pkgs.nodejs_22 ];  # editor/ 用に追加するだけ
        };
      }
    );
}
