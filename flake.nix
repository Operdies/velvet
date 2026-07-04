{
  description = "A fully scriptable terminal multiplexer";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    utf8proc = {
      url = "github:JuliaStrings/utf8proc";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, utf8proc }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages = rec {
          default = velvet;

          velvet = pkgs.stdenv.mkDerivation {
            pname = "velvet";
            version = self.shortRev or "dev";

            src = self;

            nativeBuildInputs = with pkgs; [ gnumake ];

            VELVET_VERSION = self.shortRev or "dev";

            dontConfigure = true;

            postPatch = ''
              rm -rf deps/utf8proc
              mkdir -p deps
              cp -r ${utf8proc} deps/utf8proc
              chmod -R +w deps/utf8proc
            '';

            buildPhase = ''
              runHook preBuild
              make -j8 CC=cc GIT=true VELVET_VERSION="${self.shortRev or "dev"}" release
              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall
              make install PREFIX=$out
              runHook postInstall
            '';

            meta = with pkgs.lib; {
              description = "A fully scriptable terminal multiplexer";
              homepage = "https://velvet.opie.lol";
              license = licenses.gpl3Plus;
              platforms = platforms.linux ++ platforms.darwin;
              mainProgram = "vv";
            };
          };
        };
      }
    );
}
