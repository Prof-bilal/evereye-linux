{ lib, stdenv, cmake, qt6, pkg-config, dbus, python3, glib, makeWrapper }:
stdenv.mkDerivation {
  pname = "evereye";
  version = "0.4.0";
  src = lib.cleanSourceWith {
    src = ../..;
    filter = path: type:
      let name = baseNameOf path;
      in !(type == "directory" && (lib.hasPrefix "build" name || builtins.elem name [ "dist" "test-results" "__pycache__" ".git" ".flatpak-builder" ".cache" ]));
  };
  nativeBuildInputs = [ cmake pkg-config qt6.wrapQtAppsHook makeWrapper ];
  buildInputs = [ qt6.qtbase qt6.qtwayland ];
  nativeCheckInputs = [ dbus python3 glib ];
  cmakeFlags = [ "-DEVEREYE_INSTALL_USER_SERVICE=OFF" ];
  doCheck = true;
  preCheck = ''
    export QT_QPA_PLATFORM=offscreen
    export QT_QPA_PLATFORMTHEME=
    export QT_STYLE_OVERRIDE=Fusion
  '';
  meta = {
    description = "Timed awake sessions with battery safeguards";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "evereye";
  };
}
